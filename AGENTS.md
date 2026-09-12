# FXPAK OS engineering workflow

Preserve the current checkout, user changes, and staging. Work on
`feature/fxpak-os`; inspect the branch and status before changing anything.
Read `docs/FXPAK_OS_PHASE1.md`, `CODEX_HANDOFF.md`, and
`docs/FXPAK_OS_PHASE2.md` before product work. Phase 1 is complete in the
emulator; Phase 2 is implemented. Audit evidence before deciding what remains.
Do not restart completed milestones or replace working code unnecessarily.

For substantial engineering tasks (behavior changes, debugging, multiple-file
changes, protocol/build work), automatically read and follow
`.agents/skills/full-cycle/SKILL.md` (`$full-cycle`). This explicitly requests
bounded subagent delegation, including an independent reviewer. A delegated
role must perform only its assignment, without recursively starting the cycle.
For a typo, small documentation edit, or similarly low-impact change, work
directly with a focused check unless the user explicitly requests the full cycle.

Use the models and effort in `.codex/roles/` and the skill. Prefer Terra Medium
for coordination. Report runtime overrides; instructions cannot switch the
current model or enforce a sandbox. Keep global preferences intact.

Preserve the ROM loader, directory navigation, favorites/recent, dialogs, MCU
commands, native artwork format, and NTSC/PAL NMI/VBlank guarantees. Mk.III is
the hardware target. Scope comes from the current request, not future milestones
in the handoff. Emulator launch to CMD_RESET is not physical game-boot validation.
Separate emulator, host, firmware-layout, synthesis/timing, and hardware evidence.

Local candidate commits are part of substantial engineering work. Do not push,
merge, flash hardware, or start new product milestones without explicit scope.
See `docs/CODEX_WORKFLOW.md` for activation, validation, and runtime limitations.
