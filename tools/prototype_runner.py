#!/usr/bin/env python3
"""
TLSR8258 E-Paper Dynamic Prototype Runner & Log Monitor
Allows compiling small C snippets, uploading to flash at 0x70000 over BLE,
executing them instantly without rebooting, and streaming logs back over BLE.
"""

import os
import sys
import time
import argparse
import subprocess
import asyncio
import tempfile
from pathlib import Path

# UUIDs for TLSR E-Paper BLE Service
UUID_EPD_SERVICE = "00001314-0000-1000-8000-00805f9b34fb"
UUID_EPD_CMD     = "00001315-0000-1000-8000-00805f9b34fb"
UUID_EPD_DATA    = "00001316-0000-1000-8000-00805f9b34fb"

DEFAULT_FLASH_ADDR = 0x70000
MAX_SNIPPET_SIZE   = 0x4000  # 16 KB

# ANSI colors
C_RESET  = "\033[0m"
C_BOLD   = "\033[1m"
C_CYAN   = "\033[36m"
C_GREEN  = "\033[32m"
C_YELLOW = "\033[33m"
C_RED    = "\033[31m"
C_DIM    = "\033[2m"

def crc16_modbus(data: bytes) -> int:
    """Computes Modbus CRC-16 (poly 0xA001, init 0xFFFF)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def find_tc32_toolchain():
    """Locate tc32-elf-gcc on system"""
    which_gcc = subprocess.run(["which", "tc32-elf-gcc"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if which_gcc.returncode == 0 and which_gcc.stdout.strip():
        return str(Path(which_gcc.stdout.strip()).parent)
    
    if os.environ.get("TC32PATH") and os.path.exists(os.path.join(os.environ["TC32PATH"], "tc32-elf-gcc")):
        return os.environ["TC32PATH"]

    known_paths = [
        "../reference/ATC_TLSR_Paper/Firmware/tc32_linux/bin",
        "/opt/tc32/bin",
        "./tc32/bin"
    ]
    for p in known_paths:
        if os.path.exists(os.path.join(p, "tc32-elf-gcc")):
            return p
    return None

def compile_snippet(c_file: Path, out_bin: Path, link_addr: int = DEFAULT_FLASH_ADDR, project_root: Path = None):
    """Compiles a standalone C snippet targeting link_addr with custom linker script and section checks"""
    tc32_bin = find_tc32_toolchain()
    if not tc32_bin:
        raise RuntimeError("tc32-elf-gcc not found! Please ensure Telink TC32 toolchain is in PATH.")
    
    gcc = os.path.join(tc32_bin, "tc32-elf-gcc")
    objcopy = os.path.join(tc32_bin, "tc32-elf-objcopy")
    size_tool = os.path.join(tc32_bin, "tc32-elf-size")
    
    if not project_root:
        project_root = Path(__file__).resolve().parent.parent
    
    inc_dirs = [
        project_root / "src",
        project_root / "src" / "epd",
        project_root / "SDK" / "proj",
        project_root / "SDK" / "platform",
    ]
    
    inc_flags = []
    for inc in inc_dirs:
        if inc.exists():
            inc_flags.extend(["-I", str(inc)])
            
    obj_file = out_bin.with_suffix(".o")
    elf_file = out_bin.with_suffix(".elf")
    ld_file  = out_bin.with_suffix(".ld")
    
    # Generate explicit linker script to guarantee snippet_main entrypoint at offset 0
    linker_script_content = f"""ENTRY(snippet_main)
SECTIONS {{
    . = 0x{link_addr:08x};
    .text : {{
        KEEP(*(.text.snippet_main))
        *(.text .text.*)
    }}
    .rodata : {{
        *(.rodata .rodata.*)
    }}
    .data : {{
        *(.data .data.*)
    }}
    .bss : {{
        *(.bss .bss.* COMMON)
    }}
}}
"""
    ld_file.write_text(linker_script_content)

    print(f"{C_CYAN}🔨 Compiling {c_file.name} -> {out_bin.name} (Linked @ 0x{link_addr:05X})...{C_RESET}")
    
    # 1. Compile with function & data sections
    cmd_compile = [
        gcc, "-O2", "-std=gnu99", "-ffunction-sections", "-fdata-sections",
        "-DMCU_CORE_8258=1",
        *inc_flags,
        "-c", str(c_file), "-o", str(obj_file)
    ]
    res = subprocess.run(cmd_compile, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"{C_RED}Compilation failed:\n{res.stderr}{C_RESET}")
        return False
        
    # 2. Link using generated linker script and garbage-collection
    cmd_link = [
        gcc, "-O2", "-nostdlib",
        "-Wl,--gc-sections",
        "-T", str(ld_file),
        str(obj_file), "-lgcc", "-o", str(elf_file)
    ]
    res = subprocess.run(cmd_link, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"{C_RED}Linking failed:\n{res.stderr}{C_RESET}")
        return False

    # 3. Check section sizes: reject snippets with writable static/global variables (.data / .bss)
    cmd_size = [size_tool, "-A", str(elf_file)]
    res = subprocess.run(cmd_size, capture_output=True, text=True)
    if res.returncode == 0:
        data_size = 0
        bss_size = 0
        for line in res.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 2:
                sec_name = parts[0]
                if sec_name == ".data":
                    try: data_size = int(parts[1])
                    except ValueError: pass
                elif sec_name in (".bss", ".custom_bss", "COMMON"):
                    try: bss_size += int(parts[1])
                    except ValueError: pass
        if data_size > 0 or bss_size > 0:
            print(f"{C_RED}Error: Snippet contains writable static/global variables (.data: {data_size} B, .bss: {bss_size} B).{C_RESET}")
            print(f"{C_RED}Snippets execute directly from Flash (XIP) and do not have allocated RAM. Use stack variables or the provided epd_test_api_t frame buffer.{C_RESET}")
            return False
        
    # 4. Objcopy to raw binary
    cmd_objcopy = [objcopy, "-O", "binary", str(elf_file), str(out_bin)]
    res = subprocess.run(cmd_objcopy, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"{C_RED}Objcopy failed:\n{res.stderr}{C_RESET}")
        return False
        
    size = out_bin.stat().st_size
    if size > MAX_SNIPPET_SIZE:
        print(f"{C_RED}Error: Output binary size ({size} B) exceeds maximum partition size ({MAX_SNIPPET_SIZE} B / 16 KB)!{C_RESET}")
        return False

    print(f"{C_GREEN}✓ Successfully built {out_bin.name}: {size} bytes{C_RESET}")
    return True


async def run_ble_session(target_mac: str, c_path: Path, bank_opt: str = "auto", flash_addr: int = DEFAULT_FLASH_ADDR, listen_time: int = 15):
    """Connects to device over BLE, detects active bank, compiles snippet, uploads via CRC16 blocks, triggers execution, and monitors logs"""
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError:
        print(f"{C_RED}Error: 'bleak' is required for BLE communication.{C_RESET}")
        print("Please install it with: pip install bleak (or pip install --break-system-packages bleak)")
        return False

    device = None
    if target_mac:
        print(f"{C_CYAN}🔍 Connecting to device with address: {target_mac}...{C_RESET}")
        device = await BleakScanner.find_device_by_address(target_mac, timeout=10.0)
    else:
        print(f"{C_CYAN}🔍 Scanning for TLSR E-Paper device...{C_RESET}")
        devices = await BleakScanner.discover(timeout=5.0)
        for d in devices:
            if d.name and ("TLSR" in d.name or "E31" in d.name or "M3N" in d.name or "XL3" in d.name):
                device = d
                break
                
    if not device:
        print(f"{C_RED}No matching TLSR device found. Ensure advertising is active.{C_RESET}")
        return False

    print(f"{C_GREEN}✓ Found device: {device.name} [{device.address}]{C_RESET}")
    
    last_seen_seq = 0
    snippet_done = False

    def notification_handler(sender, data):
        nonlocal last_seen_seq, snippet_done
        if len(data) == 0:
            return
        marker = data[0]
        if marker == 0x89 and len(data) >= 7:
            # Execution result response
            ret = data[1] | (data[2] << 8)
            dur = data[3] | (data[4] << 8) | (data[5] << 16) | (data[6] << 24)
            color = C_GREEN if ret == 0 else C_RED
            print(f"\n{color}{C_BOLD}▶ Snippet Completed! Return code: {ret}, Duration: {dur} ms{C_RESET}\n")
            snippet_done = True
        elif marker == 0x8A and len(data) >= 3:
            # Log packet: [0x8A, seq_lo, seq_hi, ts_b0..b3, text...]
            seq = data[1] | (data[2] << 8)
            if seq == 0 and len(data) >= 5:
                # Sync / Idle packet: bytes 3..4 report latest seq
                latest = data[3] | (data[4] << 8)
                if latest > last_seen_seq:
                    last_seen_seq = latest
            if seq <= last_seen_seq and last_seen_seq > 0:
                return  # Drop duplicate log notification
            last_seen_seq = seq
            try:
                if len(data) >= 7:
                    ts_ms = data[3] | (data[4] << 8) | (data[5] << 16) | (data[6] << 24)
                    text = data[7:].decode('utf-8', errors='replace').rstrip('\x00')
                    sec = ts_ms / 1000.0
                    print(f"{C_DIM}[{sec:8.3f}s] #{seq:03d}{C_RESET} {text}")
                else:
                    text = data[3:].decode('utf-8', errors='replace').rstrip('\x00')
                    ts = time.strftime("%H:%M:%S")
                    print(f"{C_DIM}[{ts}] #{seq:03d}{C_RESET} {text}")
            except Exception:
                pass
        elif marker == 0x83:
            pass

    async with BleakClient(device) as client:
        print(f"{C_GREEN}✓ Connected to GATT server{C_RESET}")
        
        # 1. Query device status to determine active boot bank
        active_bank = 0
        try:
            status_val = await client.read_gatt_char(UUID_EPD_CMD)
            if len(status_val) >= 11:
                active_bank = status_val[10] # byte 10 is mcuBootAddrGet()
                print(f"{C_CYAN}ℹ️  Device active firmware bank: Bank {active_bank} ({'0x040000 OTA bank' if active_bank else '0x000000 base bank'}){C_RESET}")
        except Exception as e:
            print(f"{C_YELLOW}Notice: Could not read status bank ({e}); assuming Bank 0{C_RESET}")

        if bank_opt != "auto":
            target_bank = int(bank_opt)
        else:
            target_bank = active_bank

        # Calculate XIP linking address:
        # On Bank 1, hardware SPI controller offsets fetches by -0x40000.
        # Physical flash 0x70000 executes at XIP address 0x30000.
        link_addr = (flash_addr - 0x40000) if (target_bank == 1) else flash_addr
        bin_suffix = "_bank1.bin" if (target_bank == 1) else ".bin"
        bin_path = c_path.with_name(c_path.stem + bin_suffix)

        if not compile_snippet(c_path, bin_path, link_addr=link_addr):
            print(f"{C_RED}Failed to compile snippet for Bank {target_bank}. Aborting session.{C_RESET}")
            return False

        with open(bin_path, "rb") as f:
            binary_data = f.read()

        bin_len = len(binary_data)
        print(f"{C_CYAN}📦 Payload size: {bin_len} bytes targeting flash 0x{flash_addr:05X} (XIP link: 0x{link_addr:05X}){C_RESET}")

        # Subscribe to notifications on CMD characteristic
        await client.start_notify(UUID_EPD_CMD, notification_handler)
        print(f"{C_CYAN}✓ Subscribed to command & log notifications (0x1315){C_RESET}")
        
        # 2. Prepare prototype snippet upload: 0x40 <len_lo> <len_hi>
        prep_cmd = bytearray([0x40, bin_len & 0xFF, (bin_len >> 8) & 0xFF])
        await client.write_gatt_char(UUID_EPD_CMD, prep_cmd, response=True)
        await asyncio.sleep(0.05)
        
        # 3. Stream binary in 20-byte OTA-style CRC16 blocks to 0x1316
        print(f"{C_CYAN}🚀 Uploading binary ({bin_len} B) via 20-byte CRC16 blocks...{C_RESET}")
        chunk_size = 16
        total_blocks = (bin_len + chunk_size - 1) // chunk_size
        for blk_idx in range(total_blocks):
            start = blk_idx * chunk_size
            chunk = binary_data[start:start + chunk_size]
            if len(chunk) < 16:
                chunk = chunk + b"\xFF" * (16 - len(chunk))
            pkt = bytearray(20)
            pkt[0] = blk_idx & 0xFF
            pkt[1] = (blk_idx >> 8) & 0xFF
            pkt[2:18] = chunk
            crc = crc16_modbus(pkt[:18])
            pkt[18] = crc & 0xFF
            pkt[19] = (crc >> 8) & 0xFF
            await client.write_gatt_char(UUID_EPD_DATA, pkt, response=False)
            await asyncio.sleep(0.003) # 3ms pacing
            
        await asyncio.sleep(0.05)

        # 4. Verify upload completeness via telemetry readback on UUID_EPD_DATA
        telem = await client.read_gatt_char(UUID_EPD_DATA)
        if len(telem) >= 3:
            confirmed_off = telem[0] | (telem[1] << 8)
            error_code = telem[2]
            if error_code != 0 or confirmed_off < bin_len:
                print(f"{C_RED}❌ Upload verification failed! Error code: {error_code}, Confirmed: {confirmed_off}/{bin_len} B{C_RESET}")
                return False
            print(f"{C_GREEN}✓ Upload verified: {confirmed_off} bytes confirmed in RAM (err={error_code}){C_RESET}")
        else:
            print(f"{C_YELLOW}Warning: Telemetry read returned unexpected length {len(telem)}{C_RESET}")

        # 5. Enable real-time log streaming: 0x51 0x01
        await client.write_gatt_char(UUID_EPD_CMD, bytearray([0x51, 0x01]), response=True)
        
        # 6. Commit and execute: 0x41 0x01 <addr_0..3>
        addr_bytes = [
            flash_addr & 0xFF,
            (flash_addr >> 8) & 0xFF,
            (flash_addr >> 16) & 0xFF,
            (flash_addr >> 24) & 0xFF
        ]
        exec_cmd = bytearray([0x41, 0x01, *addr_bytes])
        print(f"{C_YELLOW}⚡ Triggering execution at Flash 0x{flash_addr:05X}...{C_RESET}")
        await client.write_gatt_char(UUID_EPD_CMD, exec_cmd, response=True)
        
        # 7. Listen and poll logs
        print(f"{C_CYAN}📋 Streaming device logs (timeout {listen_time}s, Ctrl+C to exit)...{C_RESET}\n")
        start_t = time.time()
        last_poll = time.time()
        
        try:
            while time.time() - start_t < listen_time:
                await asyncio.sleep(0.1)
                # Periodically poll for any dropped entries using last_seen_seq
                if time.time() - last_poll > 1.5:
                    poll_cmd = bytearray([0x50, last_seen_seq & 0xFF, (last_seen_seq >> 8) & 0xFF])
                    await client.write_gatt_char(UUID_EPD_CMD, poll_cmd, response=False)
                    last_poll = time.time()

                if snippet_done and (time.time() - last_poll > 0.5):
                    # Snippet finished; drain one last poll and exit cleanly
                    poll_cmd = bytearray([0x50, last_seen_seq & 0xFF, (last_seen_seq >> 8) & 0xFF])
                    await client.write_gatt_char(UUID_EPD_CMD, poll_cmd, response=False)
                    await asyncio.sleep(0.5)
                    break
        except asyncio.CancelledError:
            pass
            
        await client.stop_notify(UUID_EPD_CMD)
        print(f"\n{C_GREEN}✓ Prototyping session completed cleanly.{C_RESET}")
        return True


def main():
    parser = argparse.ArgumentParser(description="TLSR8258 E-Paper Dynamic Prototype Runner & Log Monitor")
    parser.add_argument("snippet", type=str, help="Path to C snippet file (e.g. tools/snippets/test_fast_refresh.c)")
    parser.add_argument("-m", "--mac", type=str, default=None, help="BLE MAC address of target device")
    parser.add_argument("-a", "--addr", type=lambda x: int(x, 0), default=DEFAULT_FLASH_ADDR, help="Flash address (default: 0x70000)")
    parser.add_argument("-b", "--bank", choices=["0", "1", "auto"], default="auto", help="Firmware bank targeting (default: auto)")
    parser.add_argument("-t", "--time", type=int, default=15, help="Log listening timeout in seconds (default: 15)")
    parser.add_argument("--compile-only", action="store_true", help="Compile only without uploading to BLE")
    parser.add_argument("--watch", action="store_true", help="Watch file and recompile/run on changes")

    args = parser.parse_args()

    c_path = Path(args.snippet).resolve()
    if not c_path.exists():
        print(f"{C_RED}Error: File {c_path} not found!{C_RESET}")
        sys.exit(1)

    if args.compile_only:
        if args.bank == "auto":
            # Compile both Bank 0 and Bank 1 binaries
            bin_bank0 = c_path.with_name(c_path.stem + ".bin")
            bin_bank1 = c_path.with_name(c_path.stem + "_bank1.bin")
            s0 = compile_snippet(c_path, bin_bank0, link_addr=args.addr)
            s1 = compile_snippet(c_path, bin_bank1, link_addr=(args.addr - 0x40000))
            if s0 and s1:
                print(f"{C_GREEN}✓ Dual-bank compilation complete:\n  Bank 0 (0x{args.addr:05X}): {bin_bank0}\n  Bank 1 (0x{(args.addr-0x40000):05X}): {bin_bank1}{C_RESET}")
            else:
                sys.exit(1)
        else:
            target_bank = int(args.bank)
            link_addr = (args.addr - 0x40000) if (target_bank == 1) else args.addr
            bin_suffix = "_bank1.bin" if (target_bank == 1) else ".bin"
            bin_path = c_path.with_name(c_path.stem + bin_suffix)
            if compile_snippet(c_path, bin_path, link_addr=link_addr):
                print(f"{C_GREEN}✓ Compile-only complete: {bin_path}{C_RESET}")
            else:
                sys.exit(1)
        return

    if args.watch:
        print(f"{C_CYAN}👀 Watching {c_path.name} for changes...{C_RESET}")
        last_mtime = c_path.stat().st_mtime
        while True:
            try:
                time.sleep(1.0)
                mtime = c_path.stat().st_mtime
                if mtime != last_mtime:
                    last_mtime = mtime
                    print(f"\n{C_YELLOW}File changed! Re-running session...{C_RESET}")
                    asyncio.run(run_ble_session(args.mac, c_path, bank_opt=args.bank, flash_addr=args.addr, listen_time=args.time))
            except KeyboardInterrupt:
                print("\nExiting watch mode.")
                break
    else:
        asyncio.run(run_ble_session(args.mac, c_path, bank_opt=args.bank, flash_addr=args.addr, listen_time=args.time))

if __name__ == "__main__":
    main()
