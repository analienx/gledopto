#!/usr/bin/env python3
"""Classify TLSR8258 MMIO references in a raw TC32 disassembly.

The TC32 compiler routinely materializes 0x800xxx peripheral addresses through
PC-relative literal loads.  We resolve the literal address printed by objdump,
read the 32-bit value from the raw image, classify it against the public TLSR8258
register map, and emit both a compact summary and contextual evidence.

This is an offline reverse-engineering aid.  It does not generate flashable
firmware and it deliberately makes no pin/timing claims on raw counts alone.
"""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import re
import struct
from dataclasses import dataclass


# Public Telink 8258 digital register map (REG_BASE_ADDR = 0x800000).
# Keep ranges narrow where possible so unrelated RF/USB references do not
# inflate the hardware-control evidence.
REGIONS = {
    "clock_reset": (0x800060, 0x80006F),
    "uart": (0x800090, 0x80009F),
    "gpio": (0x800580, 0x8005CF),
    "timer": (0x800620, 0x80063B),
    "irq": (0x800640, 0x80064F),
    "system_timer": (0x800740, 0x80074F),
    "pwm": (0x800780, 0x8007DF),
    "uart_dma": (0x800C00, 0x800C07),
    "pwm_dma": (0x800C18, 0x800C1B),
}

# Exact registers that are especially diagnostic for a dimmer control path.
EXACT = {
    0x800060: "reg_rst0 (UART reset bit2 / PWM reset bit4)",
    0x800063: "reg_clk_en0 (UART clock bit2 / PWM clock bit4)",
    0x800090: "reg_uart_clk_div",
    0x800091: "reg_uart_ctrl0",
    0x800092: "reg_uart_ctrl1",
    0x800093: "reg_uart_ctrl2",
    0x800094: "reg_uart_buf0",
    0x800095: "reg_uart_buf1",
    0x800096: "reg_uart_buf2",
    0x800097: "reg_uart_buf3",
    0x800098: "reg_uart_ctrl2/3 word",
    0x800099: "reg_uart_ctrl3",
    0x80009A: "reg_uart_rx_timeout0",
    0x80009B: "reg_uart_rx_timeout1",
    0x80009C: "reg_uart_buf_cnt",
    0x80009D: "reg_uart_status0",
    0x80009E: "reg_uart_status1",
    0x80009F: "reg_uart_state",
    0x800580: "GPIO PA block",
    0x800588: "GPIO PB block",
    0x800590: "GPIO PC block",
    0x800598: "GPIO PD block",
    0x8005A0: "GPIO PE block",
    0x8005B5: "reg_gpio_wakeup_irq",
    0x8005B8: "GPIO RISC0 IRQ enables",
    0x8005C0: "GPIO RISC1 IRQ enables",
    0x8005C8: "GPIO RISC2 IRQ enables",
    0x800620: "reg_tmr_ctrl",
    0x800623: "reg_tmr_sta",
    0x800624: "reg_tmr0_capt",
    0x800628: "reg_tmr1_capt",
    0x80062C: "reg_tmr2_capt",
    0x800630: "reg_tmr0_tick",
    0x800634: "reg_tmr1_tick",
    0x800638: "reg_tmr2_tick",
    0x800640: "reg_irq_mask",
    0x800648: "reg_irq_src",
    0x800740: "reg_system_tick",
    0x800744: "reg_system_tick_irq",
    0x800748: "reg_system_wakeup_tick",
    0x80074C: "reg_system_tick_mode",
    0x80074F: "reg_system_tick_ctrl",
    0x800780: "reg_pwm_enable",
    0x800781: "reg_pwm0_enable",
    0x800782: "reg_pwm_clk",
    0x800783: "reg_pwm0_mode",
    0x800784: "reg_pwm_invert",
    0x800785: "reg_pwm_n_invert",
    0x800786: "reg_pwm_pol",
    0x800794: "PWM0 cycle/cmp/max",
    0x800798: "PWM1 cycle/cmp/max",
    0x80079C: "PWM2 cycle/cmp/max",
    0x8007A0: "PWM3 cycle/cmp/max",
    0x8007A4: "PWM4 cycle/cmp/max",
    0x8007A8: "PWM5 cycle/cmp/max",
    0x8007AC: "reg_pwm0_pulse_num",
    0x8007B0: "reg_pwm_irq_mask",
    0x8007B1: "reg_pwm_irq_sta",
    0x8007C4: "reg_pwm_tcmp0_shadow",
    0x8007C6: "reg_pwm_tmax0_shadow",
    0x800C00: "UART RX DMA block",
    0x800C04: "UART TX DMA block",
    0x800C18: "PWM DMA block",
}

LINE_RE = re.compile(r"^\s*([0-9a-fA-F]+):")
LITERAL_TARGET_RE = re.compile(r"\;\s*\(0x([0-9a-fA-F]+)\)")


@dataclass(frozen=True)
class Xref:
    insn: int
    literal: int
    value: int
    category: str
    name: str
    line_index: int


def classify(value: int) -> str | None:
    for name, (lo, hi) in REGIONS.items():
        if lo <= value <= hi:
            return name
    return None


def reg_name(value: int, category: str) -> str:
    if value in EXACT:
        return EXACT[value]
    # Give useful derived labels for repetitive register banks.
    if category == "gpio" and 0x800580 <= value <= 0x8005A7:
        port = "ABCDE"[(value - 0x800580) // 8]
        offs = (value - 0x800580) % 8
        suffix = ["in", "ie", "oen", "out", "pol", "ds", "func", "irq_en"][offs]
        return f"GPIO P{port} {suffix}"
    if category == "pwm" and 0x800794 <= value <= 0x8007AB:
        ch = (value - 0x800794) // 4
        rem = (value - 0x800794) % 4
        return f"PWM{ch} cycle register +{rem}"
    return f"{category} register 0x{value:06x}"


def resolve_xrefs(raw: bytes, lines: list[str]) -> list[Xref]:
    out: list[Xref] = []
    for idx, line in enumerate(lines):
        if "tloadr" not in line:
            continue
        im = LINE_RE.search(line)
        lm = LITERAL_TARGET_RE.search(line)
        if not im or not lm:
            continue
        insn = int(im.group(1), 16)
        literal = int(lm.group(1), 16)
        if literal < 0 or literal + 4 > len(raw):
            continue
        value = struct.unpack_from("<I", raw, literal)[0]
        category = classify(value)
        if category:
            out.append(Xref(insn, literal, value, category, reg_name(value, category), idx))
    return out


def raw_literal_candidates(raw: bytes) -> list[tuple[int, int, str, str]]:
    """Secondary evidence only: aligned MMIO-looking constants in the image."""
    found = []
    for off in range(0, len(raw) - 3, 2):
        value = struct.unpack_from("<I", raw, off)[0]
        category = classify(value)
        if category:
            found.append((off, value, category, reg_name(value, category)))
    return found


def context(lines: list[str], x: Xref, radius: int = 6) -> str:
    lo = max(0, x.line_index - radius)
    hi = min(len(lines), x.line_index + radius + 1)
    body = "".join(lines[lo:hi]).rstrip()
    return (
        f"\n### {x.category.upper()} xref @0x{x.insn:06x} -> literal "
        f"0x{x.literal:06x} = 0x{x.value:08x} ({x.name})\n```text\n{body}\n```\n"
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("raw", type=pathlib.Path)
    ap.add_argument("disassembly", type=pathlib.Path)
    ap.add_argument("--out-dir", type=pathlib.Path, required=True)
    args = ap.parse_args()

    raw = args.raw.read_bytes()
    lines = args.disassembly.read_text(errors="replace").splitlines(keepends=True)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    xrefs = resolve_xrefs(raw, lines)
    literals = raw_literal_candidates(raw)
    by_cat: dict[str, list[Xref]] = collections.defaultdict(list)
    for x in xrefs:
        by_cat[x.category].append(x)

    unique_values = {cat: sorted({x.value for x in xs}) for cat, xs in by_cat.items()}
    unique_insns = {cat: sorted({x.insn for x in xs}) for cat, xs in by_cat.items()}

    # Evidence-oriented architecture score.  This is deliberately descriptive,
    # not an automatic proof: direct dimming requires PWM/timer/GPIO co-location,
    # while a secondary MCU requires a coherent UART path, not merely UART use.
    direct_components = sum(bool(by_cat.get(c)) for c in ("pwm", "timer", "gpio"))
    uart_components = sum(bool(by_cat.get(c)) for c in ("uart", "uart_dma"))
    if direct_components == 3 and len(by_cat["pwm"]) >= 2:
        leaning = "DIRECT_TLSR_POWER_STAGE_CANDIDATE"
    elif uart_components == 2 and not by_cat.get("pwm"):
        leaning = "SECONDARY_MCU_UART_CANDIDATE"
    else:
        leaning = "INCONCLUSIVE_NEEDS_CALLGRAPH"

    summary = {
        "raw_bytes": len(raw),
        "xref_count": len(xrefs),
        "counts": {k: len(by_cat.get(k, [])) for k in REGIONS},
        "unique_mmio_values": {k: [f"0x{v:08x}" for v in unique_values.get(k, [])] for k in REGIONS},
        "unique_xref_instructions": {k: [f"0x{v:06x}" for v in unique_insns.get(k, [])] for k in REGIONS},
        "raw_literal_candidate_count": len(literals),
        "architecture_leaning": leaning,
        "warning": "Counts are triage evidence only; prove the dimmer path by control-flow/data-flow context before implementing hardware writes.",
    }
    (args.out_dir / "mmio-summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    with (args.out_dir / "mmio-xrefs.tsv").open("w") as f:
        f.write("category\tinsn\tliteral\tvalue\tname\n")
        for x in sorted(xrefs, key=lambda q: (q.category, q.insn)):
            f.write(f"{x.category}\t0x{x.insn:06x}\t0x{x.literal:06x}\t0x{x.value:08x}\t{x.name}\n")

    # Context is intentionally capped per category to keep Actions logs/artifacts reviewable.
    with (args.out_dir / "mmio-context.md").open("w") as f:
        f.write("# GL-SD-301P vendor firmware MMIO xref contexts\n")
        f.write("\nGenerated from resolved PC-relative TC32 literal loads.\n")
        for cat in ("pwm", "timer", "irq", "gpio", "uart", "uart_dma", "pwm_dma", "clock_reset", "system_timer"):
            xs = by_cat.get(cat, [])
            if not xs:
                continue
            f.write(f"\n## {cat} ({len(xs)} xrefs)\n")
            # Deduplicate identical instruction addresses and cap at 40 contexts.
            seen = set()
            emitted = 0
            for x in xs:
                if x.insn in seen:
                    continue
                seen.add(x.insn)
                f.write(context(lines, x))
                emitted += 1
                if emitted >= 40:
                    f.write("\n_Context output capped at 40 unique xrefs for this category._\n")
                    break

    with (args.out_dir / "raw-mmio-literals.tsv").open("w") as f:
        f.write("offset\tvalue\tcategory\tname\n")
        for off, value, cat, name in literals:
            f.write(f"0x{off:06x}\t0x{value:08x}\t{cat}\t{name}\n")

    print(json.dumps(summary, indent=2))
    print("MMIO_XREF_ANALYSIS=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
