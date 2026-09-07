#!/usr/bin/env python3
"""Apply only source/build-script convergence; workflow changes are committed separately."""

from pathlib import Path

import integrate_clean_power_stage as integration


def main() -> None:
    integration.patch_app()
    integration.patch_link()
    stub = Path("firmware/gl-sd-301p-ed/glsd_power_stage_stub.c")
    if not stub.exists():
        raise SystemExit("expected power-stage stub missing")
    stub.unlink()
    print("CLEAN_POWER_STAGE_SOURCE_INTEGRATION=PASS")


if __name__ == "__main__":
    main()
