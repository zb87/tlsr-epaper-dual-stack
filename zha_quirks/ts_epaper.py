"""Custom ZHA Quirk for Hanshow Stellar E-Paper Dual-Stack (Zigbee 3.0 + BLE).

Devices:
- Hanshow Stellar-M3N@ / E31HA (Telink TLSR8258 + FM11NC08 NFC + 2.13" BWR E-Paper Display, 250x122)
- Hanshow Stellar-XL3N@ / E31PA (Telink TLSR8258 + FM11NC08 NFC + 4.2" BWR E-Paper Display, 400x300)
Manufacturer: ZB-DIY
Models: TLSR-M3Na-E31HA, TLSR-XL3Na-E31PA
"""

from __future__ import annotations

import logging
from typing import Final

import zigpy.types as t
from zigpy.zcl import ClusterType
from zigpy.zcl.foundation import BaseAttributeDefs, ZCLAttributeDef

from zhaquirks import CustomCluster

try:
    from zhaquirks.builder import (
        EntityType,
        QuirkBuilder,
        ReportingConfig,
        SensorDeviceClass,
        SensorStateClass,
    )
except ImportError:
    # Backward compatibility shim for environments exposing QuirkBuilder via zigpy.quirks.v2
    from zigpy.quirks.v2 import (  # type: ignore[no-redef]
        EntityType,
        QuirkBuilder,
        ReportingConfig,
        SensorDeviceClass,
        SensorStateClass,
    )

try:
    from zhaquirks.builder import UnitOfTime
except ImportError:
    try:
        from zigpy.quirks.v2.homeassistant import UnitOfTime  # type: ignore[no-redef]
    except ImportError:
        class UnitOfTime:  # type: ignore[no-redef]
            MILLISECONDS = "ms"
            SECONDS = "s"

_LOGGER = logging.getLogger(__name__)

# -----------------------------------------------------------------------------
# Enums for Select / Dropdown Entities
# -----------------------------------------------------------------------------

class ActiveSlotM3(t.enum8):
    """Active screen slot for Stellar-M3N@ / E31HA (0..9).

    Slot 0: Info dashboard (runtime telemetry & device status)
    Slots 1..8: User-uploaded compressed BWR image slots (4 KB each)
    Slot 9: Blank clean white screen
    """

    Info = 0
    User_1 = 1
    User_2 = 2
    User_3 = 3
    User_4 = 4
    User_5 = 5
    User_6 = 6
    User_7 = 7
    User_8 = 8
    Blank = 9


class ActiveSlotXL3(t.enum8):
    """Active screen slot for Stellar-XL3N@ / E31PA (0..5).

    Slot 0: Info dashboard (runtime telemetry & device status)
    Slots 1..4: User-uploaded compressed BWR image slots (8 KB each)
    Slot 5: Blank clean white screen
    """

    Info = 0
    User_1 = 1
    User_2 = 2
    User_3 = 3
    User_4 = 4
    Blank = 5


# Alias for backward compatibility
ActiveSlot = ActiveSlotM3


class RenderStyle(t.enum8):
    """EPD color rendering style (0..4).

    0: Standard Tri-Color (Black, White, Red)
    1: Black & White Standard
    2: Black & White Inverted
    3: Red & White Standard
    4: Red & White Inverted
    """

    Standard_BWR = 0
    BW_Standard = 1
    BW_Inverted = 2
    RW_Standard = 3
    RW_Inverted = 4


class ActiveMode(t.enum8):
    """Active protocol stack mode (1=Zigbee, 2=BLE)."""

    Zigbee = 1
    BLE = 2


# -----------------------------------------------------------------------------
# Custom Cluster Definition (0xFC00)
# -----------------------------------------------------------------------------

class CustomEpaperCluster(CustomCluster):
    """Custom Cluster 0xFC00 for E-Paper Display & Dual-Stack Telemetry."""

    cluster_id: Final = 0xFC00
    name: Final = "custom_epaper"

    class AttributeDefs(BaseAttributeDefs):
        """Custom Cluster Attribute Definitions."""

        active_slot: Final = ZCLAttributeDef(
            id=0x0000,
            type=t.uint8_t,
            access="rw",
            is_manufacturer_specific=False,
        )
        render_style: Final = ZCLAttributeDef(
            id=0x0001,
            type=t.uint8_t,
            access="rw",
            is_manufacturer_specific=False,
        )
        active_mode: Final = ZCLAttributeDef(
            id=0x0002,
            type=t.uint8_t,
            access="rw",
            is_manufacturer_specific=False,
        )
        refresh_count: Final = ZCLAttributeDef(
            id=0x0003,
            type=t.uint16_t,
            access="r",
            is_manufacturer_specific=False,
        )
        led_state: Final = ZCLAttributeDef(
            id=0x0004,
            type=t.uint8_t,
            access="rw",
            is_manufacturer_specific=False,
        )
        wakeup_count: Final = ZCLAttributeDef(
            id=0x0005,
            type=t.uint32_t,
            access="r",
            is_manufacturer_specific=False,
        )
        wakeup_duration: Final = ZCLAttributeDef(
            id=0x0006,
            type=t.uint32_t,
            access="r",
            is_manufacturer_specific=False,
        )
        tx_duration: Final = ZCLAttributeDef(
            id=0x0007,
            type=t.uint32_t,
            access="r",
            is_manufacturer_specific=False,
        )
        rx_duration: Final = ZCLAttributeDef(
            id=0x0008,
            type=t.uint32_t,
            access="r",
            is_manufacturer_specific=False,
        )


# -----------------------------------------------------------------------------
# Quirk Registration via QuirkBuilder (Quirks V2)
# -----------------------------------------------------------------------------

def _register_epaper_quirk(
    primary_model: str,
    slot_enum: type[t.enum8],
    additional_models: list[str] | None = None,
) -> None:
    qb = QuirkBuilder("ZB-DIY", primary_model)
    if additional_models:
        for m in additional_models:
            qb.applies_to("ZB-DIY", m)

    (
        qb.replaces(CustomEpaperCluster, endpoint_id=1)
        .enum(
            attribute_name=CustomEpaperCluster.AttributeDefs.active_slot.name,
            enum_class=slot_enum,
            cluster_id=CustomEpaperCluster.cluster_id,
            endpoint_id=1,
            translation_key="active_slot",
            fallback_name="Active slot",
        )
        .enum(
            attribute_name=CustomEpaperCluster.AttributeDefs.render_style.name,
            enum_class=RenderStyle,
            cluster_id=CustomEpaperCluster.cluster_id,
            endpoint_id=1,
            translation_key="render_style",
            fallback_name="Render style",
        )
        .enum(
            attribute_name=CustomEpaperCluster.AttributeDefs.active_mode.name,
            enum_class=ActiveMode,
            cluster_id=CustomEpaperCluster.cluster_id,
            endpoint_id=1,
            entity_type=EntityType.CONFIG,
            translation_key="active_mode",
            fallback_name="Operating mode",
        )
        .sensor(
            attribute_name=CustomEpaperCluster.AttributeDefs.refresh_count.name,
            cluster_id=CustomEpaperCluster.cluster_id,
            endpoint_id=1,
            state_class=SensorStateClass.TOTAL_INCREASING,
            entity_type=EntityType.DIAGNOSTIC,
            initially_disabled=True,
            reporting_config=ReportingConfig(
                min_interval=0,
                max_interval=3600,
                reportable_change=1,
            ),
            translation_key="refresh_count",
            fallback_name="Screen refresh count",
        )
        .sensor(
            attribute_name=CustomEpaperCluster.AttributeDefs.wakeup_count.name,
            cluster_id=CustomEpaperCluster.cluster_id,
            endpoint_id=1,
            state_class=SensorStateClass.TOTAL_INCREASING,
            entity_type=EntityType.DIAGNOSTIC,
            initially_disabled=True,
            reporting_config=ReportingConfig(
                min_interval=0,
                max_interval=3600,
                reportable_change=1,
            ),
            translation_key="wakeup_count",
            fallback_name="Wakeup count",
        )
        .sensor(
            attribute_name=CustomEpaperCluster.AttributeDefs.wakeup_duration.name,
            cluster_id=CustomEpaperCluster.cluster_id,
            endpoint_id=1,
            device_class=SensorDeviceClass.DURATION,
            state_class=SensorStateClass.TOTAL_INCREASING,
            unit=UnitOfTime.MILLISECONDS,
            entity_type=EntityType.DIAGNOSTIC,
            initially_disabled=True,
            reporting_config=ReportingConfig(
                min_interval=0,
                max_interval=3600,
                reportable_change=100,
            ),
            translation_key="wakeup_duration",
            fallback_name="Wakeup duration",
        )
        .sensor(
            attribute_name=CustomEpaperCluster.AttributeDefs.tx_duration.name,
            cluster_id=CustomEpaperCluster.cluster_id,
            endpoint_id=1,
            device_class=SensorDeviceClass.DURATION,
            state_class=SensorStateClass.TOTAL_INCREASING,
            unit=UnitOfTime.MILLISECONDS,
            entity_type=EntityType.DIAGNOSTIC,
            initially_disabled=True,
            reporting_config=ReportingConfig(
                min_interval=0,
                max_interval=3600,
                reportable_change=10,
            ),
            translation_key="tx_duration",
            fallback_name="TX duration",
        )
        .sensor(
            attribute_name=CustomEpaperCluster.AttributeDefs.rx_duration.name,
            cluster_id=CustomEpaperCluster.cluster_id,
            endpoint_id=1,
            device_class=SensorDeviceClass.DURATION,
            state_class=SensorStateClass.TOTAL_INCREASING,
            unit=UnitOfTime.MILLISECONDS,
            entity_type=EntityType.DIAGNOSTIC,
            initially_disabled=True,
            reporting_config=ReportingConfig(
                min_interval=0,
                max_interval=3600,
                reportable_change=10,
            ),
            translation_key="rx_duration",
            fallback_name="RX duration",
        )
        .add_to_registry()
    )


# Register Stellar-M3N@ / E31HA Quirk (2.13" BWR, slots 0..13)
_register_epaper_quirk("TLSR-M3Na-E31HA", ActiveSlotM3)

# Register Stellar-XL3N@ / E31PA Quirk (4.2" BWR, slots 0..7)
_register_epaper_quirk("TLSR-XL3Na-E31PA", ActiveSlotXL3)
