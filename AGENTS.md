# Agent instructions — `analienx/gledopto`

## Source locality — read this first

You are operating in a LOCAL checkout of `analienx/gledopto`.

The canonical Supervisor ↔ Executor control plane is **EXTERNAL_GITHUB**:

- private GitHub repository: `analienx/config`
- canonical ref: `main`
- protocol: `skills/supervisor-executor/SKILL.md`
- registry: `supervisor/projects.yaml`

Notation such as `analienx/config:skills/supervisor-executor/SKILL.md` means
**GitHub repository + repository path**, not a path inside this LOCAL worktree.
Never assume `analienx/config` is already cloned locally.

Before meaningful supervised work, verify authenticated GitHub access and fetch
the latest external canonical files:

```sh
gh api -H "Accept: application/vnd.github.raw+json" "repos/analienx/config/contents/skills/supervisor-executor/SKILL.md?ref=main"
gh api -H "Accept: application/vnd.github.raw+json" "repos/analienx/config/contents/supervisor/projects.yaml?ref=main"
```

If canonical GitHub access is unavailable, do not claim policy is current and
do not begin a newly authorized mutation; report `BLOCKED`. Bounded read-only
orientation may continue if safe.

## Mandatory bootstrap

1. latest **EXTERNAL_GITHUB** `analienx/config/main:skills/supervisor-executor/SKILL.md`;
2. latest **EXTERNAL_GITHUB** `analienx/config/main:supervisor/projects.yaml`;
3. LOCAL `.supervisor/project.yaml`;
4. LOCAL `CLEAN_ROOM.md`;
5. this LOCAL file;
6. LOCAL `devices/gl-sd-301p/README.md`, `devices/gl-sd-301p/STATUS.md`, and
   `devices/gl-sd-301p/interoperability/INTERFACE.md`;
7. the assigned **EXTERNAL_GITHUB** issue (currently analienx/gledopto#1) and
   its **newest comments**;
8. referenced PRs, commits, CI and evidence needed for the current decision.

Precedence: `explicit user instruction > stronger project safety policy >
canonical skill > repo/project specialization > task issue > older comments`.

## Clean-room implementation boundary

This repository is the implementation side of a clean-room interoperability
workflow. `CLEAN_ROOM.md` is mandatory policy.

- Do not ingest, commit, reconstruct, download in CI, attach, or publish vendor
  firmware, decompiler output, full disassembly, translated vendor source, or
  encoded/chunked equivalents.
- Do not implement by translating vendor control flow or data structures.
- Implementation may use only the sanitized interface specification, public
  standards/SDK documentation, and independently generated black-box tests.
- Treat every field marked `UNKNOWN` in the interoperability specification as
  fail-closed. Do not guess reserved bytes, checksums, level mappings, safety
  timings, power-stage commands or startup sequences.
- Any PR changing the interoperability implementation must state its public or
  sanitized specification basis and affirm independent implementation.
- Run `python3 tools/check_cleanroom.py` before handing work back.

Private reverse-engineering work belongs outside this repository. Only the
minimum behavioural/interface facts necessary for interoperability may cross
into the public specification.

## Device/hardware hard boundaries

- The installed production GL-SD-301P is not the first canary.
- No OTA update/downgrade/schedule, custom image serving, factory reset/re-pair,
  binding mutation, coordinator-wide changes, unknown manufacturer-specific
  writes, or opening the installed production unit unless a later explicit
  Supervisor gate supersedes this policy.
- Runtime electrical validation is performed only on a sacrificial spare under
  a separate Supervisor-authored gate.
- Mains-side investigation requires appropriate isolation and instrumentation;
  a repository commit or CI result does not authorize physical mains work.
- Credentials, Zigbee network keys and unsanitized logs must never be committed.

## Current architectural direction

The canonical interface specification currently classifies the power-stage path
as `SECOND_MCU_UART` with high software confidence: TLSR8258 UART, 9600 8N1,
TX=PB1, RX=PA0, six-byte application payloads. Exact packet semantics remain
partially unresolved, so no production power-stage encoder or flashable canary
is authorized yet.

## `.`

`.` resolves through the canonical `supervise_latest` operation via the
canonical skill/registry, using conversation context first.
