const m = require('zigbee-herdsman-converters/lib/modernExtend');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const e = exposes.presets;
const ea = exposes.access;

const customClusters = {
    customEpaper: {
        name: 'customEpaper',
        ID: 0xFC00,
        attributes: {
            activeSlot: {
                name: 'activeSlot',
                ID: 0x0000,
                type: 0x20, // uint8
                write: true,
                read: true,
            },
            renderStyle: {
                name: 'renderStyle',
                ID: 0x0001,
                type: 0x20, // uint8
                write: true,
                read: true,
            },
            activeMode: {
                name: 'activeMode',
                ID: 0x0002,
                type: 0x20, // uint8
                write: true,
                read: true,
            },
            refreshCount: {
                name: 'refreshCount',
                ID: 0x0003,
                type: 0x21, // uint16
                read: true,
            },
            ledState: {
                name: 'ledState',
                ID: 0x0004,
                type: 0x20, // uint8
                write: true,
                read: true,
            },
            wakeupCount: {
                name: 'wakeupCount',
                ID: 0x0005,
                type: 0x23, // uint32
                read: true,
            },
            wakeupDuration: {
                name: 'wakeupDuration',
                ID: 0x0006,
                type: 0x23, // uint32
                read: true,
            },
            txDuration: {
                name: 'txDuration',
                ID: 0x0007,
                type: 0x23, // uint32
                read: true,
            },
            rxDuration: {
                name: 'rxDuration',
                ID: 0x0008,
                type: 0x23, // uint32
                read: true,
            },
        },
        commands: {},
        commandsResponse: {},
    },
};

const commonExtends = [
    m.deviceAddCustomCluster('customEpaper', customClusters.customEpaper),
    m.battery({
        percentage: true,
        voltage: true,
    }),
    m.identify(),
    m.enumLookup({
        name: 'render_style',
        cluster: 'customEpaper',
        attribute: 'renderStyle',
        lookup: {
            'standard': 0,
            'bw_standard': 1,
            'bw_inverted': 2,
            'rw_standard': 3,
            'rw_inverted': 4,
        },
        description: 'E-Paper color rendering style (0: Standard BWR, 1: B&W Std, 2: B&W Inv, 3: R&W Std, 4: R&W Inv)',
        access: 'ALL',
    }),
    m.enumLookup({
        name: 'mode',
        cluster: 'customEpaper',
        attribute: 'activeMode',
        lookup: {
            'zigbee': 1,
            'ble': 2,
        },
        description: 'Active protocol mode. Writing "ble" switches device to BLE mode.',
        access: 'ALL',
    }),
    m.numeric({
        name: 'screen_refresh_count',
        cluster: 'customEpaper',
        attribute: 'refreshCount',
        description: 'Total cumulative E-Paper screen refresh count',
        access: 'STATE_GET',
        entityCategory: 'diagnostic',
    }),
    m.numeric({
        name: 'wakeup_count',
        cluster: 'customEpaper',
        attribute: 'wakeupCount',
        description: 'Total device wake-up count since boot',
        access: 'STATE_GET',
        entityCategory: 'diagnostic',
    }),
    m.numeric({
        name: 'wakeup_duration',
        cluster: 'customEpaper',
        attribute: 'wakeupDuration',
        unit: 'ms',
        description: 'Cumulative active awake duration since boot',
        access: 'STATE_GET',
        entityCategory: 'diagnostic',
    }),
    m.numeric({
        name: 'tx_duration',
        cluster: 'customEpaper',
        attribute: 'txDuration',
        unit: 'ms',
        description: 'Cumulative wireless TX active duration since boot',
        access: 'STATE_GET',
        entityCategory: 'diagnostic',
    }),
    m.numeric({
        name: 'rx_duration',
        cluster: 'customEpaper',
        attribute: 'rxDuration',
        unit: 'ms',
        description: 'Cumulative wireless RX active duration since boot',
        access: 'STATE_GET',
        entityCategory: 'diagnostic',
    }),
];

const definition_m3na = {
    zigbeeModel: ['TLSR-M3Na-E31HA'],
    model: 'TLSR-M3Na-E31HA',
    vendor: 'ZB-DIY',
    description: 'Hanshow Stellar-M3N@ / E31HA 2.13-inch BWR E-Paper Dual-Stack (Zigbee + BLE)',
    extend: [
        m.enumLookup({
            name: 'active_slot',
            cluster: 'customEpaper',
            attribute: 'activeSlot',
            lookup: {
                'info': 0,
                'user1': 1,
                'user2': 2,
                'user3': 3,
                'user4': 4,
                'user5': 5,
                'user6': 6,
                'user7': 7,
                'user8': 8,
                'blank': 9,
            },
            description: 'Active screen slot (0: Info, 1..8: User Images 1..8 [4KB Compressed BWR], 9: Blank)',
            access: 'ALL',
        }),
        ...commonExtends,
    ],
    ota: true,
    meta: {},
};

const definition_xl3na = {
    zigbeeModel: ['TLSR-XL3Na-E31PA'],
    model: 'TLSR-XL3Na-E31PA',
    vendor: 'ZB-DIY',
    description: 'Hanshow Stellar-XL3N@ / E31PA 4.2-inch BWR E-Paper Dual-Stack (Zigbee + BLE)',
    extend: [
        m.enumLookup({
            name: 'active_slot',
            cluster: 'customEpaper',
            attribute: 'activeSlot',
            lookup: {
                'info': 0,
                'user1': 1,
                'user2': 2,
                'user3': 3,
                'user4': 4,
                'blank': 5,
            },
            description: 'Active screen slot (0: Info, 1..4: User Images 1..4 [8KB Compressed BWR], 5: Blank)',
            access: 'ALL',
        }),
        ...commonExtends,
    ],
    ota: true,
    meta: {},
};

module.exports = [definition_m3na, definition_xl3na];
