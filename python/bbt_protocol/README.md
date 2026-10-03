BBT Protocol Python Test Module
===============================

This package provides a minimal, testable Python implementation of the BBT
protocol framing and two transport adapters (UART and BLE). Use it to build
integration tests or to exercise the embedded C BBT implementation over real
or simulated transports.

Prerequisites
-------------
- Python 3.8+
- Optional: bleak (BLE support) `pip install bleak`
- Optional: pyserial or pyserial-asyncio (UART support) `pip install pyserial pyserial-asyncio`

Structure
---------
- frame.py: Packet builders and parsers (mirrors C bbt_packet helpers)
- transports/: BLE and UART transport adapters
- protocol.py: High-level harness that composes transports and framing
- examples/: basic usage examples for UART and BLE
- tests/: unit tests for framing

Note: This module is intended for testing and integration. It intentionally
mirrors the packet layout used by the C implementation to ensure binary
compatibility.
