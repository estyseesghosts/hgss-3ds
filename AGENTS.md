# AGENTS.md

Repository-wide rules for agent sessions working on pokeheartgold-3ds.

## Delegating to subagents

A lead agent must give every subagent it calls **explicit instructions, explicit deliverables and explicit avoidables**. This is not optional, and it matters most for implementer agents: they edit files and cannot rely on chat context the lead has not written down.

Every subagent brief must state, in the prompt itself:

1. **Objective** — the one bounded slice to complete, and why it exists.
2. **Allowed paths** — the exact files or directories the subagent may read and edit.
3. **Deliverables** — the concrete output: code, patch files, tests, docs, and the required report format (files changed, commands run, results, open questions).
4. **Acceptance checks** — the commands or observations that prove the work is done, and the expected result.
5. **Avoidables** — what not to touch or do: paths outside the slice, unrelated refactors, the decomp checkout in place, commits/pushes unless explicitly assigned, and any known traps from `gameplan.md` or the risk register.
6. **Stopping point** — when to stop and report instead of guessing, for example if the slice grows, the brief conflicts with `gameplan.md`, or an acceptance check cannot pass.

Do not send a subagent a vague goal and let it infer scope. If the lead cannot fill in all six items, the slice is not ready to delegate. A brief that is broader, vaguer or in conflict with `gameplan.md` should be revised before the call, not discovered by the subagent mid-edit.

Implementer subagents hold `docs/progress/STATE.md` and `gameplan.md` as required reading; the brief adds the slice-specific detail those files cannot contain. When this file disagrees with an agent definition under `.opencode/agents/` or `.claude/agents/`, this file wins.
