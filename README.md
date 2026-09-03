# analienx/gledopto — GL-SD-301P firmware ledger

Authoritative ledger for custom-firmware work on the GLEDOPTO **GL-SD-301P**
(Zigbee triac AC dimmer). Migrated from analienx/bseed#9; the live control
issue is analienx/gledopto#1.

Current phase: **Phase 1 — SOFTWARE-ONLY / READ-ONLY evidence** (complete to
the extent possible; see `evidence/phase1-software-only-20260903/`).

The production installed unit is not the first canary. Any flashing requires a
sacrificial spare and a separate Supervisor-authored gate sequence.

## Layout

- `AGENTS.md` — mandatory agent bootstrap (canonical skill is EXTERNAL_GITHUB).
- `.supervisor/project.yaml` — project manifest.
- `devices/gl-sd-301p/` — device ledger (README = facts, STATUS = state).
- `evidence/` — sanitized raw evidence per run. Raw dumps/binaries stay local
  (host `/config/zigbee2mqtt/gledopto_probe/`) and are never committed.
