# Reusable Codex full cycle

This workflow continues the existing `feature/fxpak-os` checkout. It does not
restart Phase 1, replace Phase 2, or authorize a push, merge, hardware flash or
new milestone. Read `AGENTS.md` and invoke:

```text
$full-cycle Continue the remaining Phase 2 work.
```

For another engineering task, replace the sentence after `$full-cycle` with
its scope and acceptance criteria. Substantial engineering requests also select
the skill through AGENTS instructions; small edits stay lightweight. This is
agent-guided orchestration, not a background daemon or mandatory Git hook.

## Roles

| Role | Model ID | Reasoning | Behavior |
|---|---|---|---|
| Coordinator/main | `gpt-5.6-terra` | Medium | Own scope, evidence, simple edits, integration and commits |
| Investigator | `gpt-5.6-terra` | Low | Independent read-only audit |
| Planner | `gpt-5.6-terra` | Medium | Independent read-only remaining-work plan |
| Reviewer | `gpt-5.6-terra` | Medium | Independent read-only exact-commit review |
| Tester | `gpt-5.6-terra` | Low | Routine tests; generated outputs only |
| Implementer | `gpt-6-astra` | Medium | Bounded implementation that needs Astra |
| Debugger | `gpt-6-astra` | High | Difficult work with a recorded escalation reason |

At most two children run alongside the coordinator, and only one agent writes
source. Children do not delegate. Assignments reuse concise evidence instead of
re-scanning the repository. The reviewer must not be an author of the changes.
Two consecutive review/fix rounds with no new evidence or meaningful improvement
end with a concrete blocker report, not an unsupported approval.

## Configuration and activation

The installed CLI used for setup is `codex-cli 0.153.0-alpha.5`. The checked-in
`.codex/config.toml` uses explicit `[agents.<role>]` registrations with relative
`config_file` paths to `.codex/roles/*.toml`. Each role file is a normal config
layer. This format is supported by the installed strict loader and the current
[configuration reference](https://learn.chatgpt.com/docs/config-file/config-reference).
The newer standalone agent format is not needed. No global preferences, trust
settings, plugins, or approval policy are overwritten.

Codex loads project settings only in trusted projects. For a new checkout, trust
that project through the normal Codex UI if prompted. The existing task starts
two levels above the repository, so install its entry points once:

```sh
python3 work/sd2snes/tools/codex/install_task_bridge.py "$PWD"
```

Run that command from the task directory containing `work/sd2snes`. It creates
four relative symlinks without replacing any existing path:

- `AGENTS.md` -> checked-in `tools/codex/task-AGENTS.md`
- `.agents/skills/full-cycle` -> the repository skill
- `.codex/config.toml` -> repository defaults/role registrations
- `.codex/roles` -> repository role files

The bridge is installed for this task. Its targets are versioned in the repo;
the four parent-directory links are local setup, outside the repository commit.
The helper is idempotent and stops on a collision for manual integration, leaving
existing instructions/configuration intact. It supports this task layout only.
For a different checkout layout, start Codex in the repository itself.

Skills are discovered from `.agents/skills`, including symlinked skill folders,
as described in the [official skill documentation](https://learn.chatgpt.com/docs/build-skills).
`codex debug prompt-input` verifies the entry instructions and skill listing from
both directories. If the existing app task does not show the newly added skill,
reload the app and resume this same task, or explicitly ask it to read the skill
file. There is no need to reset the checkout or create a new task.

**One-time model selection for this existing task:** select Terra / Medium in
the composer. Project config changes do not switch the already-running main
session; global Astra / High was left intact. For a fresh CLI session, an explicit
invocation removes ambiguity from other runtime overrides:

```sh
codex -C work/sd2snes -m gpt-5.6-terra -c model_reasoning_effort=medium
```

The current desktop collaboration tool exposes explicit model/effort parameters
but no custom `agent_type` or per-child sandbox selector. The skill therefore
also supports dispatching with explicit model and reasoning values plus the role
instructions. Task names alone do not select registered custom roles. Use short
context handoffs for model overrides; this tool does not accept model overrides
with a full-history fork. No action here changes the current main session model.

Investigator/planner/reviewer configurations request `sandbox_mode = "read-only"`.
This is a configuration default, not a claim of enforced isolation in the
desktop collaboration runtime. Live parent permission overrides may take
precedence, as the [subagent documentation](https://learn.chatgpt.com/docs/agent-configuration/subagents)
explains. Their assignments independently forbid mutations. Actual OS sandbox
enforcement and model execution identity must be reported only when runtime
metadata verifies them. Native config acceptance verifies settings, not account
model entitlement or inference. Setup's Terra agents were explicitly requested
through the runtime tool; the tool does not return an execution-model attestation.

## Validation

Run the inexpensive offline checks from the repository:

```sh
python3 tools/codex/test_workflow.py
python3 tools/codex/validate_workflow.py
```

The first uses disposable repositories to check idempotent relative bridges,
collision/symlink-parent rejection, and a real `git commit --only` preserving
unrelated staged and unstaged content. Whole-path commits are restricted to
wholly owned files; these tests do not make them safe for mixed user/agent hunks.

The second copies only the workflow into a disposable fixture with a temporary
`CODEX_HOME`, explicitly trusts only the fixture, and uses the installed native
`app-server --strict-config` loader. It verifies all role registrations, every
role config layer's exact bytes/settings, coordinator defaults, skill discovery
and AGENTS prompt inclusion from both repository and task directory. An unknown
key must be rejected as a negative control. No inference or credentials are
needed; test-only trust settings never enter the user's config.

Independent role dispatch, a bounded behavioral exercise, and exact-candidate
review supplement these structural checks during setup. Re-run the native
validator after Codex upgrades; experimental format changes may need adjustment.
The workflow itself requires final acceptance verification at the approved SHA,
with tests on dirty dependencies labeled worktree-only until a clean candidate
check establishes committed behavior.

## Product evidence carried forward

Setup began at `0d3c982` with four modified files: `CODEX_HANDOFF.md`,
`docs/FXPAK_OS_PHASE2.md`, `tools/fxpakos/build_mk3_firmware.sh`, and
`tools/fxpakos/verify_mk3_firmware.py`. They are outside this workflow change.

The initial independent audit found Phase 1 complete in the emulator and Phase 2
dynamic artwork implemented. It also found that the current docs' assertion that
both Mk.III MCU images validate conflicts with `.build/mk3-mini/both-build.log`:
STM32 linking finishes but verification ends with `ELF flash end differs from
firmware`; its verification log is empty. LPC Mk.III has a passing validator
log. Recheck these observations when resuming product work rather than treating
this dated snapshot as permanent state. Setup does not fix or commit those edits.

Fresh Quartus synthesis still needs Cyclone IV device support; a reused pinned
release mini core is not fresh synthesis. Physical SNES/SD/game boot, FPGA
reconfiguration, save persistence and controller/video checks remain hardware-only.
The emulator's CMD_RESET assertion is not physical game-boot validation. No
firmware tests or hardware operations are required merely to install this workflow.
