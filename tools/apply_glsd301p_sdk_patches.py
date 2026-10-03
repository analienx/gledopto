#!/usr/bin/env python3
"""Apply pinned narrow GL-SD-301P patches to a hosted Telink SDK copy.

Each patch pins the exact original file hash, applies anchor-based edits
(each anchor must occur exactly once), and verifies the exact patched hash.
Re-running over an already-patched copy verifies the patched hash instead of
editing again, so repeated builds in one workspace stay deterministic.

Provenance (original/patched hashes, anchors) is emitted as JSON for the
build manifest. Only the listed files are touched.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

PATCHES = [
    {
        "id": "P1-timebase-hook",
        "relpath": "proj/os/ev_timer.c",
        "original_sha256": (
            "56bba6c25b1ed157c89c644d9d8793ccc9feb0fa37926cbd0824c23757bc9a67"
        ),
        "patched_sha256": (
            "239f1a38ac2cb1ab7a9241f329c492ff914c2245ca92159f0c18869c28fa01eb"
        ),
        "marker": "glsd301p_timebase_advance",
        "edits": [
            {
                "anchor": '#include "ev_timer.h"\n',
                "replacement": (
                    '#include "ev_timer.h"\n'
                    '#include "glsd301p_timebase.h"\n'
                ),
            },
            {
                "anchor": "    ev_rtc_update(updateTime);\n",
                "replacement": (
                    "    ev_rtc_update(updateTime);\n"
                    "    glsd301p_timebase_advance(updateTime);\n"
                ),
            },
        ],
    },
]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def apply_patch(sdk_root: Path, patch: dict, derive: bool = False) -> dict:
    relpath = patch["relpath"]
    path = sdk_root / relpath
    if not path.is_file():
        raise SystemExit(f"ERROR: SDK file missing for {patch['id']}: {relpath}")
    raw = path.read_bytes()
    original_hash = sha256_bytes(raw)
    marker = patch["marker"].encode("ascii")

    if marker in raw:
        if original_hash != patch["patched_sha256"]:
            raise SystemExit(
                f"ERROR: {patch['id']} marker present but {relpath} hash "
                f"{original_hash} != pinned patched {patch['patched_sha256']}"
            )
        return {
            "id": patch["id"],
            "file": relpath,
            "original_sha256": patch["original_sha256"],
            "patched_sha256": patch["patched_sha256"],
            "applied": False,
            "already_patched": True,
        }

    if original_hash != patch["original_sha256"]:
        raise SystemExit(
            f"ERROR: {patch['id']} {relpath} hash {original_hash} != pinned "
            f"original {patch['original_sha256']}"
        )

    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise SystemExit(f"ERROR: {patch['id']} {relpath} not UTF-8: {exc}")

    crlf = text.count("\r\n")
    lf = text.count("\n") - crlf
    style = "crlf" if crlf > 0 and lf == 0 else ("lf" if crlf == 0 else "mixed")
    if style == "mixed":
        raise SystemExit(f"ERROR: {patch['id']} {relpath} mixed newlines")
    work = text.replace("\r\n", "\n")
    newline = "\r\n" if style == "crlf" else "\n"

    anchors = []
    for edit in patch["edits"]:
        anchor = edit["anchor"]
        occurrences = work.count(anchor)
        if occurrences != 1:
            raise SystemExit(
                f"ERROR: {patch['id']} anchor {anchor!r} occurs "
                f"{occurrences}x in {relpath}, need exactly 1"
            )
        line_no = work[: work.find(anchor)].count("\n") + 1
        anchors.append({"anchor": anchor, "line": line_no})
        work = work.replace(anchor, edit["replacement"])

    # Single newline restoration at the end; replacements stay LF until here.
    patched_text = work.replace("\n", newline) if style == "crlf" else work
    patched_raw = patched_text.encode("utf-8")
    patched_hash = sha256_bytes(patched_raw)
    if derive:
        path.write_bytes(patched_raw)
        print(f"DERIVED {patch['id']} {relpath} sha256={patched_hash}")
        return {
            "id": patch["id"],
            "file": relpath,
            "original_sha256": patch["original_sha256"],
            "patched_sha256": patched_hash,
            "applied": True,
            "derived": True,
        }
    if patched_hash != patch["patched_sha256"]:
        raise SystemExit(
            f"ERROR: {patch['id']} {relpath} patched hash {patched_hash} != "
            f"pinned {patch['patched_sha256']}"
        )
    path.write_bytes(patched_raw)
    return {
        "id": patch["id"],
        "file": relpath,
        "original_sha256": patch["original_sha256"],
        "patched_sha256": patch["patched_sha256"],
        "applied": True,
        "already_patched": False,
        "newline_style": style,
        "anchors": anchors,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--provenance-out", type=Path)
    parser.add_argument("--derive", action="store_true")
    args = parser.parse_args(argv)

    report = {
        "schema": 1,
        "sdk_root": str(args.sdk_root),
        "patches": [apply_patch(args.sdk_root, p, derive=args.derive) for p in PATCHES],
    }
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.provenance_out:
        args.provenance_out.write_text(text, encoding="utf-8")
    print(text, end="")
    print("GLSD301P_SDK_PATCHES_APPLIED=" + str(len(report["patches"])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
