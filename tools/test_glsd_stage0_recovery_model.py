#!/usr/bin/env python3
"""Power-failure model for the bank-neutral GL-SD-301P Stage-0 transaction.

The model injects power loss after every destructive/commit primitive for BOTH
possible OTA histories:
  * Stage-0 in bank B, preserved stock in bank A
  * Stage-0 in bank A, preserved stock in bank B

The TLSR8258 boot convention is modeled conservatively as bank-A-first when both
flags are valid.  A cut may therefore return directly to stock before Stage-0
gets a chance to clear its own flag; that is still a safe outcome.
"""

from __future__ import annotations

from dataclasses import dataclass, replace


@dataclass
class FlashState:
    self_bank: str
    stock_sector: str = "stock-disabled"  # stock-disabled|erased|partial|staged|stock-enabled
    stock_boot: str = "disabled"          # disabled|erased|staged|enabled
    self_boot: str = "enabled"            # enabled|disabled
    backup: bool = False
    journal: bool = False

    @property
    def stock_bank(self) -> str:
        return "B" if self.self_bank == "A" else "A"


def bank_boot(s: FlashState, bank: str) -> str:
    return s.self_boot if bank == s.self_bank else s.stock_boot


def boot_target(s: FlashState) -> str | None:
    # mcuBootAddrGet()/Telink convention checks A before B.
    if bank_boot(s, "A") == "enabled":
        return "A"
    if bank_boot(s, "B") == "enabled":
        return "B"
    return None


def assert_boot_safe(s: FlashState) -> None:
    assert boot_target(s) is not None, s
    if s.stock_boot == "enabled":
        assert s.stock_sector == "stock-enabled", s
    if s.self_boot == "disabled":
        assert s.stock_boot == "enabled" and s.stock_sector == "stock-enabled", s


def transaction_steps(s: FlashState):
    """Yield state after each persistent primitive in one Stage-0 recovery run."""
    s = replace(s)

    if not (s.backup and s.journal):
        # The stock bank is untouched until BOTH recovery records are durable.
        assert s.stock_sector in {"stock-disabled", "stock-enabled"}
        s.backup = False
        yield "erase-self-backup", replace(s)
        s.backup = True
        yield "write-self-backup", replace(s)
        s.journal = False
        yield "erase-self-journal", replace(s)
        s.journal = True
        yield "write-self-journal", replace(s)

    if s.stock_boot != "enabled":
        assert s.backup and s.journal
        s.stock_sector = "erased"
        s.stock_boot = "erased"
        yield "erase-stock-sector0", replace(s)

        # The boot byte remains erased throughout all 16 page writes.
        for page in range(16):
            s.stock_sector = "partial" if page < 15 else "staged"
            s.stock_boot = "staged" if page == 15 else "erased"
            yield f"write-stock-page-{page:02d}", replace(s)

        # Complete-image CRC passed; commit only the one boot byte FF -> 4B.
        s.stock_sector = "stock-enabled"
        s.stock_boot = "enabled"
        yield "commit-stock-boot-flag", replace(s)

    # Final one-way commit; stock is known-valid before this can happen.
    s.self_boot = "disabled"
    yield "invalidate-stage0-self", replace(s)


def resume_after_power_cut(s: FlashState) -> FlashState:
    """Boot the image Telink would select, and let Stage-0 finish if selected."""
    assert_boot_safe(s)
    target = boot_target(s)
    assert target is not None

    if target == s.stock_bank:
        # Direct return to valid stock is already the desired safe state.  The
        # stale Stage-0 flag may remain set only in the B-self/A-stock case;
        # bank-A-first means it will not execute on subsequent normal boots.
        assert s.stock_boot == "enabled"
        assert s.stock_sector == "stock-enabled"
        return s

    assert target == s.self_bank
    assert s.self_boot == "enabled"
    if s.stock_sector in {"erased", "partial", "staged"}:
        assert s.backup and s.journal

    current = s
    for _, current in transaction_steps(current):
        assert_boot_safe(current)
    return current


def verify_orientation(self_bank: str) -> int:
    initial = FlashState(self_bank=self_bank)
    assert_boot_safe(initial)
    assert boot_target(initial) == self_bank

    baseline = list(transaction_steps(initial))
    assert baseline

    for name, interrupted in baseline:
        assert_boot_safe(interrupted)
        final = resume_after_power_cut(interrupted)
        assert final.stock_boot == "enabled", (self_bank, name, final)
        assert final.stock_sector == "stock-enabled", (self_bank, name, final)
        assert boot_target(final) == final.stock_bank, (self_bank, name, final)
        assert_boot_safe(final)

    partial_states = [s for n, s in baseline if n.startswith("write-stock-page-")]
    assert len(partial_states) == 16
    for s in partial_states[:-1]:
        assert s.stock_boot != "enabled" and s.self_boot == "enabled"
    staged = partial_states[-1]
    assert staged.stock_sector == "staged" and staged.stock_boot == "staged"
    assert staged.self_boot == "enabled"

    # Normal no-cut completion must leave only stock bootable.
    completed = baseline[-1][1]
    assert completed.self_boot == "disabled"
    assert completed.stock_boot == "enabled"
    assert boot_target(completed) == completed.stock_bank

    return len(baseline)


def main() -> int:
    cuts_a = verify_orientation("A")
    cuts_b = verify_orientation("B")
    print(
        "GLSD_STAGE0_POWERFAIL_MODEL=PASS "
        f"orientations=2 cut_points={cuts_a + cuts_b}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
