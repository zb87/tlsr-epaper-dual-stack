#!/usr/bin/env python3
"""
init_endor0420_mac.py - Generate MAC & Crystal Trim Binary for BOARD_ENDOR_0420

WHY THIS IS NEEDED
------------------
On BLOZI Endor 4.2" (Endor-Telink-0420) tags, the stock firmware stores display
bitmaps across 0x73ABA-0x7824F, overwriting Telink's factory MAC sector (0x76000)
and 24 MHz crystal capacitance trim sector (0x77000) with 0x00 bytes.
Because blc_initMacAddress() only auto-generates a MAC when 0x76000 is 0xFF,
those two 4 KB sectors (8 KB total: 0x76000-0x77FFF) must be initialized with a
valid non-zero BLE MAC address (Little-Endian) and crystal trim (default 0x60)
before running tlsr-epaper-endor0420.bin.

WIRING (tlsr-flasher H1 -> Endor-0420 P6 test pads, battery disconnected)
------------------------------------------------------------------------
  tlsr-flasher H1.1 (GND) -> P6 Pin 1 (Square pad, GND)
  tlsr-flasher H1.2 (PWR) -> P6 Pin 5 (3V3)
  tlsr-flasher H1.4 (SWM) -> P6 Pin 3 (SWS / PA7)

USAGE
-----
  1. Generate an 8 KB factory blob with a random BLE MAC (default output:
     /tmp/endor0420_factory_mac.bin) and print TlsrPgm.py flash commands:
       python3 script/init_endor0420_mac.py

  2. Specify a custom output path or explicit MAC address:
       python3 script/init_endor0420_mac.py -o /tmp/my_mac.bin --mac A4:C1:38:00:A0:BF

  3. Specify your serial port and path to TlsrPgm.py so the printed commands
     are ready to copy/paste directly:
       python3 script/init_endor0420_mac.py -p /dev/cu.usbmodem14101 --flasher ../tlsr-flasher/TlsrPgm.py

  4. Run the printed TlsrPgm.py commands from https://github.com/zb87/tlsr-flasher
     to flash the MAC/trim blob at 0x76000 and the firmware at 0x00000.
"""

import argparse
import os
import secrets
import sys

STANDARD_MAC_ADDR = 0x76000
STANDARD_CAL_ADDR = 0x77000
FACTORY_BLOB_SIZE = 8192  # 4 KB MAC sector (0x76000) + 4 KB crystal trim sector (0x77000)
DEFAULT_CRYSTAL_TRIM = 0x60
DEFAULT_OUTPUT_PATH = "/tmp/endor0420_factory_mac.bin"

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT_FW_PATH = os.path.join(REPO_ROOT, "bin", "tlsr-epaper-endor0420.bin")


def generate_random_mac() -> bytes:
    """Generate a random 6-byte unicast BLE MAC (Big-Endian: A4:C1:38:XX:XX:XX)."""
    while True:
        tail = secrets.token_bytes(3)
        if tail not in (b"\x00\x00\x00", b"\xff\xff\xff"):
            return bytes([0xA4, 0xC1, 0x38]) + tail


def parse_mac_be(mac_str: str) -> bytes:
    """Parse AA:BB:CC:DD:EE:FF (or hyphen-separated) into 6 Big-Endian bytes."""
    parts = mac_str.strip().replace("-", ":").split(":")
    if len(parts) != 6:
        raise ValueError(f"Invalid MAC address '{mac_str}' (expected AA:BB:CC:DD:EE:FF)")
    mac_be = bytes(int(p, 16) for p in parts)
    if mac_be in (b"\x00" * 6, b"\xff" * 6):
        raise ValueError("MAC address cannot be all 0x00 or all 0xFF")
    if mac_be[0] & 0x01:
        raise ValueError(f"MAC address '{mac_str}' has multicast bit set (LSB of first byte must be 0)")
    return mac_be


def format_mac_be(mac_be: bytes) -> str:
    return ":".join(f"{b:02X}" for b in mac_be)


def build_factory_blob(mac_be: bytes, crystal_trim: int) -> bytearray:
    """
    Build the 8 KB factory blob for 0x76000-0x77FFF:
      - 0x0000..0x0005 (flash 0x76000): 6-byte MAC in Little-Endian byte order
      - 0x1000         (flash 0x77000): 1-byte 24 MHz crystal capacitance trim
      - All remaining bytes: 0xFF
    """
    blob = bytearray([0xFF] * FACTORY_BLOB_SIZE)
    blob[0:6] = bytes(reversed(mac_be))
    blob[0x1000] = crystal_trim & 0xFF
    return blob


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate an 8 KB MAC (0x76000) + crystal trim (0x77000) blob for BOARD_ENDOR_0420 and print TlsrPgm.py flash commands."
    )
    parser.add_argument("-o", "--output", default=DEFAULT_OUTPUT_PATH,
                        help=f"Output binary path (default: {DEFAULT_OUTPUT_PATH})")
    parser.add_argument("--mac", help="Explicit MAC address (AA:BB:CC:DD:EE:FF). Random A4:C1:38:xx:xx:xx if omitted.")
    parser.add_argument("--trim", type=lambda x: int(x, 0), default=DEFAULT_CRYSTAL_TRIM,
                        help="Crystal cap trim byte at 0x77000 (default: 0x60)")
    parser.add_argument("-p", "--port", default="<PORT>",
                        help="Serial port to include in printed commands (e.g. /dev/cu.usbmodem* or /dev/ttyACM0)")
    parser.add_argument("--flasher", default="TlsrPgm.py",
                        help="Path to TlsrPgm.py to include in printed commands (default: TlsrPgm.py)")
    parser.add_argument("--fw", default="bin/tlsr-epaper-endor0420.bin",
                        help="Firmware binary path to include in printed commands (default: bin/tlsr-epaper-endor0420.bin)")

    args = parser.parse_args()

    mac_be = parse_mac_be(args.mac) if args.mac else generate_random_mac()
    mac_le = bytes(reversed(mac_be))
    blob = build_factory_blob(mac_be, args.trim)

    out_dir = os.path.dirname(os.path.abspath(args.output))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(blob)

    print(f"[*] Target Board : BOARD_ENDOR_0420 (TLSR8359 / TLSR8258)")
    print(f"[*] MAC Address  : {format_mac_be(mac_be)} (flash LE: {mac_le.hex(' ').upper()})")
    print(f"[*] Crystal Trim : 0x{args.trim & 0xFF:02X} at 0x{STANDARD_CAL_ADDR:05X}")
    print(f"[+] Wrote {len(blob)} bytes to: {args.output}")
    print()
    print("Flashing instructions (using TlsrPgm.py from https://github.com/zb87/tlsr-flasher):")
    print("  Wiring (battery disconnected): H1.1 GND -> P6.1, H1.2 PWR -> P6.5 (3V3), H1.4 SWM -> P6.3 (SWS)")
    print("  Replace <PORT> with your programmer port (e.g. /dev/cu.usbmodem* on macOS or /dev/ttyACM0 on Linux).")
    print()
    print("  # 1. (Optional) Back up original 512 KB flash first:")
    print(f"  python3 {args.flasher} -p {args.port} -b 1500000 -t 100 -a 150 rf 0x00000 0x80000 /tmp/endor0420_backup.bin")
    print()
    print("  # 2. Flash MAC (0x76000) & crystal trim (0x77000):")
    print(f"  python3 {args.flasher} -p {args.port} -b 1500000 -t 100 -a 150 we 0x{STANDARD_MAC_ADDR:05X} {args.output}")
    print()
    print("  # 3. Flash BOARD_ENDOR_0420 firmware (0x00000):")
    print(f"  python3 {args.flasher} -p {args.port} -b 1500000 -t 100 -a 150 we 0x00000 {args.fw}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
