---
name: full-cycle
description: Complete substantial sd2snes engineering work through an evidence audit, remaining-work plan, incremental implementation, tests, local candidate commits, and independent exact-commit review. Invoke for behavior, protocol, build, or multi-file changes, or an explicit full-cycle request. Keep small edits lightweight. Delegated roles execute only their bounded assignment.
---

# Full engineering cycle

## Locate and preserve

If started above the repository, look for `work/sd2snes/AGENTS.md`; otherwise
use `git rev-parse --show-toplevel`. Read the repository AGENTS instructions.
Resolve all commands and agent assignments to that absolute repository path.
An existing task can read this file directly even before skill-list refresh.

Record the branch, full HEAD, status (including untracked paths), staged diff,
unstaged diff, and acceptance criteria from the current request. Save concise
notes/evidence under ignored `.build/full-cycle/`; avoid secrets and large logs
in handoffs. Identify pre-existing changes by file/hunk before editing. Preserve
them and staging. Do not reset, stash, discard, or clean the checkout. If the
branch differs from `feature/fxpak-os`, investigate before switching it.

## Roles and economical dispatch

Use coordinator Terra (`gpt-5.6-terra`) Medium where the runtime supports it.
Use these separate agent roles from `.codex/roles/`:

| Role | Model | Effort | Assignment |
|---|---|---|---|
| investigator | gpt-5.6-terra | low | Read-only evidence audit |
| planner | gpt-5.6-terra | medium | Read-only remaining-work plan |
| reviewer | gpt-5.6-terra | medium | Read-only exact candidate review |
| tester | gpt-5.6-terra | low | Routine checks; generated outputs only |
| implementer | gpt-6-astra | medium | Bounded implementation needing Astra |
| debugger | gpt-6-astra | high | Difficult implementation/debugging only |

Use configured custom roles when the spawn tool supports them. In runtimes
exposing only `model`/`reasoning_effort`, supply both explicitly, use a short
context or `fork_turns="none"`, and include the relevant role file's instructions
in the assignment. Do not assume merely naming a task loads its role config.
Verify returned runtime metadata when available; report requested settings as
requested, not observed execution settings, if no metadata is exposed. The main
session cannot change itself by reading config. Report overrides/unavailability;
never silently substitute models or pretend self-review is independent.

At most two children active, one writer, and no child delegation. Reuse a
completed child for the same role if slots are limited; never reuse an author
as the reviewer. Close finished agent threads when the runtime provides that
operation; some runtimes cap open threads, not just actively running turns.
If thread limits prevent an independent reviewer, report that review blocker.
Give every handoff the absolute repo, task/scope exclusions,
baseline/candidate SHA, owned paths, relevant evidence, exact question, and a
short return format. Share the audit/plan instead of repeating repository scans.
Parallelize only independent useful work. Do not spawn agents just to wait.
Coordinator handles straightforward implementation; delegate Astra only when
needed. Before High escalation, record the specific difficulty, evidence, what
Medium could not resolve (or why insufficient), and a bounded stopping condition.

Read-only roles must not edit, stage, commit, install, or run mutating tests.
Their configured read-only sandbox is a default that live runtime permissions
may override. Enforce read-only behavior in assignments too; do not claim OS
enforcement without checking the effective sandbox. Testers may write only
agreed generated outputs; source fixes return to the owner.

## Execute the cycle

1. **Audit:** Have an independent investigator inspect the relevant existing
   implementation and evidence. Classify each acceptance criterion as completed,
   partial, missing, or unverified, with file/test evidence and confidence limits.
   Resolve contradictory docs/logs explicitly. Read Phase 1 first, then the handoff
   and Phase 2 for product work. Don't rebuild a completed milestone as busywork.
2. **Plan:** Give that audit to a separate read-only planner. Plan only remaining
   work in the current scope, with small changes, owners, meaningful tests, and
   blockers. Coordinator resolves routine choices without unnecessary approvals.
3. **Implement/test:** Make incremental changes, reusing working code. Run focused
   checks through a Terra Low tester while doing independent useful work. Run the
   existing emulator/MCU checks when menu/protocol/timing changes warrant them;
   workflow-only edits need workflow checks. Reuse passing results for unchanged
   inputs; broaden tests only for changed behavior or unresolved risk.
4. **Candidate:** Stop writers; inspect diffs and the index. Prepare an explicit
   list of intended paths/hunks. For wholly owned files without pre-existing
   unrelated changes, use `git add -- <new-owned-files>` as needed, then
   `git commit --only -m '<purpose>' -- <owned-paths>`. Never use `git add .`, `-a`,
   or an ordinary commit that sweeps up unrelated staging. `--only` commits the
   working-tree content of selected paths: it is NOT safe for mixed-ownership
   files. In that case use a separately prepared temporary index/patch based on
   HEAD and prove preservation of the user's original staged/unstaged hunks
   before advancing HEAD; if this cannot be done reliably, report the exact
   overlap as a blocker instead of committing unrelated work. Compare unrelated
   staged diff and file hashes before/after. Record the full candidate SHA and
   `git show --stat`/changed paths. Make no empty commits for a no-op request.
5. **Independent review:** A separate agent that did not author the change must
   review that exact SHA, its parent diff, relevant surrounding code at that SHA,
   acceptance criteria, and test evidence. Pass cumulative cycle baseline too:
   a fix commit must be reviewed together with earlier cycle changes. Use
   `git show <sha>:<path>` for dirty paths; the live worktree is not the candidate.
   Ask for `APPROVED <full SHA>` or actionable findings with severity, location,
   failure mechanism, and validation gap. Approval cannot be inferred from silence.
6. **Fix/re-review:** Assess findings against evidence, fix valid ones, test the
   affected behavior, create a new candidate commit, and re-review the full
   cumulative result. Explain rejected findings to the reviewer. Every candidate
   change invalidates prior approval. After two consecutive cycles with no new
   evidence or meaningful improvement, stop and report the concrete blocker,
   attempted fixes, and required input/change. If the independent agent/model is
   unavailable, preserve the candidate and report review blocked; no self-approval.
7. **Final verification:** Match the approved SHA against HEAD and final acceptance
   criteria. Verify committed inputs match tested inputs (including any required
   pre-existing work); if dirty dependencies influenced tests, label that result
   as worktree-only and verify the clean candidate separately where feasible.
   Re-run only tests invalidated by changes. Confirm unrelated work/staging is
   preserved. Report reviewed SHA/reviewer, acceptance results, tests, limitations,
   pending work and hardware-only checks. Approval is code-review evidence, not
   proof of tests or hardware success. Do not claim completion with unmet criteria.

Do not push, merge, flash hardware, or expand product milestones unless the
current user request explicitly authorizes it. Preserve native art, fallback,
loader/navigation/MCU behavior and NTSC/PAL VBlank timing. The harness simulates
cartridge responses and ends at CMD_RESET: physical SD latency, game boot,
FPGA reconfiguration, saves and real controller/video behavior remain separate.
