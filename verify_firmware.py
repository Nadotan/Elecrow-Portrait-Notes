
"""Verify the ESP32-P4 firmware layout before publishing it.

This does not test physical hardware, LCD, touch or UI.
"""

from pathlib import Path
import csv
import struct

ROOT = Path(__file__).resolve().parent

FLASH_SIZE = 16 * 1024 * 1024
APP_OFFSET = 0x10000
PARTITIONS_OFFSET = 0x8000


def check(condition, message):
    if not condition:
        raise SystemExit(
            f"FIRMWARE VALIDATION FAILED: {message}"
        )


def num(value):
    return int(value.strip(), 0)


def read_expected_layout():
    parts = []

    with (ROOT / "partitions.csv").open(
        newline="", encoding="utf-8"
    ) as f:

        for row in csv.reader(f):
            if not row:
                continue

            if row[0].strip().startswith("#"):
                continue

            check(
                len(row) >= 5,
                f"Invalid partition row: {row!r}"
            )

            name, typ, subtype, offset, size = [
                x.strip() for x in row[:5]
            ]

            parts.append(
                (name, typ, subtype, num(offset), num(size))
            )

    check(len(parts) >= 3, "Missing partition rows")

    for p in parts:
        alignment = (
            0x10000 if p[1] == "app" else 0x1000
        )

        check(
            p[3] % alignment == 0,
            f"Unaligned partition: {p[0]}"
        )

        check(
            p[3] >= 0x9000 and
            p[3] + p[4] <= FLASH_SIZE,
            f"Partition outside 16MB flash: {p[0]}"
        )

    ordered = sorted(parts, key=lambda x: x[3])

    for a, b in zip(ordered, ordered[1:]):
        check(
            a[3] + a[4] <= b[3],
            f"Overlapping partitions: {a[0]}, {b[0]}"
        )

    apps = [
        p for p in parts if p[1] == "app"
    ]

    check(
        len(apps) == 1,
        "Exactly one app partition is required"
    )

    check(
        apps[0][2] == "factory" and
        apps[0][3] == APP_OFFSET,
        "Factory app must start at 0x10000"
    )

    check(
        any(
            p[0] == "spiffs" and
            p[1] == "data" and
            p[3] >= APP_OFFSET + apps[0][4]
            for p in parts
        ),
        "Missing or overlapping SPIFFS partition"
    )

    return parts


def read_partition_bin(binary):
    parts = []

    for offset in range(0, 0xC00, 32):
        magic = struct.unpack_from(
            "<H", binary, offset
        )[0]

        if magic in (0xFFFF, 0xEBEB):
            break

        check(
            magic == 0x50AA,
            f"Invalid partition magic at {offset:#x}"
        )

        (
            _,
            typ,
            subtype,
            start,
            size,
            name,
            flags
        ) = struct.unpack_from(
            "<HBBII16sI", binary, offset
        )

        name = name.partition(b"\x00")[0].decode()

        parts.append(
            (name, typ, subtype, start, size)
        )

    return parts


def main():
    expected = read_expected_layout()

    firmware_dir = ROOT / "firmware"

    merged = (
        firmware_dir / "Sketch.ino.merged.bin"
    ).read_bytes()

    app = (
        firmware_dir / "Sketch.ino.bin"
    ).read_bytes()

    table = (
        firmware_dir / "Sketch.ino.partitions.bin"
    ).read_bytes()

    check(
        len(merged) == FLASH_SIZE,
        "Merged firmware must be exactly 16MB"
    )

    check(
        len(table) in (0xC00, 0x1000),
        "Unexpected partition table size"
    )

    check(
        merged[
            PARTITIONS_OFFSET:
            PARTITIONS_OFFSET + len(table)
        ] == table,
        "Partition table mismatch in merged image"
    )

    check(
        len(app) > 1024 and app[0] == 0xE9,
        "Compiled application has invalid ESP magic"
    )

    check(
        merged[
            APP_OFFSET:
            APP_OFFSET + len(app)
        ] == app,
        "App is not at 0x10000 in merged firmware"
    )

    check(
        merged[APP_OFFSET] == 0xE9,
        "Invalid app magic byte at 0x10000"
    )

    actual = read_partition_bin(table)

    check(
        len(actual) == len(expected),
        "Partition entry count mismatch"
    )

    actual_by_name = {
        p[0]: p for p in actual
    }

    type_codes = {
        "app": 0x00,
        "data": 0x01
    }

    subtype_codes = {
        "factory": 0x00,
        "nvs": 0x02,
        "phy": 0x01,
        "spiffs": 0x82
    }

    for name, typ, subtype, offset, size in expected:
        check(
            name in actual_by_name,
            f"Missing binary partition: {name}"
        )

        found = actual_by_name[name]

        check(
            found[1:] == (
                type_codes[typ],
                subtype_codes[subtype],
                offset,
                size
            ),
            f"Partition mismatch: {name}"
        )

    print("PASS: Flash size is 16MB")
    print("PASS: Application starts at 0x10000")
    print("PASS: Application image has valid magic")
    print("PASS: Partition table matches CSV")
    print("PASS: SPIFFS is within flash limits")

    print(
        "NOT TESTED: Hardware, display, touch or UI"
    )


if __name__ == "__main__":
    main()
