#!/usr/bin/env python3
"""GL-SD-301P vendor-stock-only TX -> SWire writer for CH340G.

This is a deliberately feedback-free port of pvvx's USB-COM TX-only
flash writer v0.5 (pvvx/pvvx.github.io/ATC_MiThermometer/USBCOMFlashTx.html).

Commands:
  plan       Prepare exact vendor stock app and print an offline plan.
  ports      Display available serial ports only.
  stage      Write the staged stock app at bank A with inactive boot marker.
  activate   In a DIFFERENT, deliberate invocation, send the single 0x4B
             marker byte at 0x00008 WITHOUT sector erase.

No full-chip erase, arbitrary image, address, OTA, flash read or automatic
stage->activate operation is exposed. No response is received from the MCU.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import time

from glsd301p_stock_swire_recovery import (
    BANK_A_END, BANK_A_START, DEFAULT_OUTPUT, FLASH_SECTOR_SIZE,
    RecoveryError, prepare, print_serial_ports, sha256,
)

# Pinned TX-only reference: pvvx.github.io ATC_MiThermometer USBCOMFlashTx.html v0.5
REFERENCE_URL = (
    "https://github.com/pvvx/pvvx.github.io/blob/master/"
    "ATC_MiThermometer/USBCOMFlashTx.html"
)

UART_BAUD = 460800  # v0.5 TX-only default; SWS divider below is tuned for this
SWS_DIVIDER = 55
PAGE_BYTES = 256
ERASE_SETTLE_SECONDS = 0.300  # upstream v0.5 conservative delay (no WIP polling)
WRITE_SETTLE_SECONDS = 0.010  # upstream v0.5 conservative delay (no WIP polling)
STATUS_SETTLE_SECONDS = 0.003
ACTIVATION_SECONDS = 3.0  # upstream v0.5 activation loop; no feedback!
BOOT_MARKER_OFFSET = 8
BOOT_MARKER = b"KNLT"
STAGE_MARKER = bytes.fromhex("ff4e4c54")
ACTIVATE_BYTE = b"K"
STAGE_CONFIRM = "TX_ONLY_STAGE"
ACTIVATE_CONFIRM = "TX_ONLY_ACTIVATE_UNVERIFIED"


def swire_write_frame(address: int, data: bytes) -> bytes:
    """Reproduce upstream v0.5 sws_wr_addr() 10-UART-byte encoding per word."""
    if not 0 <= address <= 0xFFFFFF or not data:
        raise RecoveryError("SWire encoding address or payload invalid")
    message = bytes((0x5A, (address >> 16) & 0xFF,
                     (address >> 8) & 0xFF, address & 0xFF, 0)) + bytes(data)
    encoded = bytearray()
    for i, val in enumerate(message):
        encoded.append(0x80 if i == 0 else 0xFE)
        for bit in range(7, -1, -1):
            encoded.append(0x80 if val & (1 << bit) else 0xFE)
        encoded.append(0xFE)
    # Upstream stop word is deliberately 0x80 * 9, 0xfe.
    encoded.extend(b"\x80" * 9 + b"\xFE")
    assert len(encoded) == 10 * (len(data) + 6)
    return bytes(encoded)


class TxOnlyWriter:
    """Deterministic send-only SWire commands; NEVER reads from the chip."""

    def __init__(self, serial_port, *, sleeper=time.sleep, clock=time.monotonic):
        self.serial_port = serial_port
        self.sleep = sleeper
        self.clock = clock
        self.bytes_sent = 0
        self.frame_count = 0
        self.sectors_erased = []
        self.pages_programmed = []
        self.activation_writes = []

    def raw(self, buf: bytes) -> None:
        # pvvx breaks high-speed transfers to 60-byte USB packets only above
        # 460800; at our pinned 460800 this remains a single serial.write.
        sent = self.serial_port.write(buf)
        if sent != len(buf):
            raise RecoveryError(f"short serial transmission {sent}/{len(buf)}")
        self.serial_port.flush()
        self.bytes_sent += sent
        self.frame_count += 1

    def sws(self, addr: int, payload: bytes) -> None:
        self.raw(swire_write_frame(addr, payload))

    def command(self, spi_cmd: int) -> None:
        self.sws(0x0D, b"\x00")             # chip select LOW
        self.sws(0x0C, bytes((spi_cmd, 1))) # command then chip select HIGH

    def wake(self) -> None:
        self.command(0xAB)

    def write_enable(self) -> None:
        self.command(0x06)

    def status_clear(self) -> None:
        # Exactly as upstream v0.5: enable SPI flash then send status=0.
        # This is not a read or verification, and can change protection bits.
        self.write_enable()
        self.sws(0x0D, b"\x00")
        self.sws(0x0C, b"\x01")
        self.sws(0x0C, b"\x00\x01")
        self.sleep(STATUS_SETTLE_SECONDS)

    def soft_reset(self) -> None:
        self.sws(0x006F, b"\x20")

    def activate_swire(self, seconds: float = ACTIVATION_SECONDS) -> None:
        if not (0 < seconds <= 16):
            raise RecoveryError("unsafe SWire activation duration")
        self.soft_reset()
        stop_cpu = swire_write_frame(0x0602, b"\x05")
        start = self.clock()
        while self.clock() - start < seconds:
            self.raw(stop_cpu)
        self.sws(0x00B2, bytes((SWS_DIVIDER,)))
        self.raw(stop_cpu)
        self.wake()

    def fifo(self, register: int, payload: bytes) -> None:
        self.sws(0x00B3, b"\x80")
        self.sws(register, payload)
        self.sws(0x00B3, b"\x00")

    def erase_sector(self, address: int) -> None:
        if address % FLASH_SECTOR_SIZE != 0 or not 0 <= address < BANK_A_END:
            raise RecoveryError("sector erase would exceed stock application bank A")
        self.write_enable()
        self.sws(0x0D, b"\x00")
        self.sws(0x0C, b"\x20")
        self.sws(0x0C, bytes(((address >> 16) & 0xFF,)))
        self.sws(0x0C, bytes(((address >> 8) & 0xFF,)))
        self.sws(0x0C, bytes((address & 0xFF, 1)))
        self.sectors_erased.append(address)
        self.sleep(ERASE_SETTLE_SECONDS)

    def page(self, address: int, data: bytes, *, activation: bool = False) -> None:
        if not data or len(data) > PAGE_BYTES or (address // 256 != (address + len(data) - 1) // 256):
            raise RecoveryError("invalid page span")
        if activation:
            if address != BOOT_MARKER_OFFSET or data != ACTIVATE_BYTE:
                raise RecoveryError("activation can only program 0x4B at 0x00008")
            self.activation_writes.append(address)
        elif not 0 <= address < address + len(data) <= BANK_A_END:
            raise RecoveryError("page outside bank A")
        self.write_enable()
        self.sws(0x0D, b"\x00")
        spi_data = bytes((0x02, (address >> 16) & 0xFF,
                          (address >> 8) & 0xFF, address & 0xFF)) + data
        self.fifo(0x0C, spi_data)
        self.sws(0x0D, b"\x01")
        self.pages_programmed.append((address, len(data)))
        self.sleep(WRITE_SETTLE_SECONDS)

    def stage(self, staged: bytes) -> None:
        if (staged[8:12] != STAGE_MARKER or len(staged) == 0
                or len(staged) >= BANK_A_END
                or (len(staged) + 4095) // 4096 * 4096 > BANK_A_END):
            raise RecoveryError("invalid staged stock-only image")
        self.activate_swire()
        self.status_clear()
        self.status_clear()
        for addr in range(0, len(staged), PAGE_BYTES):
            if addr % FLASH_SECTOR_SIZE == 0:
                self.erase_sector(addr)
            self.page(addr, staged[addr:addr + PAGE_BYTES])
        # No forced restart after stage: the device must remain with bank-A
        # startup marker inactive until a separate deliberate activation.

    def activate_stock(self) -> None:
        self.activate_swire()
        self.status_clear()
        self.status_clear()
        # Important: no SectorErase here. 0xFF -> 0x4B is NOR flash 1->0 only.
        self.page(BOOT_MARKER_OFFSET, ACTIVATE_BYTE, activation=True)
        self.soft_reset()


def receipt_path(output: Path) -> Path:
    return output.with_name(output.stem + "-txonly-stage-sent.json")


def confirm(action: str, provided: str | None) -> None:
    token = STAGE_CONFIRM if action == "stage" else ACTIVATE_CONFIRM
    if provided == token:
        return
    if not sys.stdin.isatty():
        raise RecoveryError(f"{action} requires --confirm {token} in noninteractive mode")
    if input(f"Type {token} to authorize TX-only {action}: ").strip() != token:
        raise RecoveryError("operation cancelled")


def prepare_payload(output: Path) -> tuple[dict, bytes, bytes]:
    report = prepare(output=output)
    stage_path = output.with_name(output.stem + "-stage.bin")
    activate_path = output.with_name(output.stem + "-activate.bin")
    staged = stage_path.read_bytes()
    activation = activate_path.read_bytes()
    if (sha256(staged) != report["stagedSha256"]
            or sha256(output.read_bytes()) != report["applicationSha256"]
            or len(staged) != report["applicationBytes"]
            or staged[8:12] != STAGE_MARKER
            or activation != ACTIVATE_BYTE):
        raise RecoveryError("prepared images differ from pinned GLEDOPTO stock values")
    return report, staged, activation


def open_port(port: str):
    if not re.fullmatch(r"(?:COM\d+|/dev/[A-Za-z0-9_./-]+)", port, re.IGNORECASE):
        raise RecoveryError("invalid COM or POSIX serial device")
    try:
        import serial
    except ImportError as exc:
        raise RecoveryError("pyserial required: python -m pip install pyserial") from exc
    # No RTS/3.3V control. The photographed CH340G has no proven RTS pin.
    # Some drivers glitch DTR/RTS on open: operator must check hardware.
    com = serial.Serial(
        port=port, baudrate=UART_BAUD, timeout=0.5, write_timeout=30,
        rtscts=False, dsrdtr=False, xonxoff=False,
    )
    com.rts = False
    com.dtr = False
    return com


def run_action(action: str, port: str, output: Path,
               provided: str | None) -> dict:
    report, staged, activation = prepare_payload(output)
    previous = receipt_path(output)
    if action == "stage" and previous.exists():
        raise RecoveryError(
            "stage-sent receipt exists; refusing automatic repeat/erase. "
            "Review the result and move/rename the receipt only if a fresh "
            "stage erase is deliberately required."
        )
    if action == "activate":
        if not previous.exists():
            raise RecoveryError("no prior stage-sent receipt; activation forbidden")
        receipt = json.loads(previous.read_text(encoding="utf-8"))
        if (receipt.get("phase") != "STAGE_SENT_UNVERIFIED"
                or receipt.get("device") != report["device"]
                or receipt.get("stageSha256") != sha256(staged)
                or receipt.get("port") != port
                or receipt.get("activationSent") is True):
            raise RecoveryError("stage receipt incompatible or already activated")
    print(
        "DANGER: GL-SD-301P is a mains dimmer. DISCONNECT 230 V and ensure "
        "electrical isolation, proven GND/3.3V/SWS pad identity, TXD=3.3V, "
        "current-limited isolated supply, and mechanically stable contacts.",
        flush=True,
    )
    print(
        f"TX-ONLY {action.upper()}: no RX and NO DEVICE ACKNOWLEDGEMENT. "
        "COM write success does not prove that flash programmed correctly.",
        flush=True,
    )
    confirm(action, provided)
    with open_port(port) as serial_port:
        writer = TxOnlyWriter(serial_port)
        if action == "stage":
            writer.stage(staged)
        else:
            writer.activate_stock()
    snapshot = {
        "device": report["device"], "port": port,
        "stageSha256": sha256(staged),
        "sourceSha256": report["sourceSha256"],
        "phase": "STAGE_SENT_UNVERIFIED" if action == "stage" else "ACTIVATION_SENT_UNVERIFIED",
        "activationSent": action == "activate",
        "feedbackAvailable": False, "readbackVerified": False,
        "transmittedUartBytes": writer.bytes_sent,
        "sentSwireFrames": writer.frame_count,
        "sectorsErasedByPlan": writer.sectors_erased,
        "pagesSent": len(writer.pages_programmed),
    }
    # Stage receipt is only a transmission record, NEVER an attestation
    # that the target accepted the bytes.
    previous.write_text(json.dumps(snapshot, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
    print(f"TX_ONLY_{snapshot['phase']} — NOT FLASH VERIFIED", flush=True)
    return snapshot


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    commands.add_parser("ports", help="list available COM ports (read-only)")
    plan = commands.add_parser("plan", help="generate stock stage/activation BINs offline")
    plan.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    for name in ("stage", "activate"):
        p = commands.add_parser(name, help="send ONLY this TX-only phase")
        p.add_argument("--port", required=True)
        p.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
        p.add_argument("--confirm")
    args = parser.parse_args(argv)
    try:
        if args.action == "ports":
            print_serial_ports()
        elif args.action == "plan":
            report, staged, _ = prepare_payload(args.output)
            print(json.dumps({
                "device": report["device"], "stageSha256": sha256(staged),
                "sourceSha256": report["sourceSha256"],
                "stageAddress": "0x00000", "activateAddress": "0x00008",
                "stageMarker": "ff4e4c54", "activateByte": "4b",
                "safeSectorEraseEndExclusive": report["eraseEndExclusive"],
                "transport": "TX-only; UNSUPPORTED LIVE HARDWARE",
            }, indent=2))
        else:
            run_action(args.action, args.port, args.output, args.confirm)
    except (RecoveryError, OSError, ValueError, ImportError) as exc:
        print(f"TX_ONLY_BLOCKED: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
