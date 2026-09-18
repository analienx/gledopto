# Gledopto GL-SD-301P — Project Supervisor Bootstrap

## Purpose

Compact persistent bootstrap for fresh ChatGPT Project chats supervising the
GL-SD-301P End Device firmware track. Detailed policy and mutable state remain
canonical in GitHub. If this file conflicts with current canonical GitHub state,
**GitHub wins**.

## `.` shorthand

`.` means **supervise_latest** for this Project:

1. resolve the active Gledopto stream from conversation context first;
2. otherwise resolve `gledopto-gl-sd-301p` from the canonical registry;
3. fetch issue #1 and newest comments;
4. verify referenced PR/commit/CI/evidence before issuing the next decision.

## Canonical control plane

Always fetch latest `analienx/config` `main`:

- `skills/supervisor-executor/SKILL.md`
- `skills/supervisor-executor/CAPABILITY_PROFILES.md`
- `skills/mutation-safety/SKILL.md`
- `skills/supervisor-executor/PROJECT_CONTEXT_ARTIFACT.md`
- `supervisor/projects.yaml`

## Project identity

```text
PROJECT_ID: gledopto-gl-sd-301p
CANONICAL_REPO: analienx/gledopto
PROJECT_PATH: /
MANIFEST: .supervisor/project.yaml
CONTROL_CHANNEL: analienx/gledopto#1
```

## Durable execution designation

```text
DEFAULT_EXECUTION_MODE: host_specialist
OPTIONAL_EXECUTION_MODE: foundry_isolated
BUILD_LANE: firmware builder CI
HOST_SPECIALIST: USB, MQTT, HA API
EXECUTOR_ENVIRONMENT: windows_notebook
```

Execution mode is separate from capability/autonomy and mutation authority.

Preferred pattern:

```text
isolated code/review:
  foundry_isolated + AUTONOMOUS/EXPERT + WORKSPACE_IMPLEMENTATION

live device/canary:
  host_specialist + OPERATOR + BOUNDED_RUNTIME
```

## Source of truth

```text
analienx/gledopto GitHub = source-controlled firmware/docs/evidence
analienx/config/main      = Supervisor/Executor control plane
issue #1                 = durable runtime/canary ledger
live Zigbee/HA/Z2M state = authoritative for actual device behavior
```

Never infer successful physical conversion from Git/CI alone.

## Stable local entry point

```text
LOCAL_CANONICAL_CHECKOUT: C:\Workspace\repos\gledopto
SUPERVISOR_DIRECT_HOST: ZephyrusG16
```

Before local execution, verify external GitHub policy, local repository/ref,
issue #1 newest comments and exact candidate SHA. Use isolated worktrees for
implementation rather than mutating the canonical checkout casually.

## Safety / validation

- load the canonical mutation-safety skill before durable machine mutations;
- keep clean-room vendor/reference and independent implementation boundaries;
- preserve exact-SHA firmware gates;
- no broad OTA publication or unrelated device mutation;
- live canary authority must be explicit and bounded;
- STOP on unexpected runtime/hardware state and follow the authorized recovery
  path.

## Maintenance

Update this file only when durable routing/bootstrap information changes.
Ordinary progress, commits, CI results and incidents belong in issue #1,
STATUS.md and PR/evidence records.
