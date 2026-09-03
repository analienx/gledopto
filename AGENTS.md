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
4. this LOCAL file;
5. LOCAL `devices/gl-sd-301p/README.md` and `devices/gl-sd-301p/STATUS.md`;
6. the assigned **EXTERNAL_GITHUB** issue (currently analienx/gledopto#1) and
   its **newest comments**;
7. referenced PRs, commits, CI and evidence needed for the current decision.

Precedence: `explicit user instruction > stronger project safety policy >
canonical skill > repo/project specialization > task issue > older comments`.

## Hard boundaries

- Phase 1 (software-only) rules of analienx/gledopto#1 apply until a
  Supervisor supersedes them in the issue: no OTA update/downgrade/schedule,
  no custom image serving, no factory reset/re-pair, no binding mutation, no
  coordinator-wide changes, no writes to manufacturer-specific attributes, no
  opening the installed production unit.
- Raw flash dumps, firmware binaries, credentials, Zigbee network keys and
  unsanitized logs must never be committed. Evidence under `evidence/` is
  sanitized; raw originals live on the HA host under
  `/config/zigbee2mqtt/gledopto_probe/`.

## `.`

`.` resolves through the canonical `supervise_latest` operation via the
canonical skill/registry, using conversation context first.
