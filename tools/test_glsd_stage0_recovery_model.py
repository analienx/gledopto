#!/usr/bin/env python3
"""Power-failure model for the GL-SD-301P Stage-0 rollback transaction.

This is deliberately independent of Telink hardware APIs.  It checks the safety
ordering we require from glsd_stage0_recovery.c: bank B stays bootable until a
complete stock A image is valid, and stock A cannot become bootable while its
first sector is only partially reconstructed.
"""

from __future__ import annotations

from dataclasses import dataclass, replace


@dataclass
class FlashState:
    a_sector: str = "stock-disabled"  # stock-disabled|erased|partial|staged|stock-enabled
    a_boot: str = "disabled"          # disabled|erased|staged|enabled
    b_boot: str = "enabled"           # enabled|disabled
    backup: bool = False
    journal: bool = False


def assert_boot_safe(s: FlashState) -> None:
    # The dangerous state is no viable recovery root.
    assert s.a_boot == "enabled" or s.b_boot == "enabled", s
    # A must never advertise a valid boot flag before the sector is complete.
    if s.a_boot == "enabled":
        assert s.a_sector == "stock-enabled", s
    # Once Stage-0 invalidates itself, verified stock must already be committed.
    if s.b_boot == "disabled":
        assert s.a_boot == "enabled" and s.a_sector == "stock-enabled", s


def transaction_steps(s: FlashState):
    """Yield state after each destructive/commit primitive of one recovery run."""
    s = replace(s)

    if not (s.backup and s.journal):
        # A is not touched until both persistent recovery records exist.
        assert s.a_sector in {"stock-disabled", "stock-enabled"}
        s.backup = False             # erase backup sector
        yield "erase-backup", replace(s)
        s.backup = True              # verified 4-KiB copy
        yield "write-backup", replace(s)
        s.journal = False            # erase journal sector
        yield "erase-journal", replace(s)
        s.journal = True             # verified metadata + backup CRC
        yield "write-journal", replace(s)

    # If A is already valid from an earlier interrupted run, no second erase.
    if s.a_boot != "enabled":
        assert s.backup and s.journal
        s.a_sector = "erased"
        s.a_boot = "erased"
        yield "erase-a-sector0", replace(s)

        # Model every 256-byte page boundary. The boot byte remains 0xFF for the
        # entire reconstruction, therefore A is never bootable in this window.
        for page in range(16):
            s.a_sector = "partial" if page < 15 else "staged"
            s.a_boot = "staged" if page == 15 else "erased"
            yield f"write-a-page-{page:02d}", replace(s)

        # Whole-image CRC has been checked with boot byte normalized; now and
        # only now commit 0xFF -> 0x4B.
        s.a_sector = "stock-enabled"
        s.a_boot = "enabled"
        yield "commit-a-boot-flag", replace(s)

    # Final one-way commit: from now on any reset can only return to stock A.
    s.b_boot = "disabled"
    yield "invalidate-stage0-b", replace(s)


def recover_to_completion(s: FlashState) -> FlashState:
    """Model reboot into whichever safe image can continue the transaction."""
    assert_boot_safe(s)

    # If stock A is already the sole bootable image, recovery is complete.
    if s.a_boot == "enabled" and s.b_boot == "disabled":
        return s

    # If both are valid, either boot choice is safe. For the stricter model,
    # assume Stage-0 gets control and completes its final self-invalidation.
    # If stock gets control instead, the real device has already returned safe.
    if s.b_boot != "enabled":
        return s

    # A can be partially destroyed only after journal+backup are durable.
    if s.a_sector in {"erased", "partial", "staged"}:
        assert s.backup and s.journal

    current = s
    for _, current in transaction_steps(current):
        assert_boot_safe(current)
    return current


def main() -> int:
    initial = FlashState()
    assert_boot_safe(initial)

    baseline = list(transaction_steps(initial))
    assert baseline

    # Cut power after every primitive, including every page of the stock-sector
    # rewrite, then require the next run to converge to verified stock-only boot.
    for name, interrupted in baseline:
        assert_boot_safe(interrupted)
        final = recover_to_completion(interrupted)
        assert final.a_boot == "enabled", (name, final)
        assert final.a_sector == "stock-enabled", (name, final)
        assert final.b_boot == "disabled", (name, final)
        assert_boot_safe(final)

    # Explicitly prove the key two-phase invariant.
    partial_states = [s for n, s in baseline if n.startswith("write-a-page-")]
    assert partial_states
    for s in partial_states[:-1]:
        assert s.a_boot != "enabled" and s.b_boot == "enabled"
    staged = partial_states[-1]
    assert staged.a_sector == "staged" and staged.a_boot == "staged"
    assert staged.b_boot == "enabled"

    print(f"GLSD_STAGE0_POWERFAIL_MODEL=PASS cut_points={len(baseline)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
