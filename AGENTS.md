# Agent instructions — `analienx/gledopto`

## Source locality

This is a LOCAL checkout. Canonical Supervisor ↔ Executor authority is the
latest **EXTERNAL_GITHUB** state on `analienx/config` `main`.

Mandatory canonical reads before meaningful supervised work:

1. `skills/supervisor-executor/SKILL.md`
2. `skills/supervisor-executor/CAPABILITY_PROFILES.md`
3. `skills/mutation-safety/SKILL.md`
4. `skills/supervisor-executor/PROJECT_CONTEXT_ARTIFACT.md`
5. `supervisor/projects.yaml`

Then load LOCAL:

6. `.supervisor/project.yaml`
7. `CHATGPT_PROJECT_CONTEXT.md`
8. this `AGENTS.md`
9. `devices/gl-sd-301p/README.md`
10. `devices/gl-sd-301p/STATUS.md`

Finally load the assigned GitHub task/control issue, newest comments, referenced
PRs, exact commits, CI and evidence required for the current decision.

If authenticated canonical GitHub access is unavailable, do not claim policy is
current and do not begin newly authorized mutation. Bounded read-only
orientation may continue; otherwise report `BLOCKED`.

## Project designation

Canonical registry identity:

```text
PROJECT_ID: gledopto-gl-sd-301p
DEFAULT_EXECUTION_MODE: host_specialist
OPTIONAL_EXECUTION_MODE: foundry_isolated
BUILD_LANE: firmware builder CI
HOST_SPECIALIST: USB, MQTT, HA API
EXECUTOR_ENVIRONMENT: windows_notebook
CONTROL_CHANNEL: analienx/gledopto#1
```

Execution mode, capability profile, autonomy scope and mutation authority are
separate dimensions.

Recommended engineering split:

```text
isolated firmware/review:
  EXECUTION_MODE = foundry_isolated
  PROFILE        = AUTONOMOUS or EXPERT as task warrants
  SCOPE          = WORKSPACE_IMPLEMENTATION

live production/device action:
  EXECUTION_MODE = host_specialist
  PROFILE        = OPERATOR
  SCOPE          = BOUNDED_RUNTIME
```

A capable profile never grants device/runtime mutation authority.

## Mutation boundary

The canonical deterministic mutation policy is mandatory. Potentially
destructive or durable **machine** mutations must use the named mutation broker
flow from `analienx/config`; do not bypass a broker rejection with ad-hoc
PowerShell/CMD/bash/Python/Node/RDC/Pi/Cline/Codex execution.

Repository/GitHub mutations still require valid Supervisor/user authority under
the canonical protocol. Direct user authorization in the active chat is valid
when clear and bounded and should be mirrored minimally to the issue ledger.

## GL-SD-301P protected invariants

- The safety-reviewed End Device candidate remains frozen by exact SHA until a
  newer candidate is independently reviewed and re-gated.
- Never infer live-device success or authorization from green CI alone.
- No broad OTA publication, global override index, unrelated device mutation,
  factory reset/re-pair, binding mutation, coordinator-wide change or unknown
  manufacturer-specific write unless explicitly and separately authorized.
- Raw flash dumps, credentials, Zigbee network keys, private auth/session state
  and unsanitized logs must never be committed.
- Vendor/reference material and independently authored implementation must
  remain separated according to the clean-room policy.
- On unexpected hardware/runtime state during a bounded canary, STOP and follow
  the recovery/rollback path from the latest authorized issue handoff.

## `.`

`.` means `supervise_latest`: resolve the active Gledopto stream from
conversation context first, otherwise from the canonical registry and newest
issue/executor activity. Fetch GitHub state directly; do not ask the user to
relay routine executor output.
