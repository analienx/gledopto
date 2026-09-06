#!/usr/bin/env python3
"""Offline Telink TLSR8258 inner-application finalizer and validator.

This tool operates on raw linker binaries only.  It does not create a Zigbee
OTA container, contact Zigbee2MQTT, or serve firmware to a device.

The public Telink 8258 startup emits the application preamble but the raw linked
binary does not contain the trailing Telink xcrc32.  Finalization therefore:

1. validates the expected 8258/GLEDOPTO-lineage preamble;
2. requires the raw linker-declared size to equal the input byte length;
3. pads the body to a 16-byte boundary with 0xFF if required;
4. patches the declared size to include the four-byte trailing CRC;
5. appends Telink reflected xcrc32 (init 0xFFFFFFFF, no final XOR);
6. re-validates the complete result.

A finalized inner image is still NOT authorization to package or deploy it.
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import struct
import sys

TELINK_8258_MAGIC = b"\x5d\x02"
TELINK_STARTUP_FLAG = 0x544C4E4B
TELINK_STARTUP_FLAG_BYTES = b"\x4b\x4e\x4c\x54"
TELINK_MARKER_OFFSET = 0x08
TELINK_DECLARED_SIZE_OFFSET = 0x18
TELINK_MIN_HEADER = 0x1C
TELINK_CRC_POLY = 0xEDB88320
DEFAULT_MANUFACTURER = 0x124F
DEFAULT_IMAGE_TYPE = 0x1416
DEFAULT_FILE_VERSION = 0x7F010001


class TelinkImageError(ValueError):
    pass


@dataclasses.dataclass(frozen=True)
class TelinkPreamble:
    file_version: int
    manufacturer_code: int
    image_type: int
    declared_size: int


def _u16le(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _u32le(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def telink_xcrc32(data: bytes | bytearray) -> int:
    """Telink reflected CRC32: init FFFFFFFF, no final XOR."""
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (TELINK_CRC_POLY if crc & 1 else 0)
    return crc & 0xFFFFFFFF


def parse_preamble(data: bytes | bytearray) -> TelinkPreamble:
    if len(data) < TELINK_MIN_HEADER:
        raise TelinkImageError("image is shorter than the Telink preamble")
    if bytes(data[6:8]) != TELINK_8258_MAGIC:
        raise TelinkImageError(
            f"unexpected MCU magic {bytes(data[6:8]).hex()}; expected {TELINK_8258_MAGIC.hex()}"
        )
    if bytes(data[TELINK_MARKER_OFFSET : TELINK_MARKER_OFFSET + 4]) != TELINK_STARTUP_FLAG_BYTES:
        raise TelinkImageError("Telink startup marker is not 4B 4E 4C 54")
    return TelinkPreamble(
        file_version=_u32le(data, 0x02),
        manufacturer_code=_u16le(data, 0x12),
        image_type=_u16le(data, 0x14),
        declared_size=_u32le(data, TELINK_DECLARED_SIZE_OFFSET),
    )


def validate_identity(
    preamble: TelinkPreamble,
    *,
    manufacturer_code: int = DEFAULT_MANUFACTURER,
    image_type: int = DEFAULT_IMAGE_TYPE,
    file_version: int | None = DEFAULT_FILE_VERSION,
) -> None:
    if preamble.manufacturer_code != manufacturer_code:
        raise TelinkImageError(
            f"manufacturer mismatch: 0x{preamble.manufacturer_code:04x} != 0x{manufacturer_code:04x}"
        )
    if preamble.image_type != image_type:
        raise TelinkImageError(
            f"image type mismatch: 0x{preamble.image_type:04x} != 0x{image_type:04x}"
        )
    if file_version is not None and preamble.file_version != file_version:
        raise TelinkImageError(
            f"file version mismatch: 0x{preamble.file_version:08x} != 0x{file_version:08x}"
        )


def is_valid_finalized_image(data: bytes | bytearray) -> bool:
    try:
        preamble = parse_preamble(data)
    except TelinkImageError:
        return False
    if len(data) < TELINK_MIN_HEADER + 4 or preamble.declared_size != len(data):
        return False
    expected = _u32le(data, len(data) - 4)
    return telink_xcrc32(data[:-4]) == expected


def validate_link_binary(
    data: bytes,
    *,
    manufacturer_code: int = DEFAULT_MANUFACTURER,
    image_type: int = DEFAULT_IMAGE_TYPE,
    file_version: int | None = DEFAULT_FILE_VERSION,
    max_final_size: int = 0x34000,
) -> TelinkPreamble:
    preamble = parse_preamble(data)
    validate_identity(
        preamble,
        manufacturer_code=manufacturer_code,
        image_type=image_type,
        file_version=file_version,
    )
    if preamble.declared_size != len(data):
        raise TelinkImageError(
            f"raw linker size mismatch: header declares {preamble.declared_size}, file has {len(data)}"
        )
    if is_valid_finalized_image(data):
        raise TelinkImageError("input already appears to be a finalized Telink image")
    padded = (len(data) + 15) & ~15
    if padded + 4 >= max_final_size:
        raise TelinkImageError(
            f"final image would reach/exceed 0x{max_final_size:x} app limit"
        )
    return preamble


def finalize_link_binary(
    data: bytes,
    *,
    manufacturer_code: int = DEFAULT_MANUFACTURER,
    image_type: int = DEFAULT_IMAGE_TYPE,
    file_version: int | None = DEFAULT_FILE_VERSION,
    max_final_size: int = 0x34000,
) -> bytes:
    validate_link_binary(
        data,
        manufacturer_code=manufacturer_code,
        image_type=image_type,
        file_version=file_version,
        max_final_size=max_final_size,
    )
    body = bytearray(data)
    padding = (-len(body)) % 16
    if padding:
        body.extend(b"\xff" * padding)
    final_size = len(body) + 4
    struct.pack_into("<I", body, TELINK_DECLARED_SIZE_OFFSET, final_size)
    crc = telink_xcrc32(body)
    result = bytes(body) + struct.pack("<I", crc)
    validate_finalized_image(
        result,
        manufacturer_code=manufacturer_code,
        image_type=image_type,
        file_version=file_version,
        max_final_size=max_final_size,
    )
    return result


def validate_finalized_image(
    data: bytes,
    *,
    manufacturer_code: int = DEFAULT_MANUFACTURER,
    image_type: int = DEFAULT_IMAGE_TYPE,
    file_version: int | None = DEFAULT_FILE_VERSION,
    max_final_size: int = 0x34000,
) -> TelinkPreamble:
    preamble = parse_preamble(data)
    validate_identity(
        preamble,
        manufacturer_code=manufacturer_code,
        image_type=image_type,
        file_version=file_version,
    )
    if preamble.declared_size != len(data):
        raise TelinkImageError(
            f"final size mismatch: header declares {preamble.declared_size}, file has {len(data)}"
        )
    if len(data) >= max_final_size:
        raise TelinkImageError(f"final image reaches/exceeds 0x{max_final_size:x} app limit")
    if len(data) < TELINK_MIN_HEADER + 4:
        raise TelinkImageError("final image is too short to contain CRC")
    expected = _u32le(data, len(data) - 4)
    actual = telink_xcrc32(data[:-4])
    if actual != expected:
        raise TelinkImageError(
            f"Telink xcrc32 mismatch: computed 0x{actual:08x}, tail 0x{expected:08x}"
        )
    return preamble


def _int_auto(value: str) -> int:
    return int(value, 0)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("check-link", "finalize", "check-final"))
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path, nargs="?")
    parser.add_argument("--manufacturer", type=_int_auto, default=DEFAULT_MANUFACTURER)
    parser.add_argument("--image-type", type=_int_auto, default=DEFAULT_IMAGE_TYPE)
    parser.add_argument("--file-version", type=_int_auto, default=DEFAULT_FILE_VERSION)
    parser.add_argument("--max-final-size", type=_int_auto, default=0x34000)
    args = parser.parse_args(argv)

    data = args.input.read_bytes()
    kwargs = dict(
        manufacturer_code=args.manufacturer,
        image_type=args.image_type,
        file_version=args.file_version,
        max_final_size=args.max_final_size,
    )
    try:
        if args.mode == "check-link":
            preamble = validate_link_binary(data, **kwargs)
            print(
                f"TELINK_LINK_IMAGE=PASS bytes={len(data)} fileVersion=0x{preamble.file_version:08x} "
                f"manufacturer=0x{preamble.manufacturer_code:04x} imageType=0x{preamble.image_type:04x}"
            )
        elif args.mode == "finalize":
            if args.output is None:
                parser.error("finalize requires OUTPUT")
            result = finalize_link_binary(data, **kwargs)
            args.output.write_bytes(result)
            crc = _u32le(result, len(result) - 4)
            print(f"TELINK_FINALIZE=PASS bytes={len(result)} xcrc32=0x{crc:08x}")
        else:
            preamble = validate_finalized_image(data, **kwargs)
            crc = _u32le(data, len(data) - 4)
            print(
                f"TELINK_FINAL_IMAGE=PASS bytes={len(data)} xcrc32=0x{crc:08x} "
                f"fileVersion=0x{preamble.file_version:08x}"
            )
    except TelinkImageError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
