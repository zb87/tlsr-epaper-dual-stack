"""Home Assistant ZHA Quirks for TLSR8258 E-Paper Dual-Stack.

Provides custom device handlers and clusters for Hanshow Stellar-M3N@ / E31HA (2.13") and Stellar-XL3N@ / E31PA (4.2") BWR ESLs.
"""

from . import ts_epaper

__all__ = ["ts_epaper"]
