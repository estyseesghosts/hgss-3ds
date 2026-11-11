# AGENTS.md

Repository-wide rules for agent sessions working on pokeheartgold-3ds.

## READ THE WIKI 

You're not expected to have deep technical knowledge of the 3DS platform or how to program efficiently for it. This information has been collected for you already. If it isn't already on disk, clone https://github.com/estyseesghosts/coffeepot.git. Always refer to coffeepot's CTR section when you're writing code or planning project changes - read the relevant hardware, firrmware or engineering articles, so you understand what you're working with. If you can't justify a change with real-hardware evidence or a citation from the wiki, you probably should not do it. Never edit coffeepot yourself. If you notice something genuinely incorrect, PR and tell the user. 

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

## Git identity

The only git identity an agent may use in this repository is:

```text
terezi <jules@foxtails.me>
```

Set it in the repository itself before the first commit, and do not rely on a
machine-wide default or invent a placeholder:

```sh
git config user.name terezi
git config user.email jules@foxtails.me
```

Every agent commit carries both fields. Do not use `A4`,
`a4@example.invalid`, the host's auto-detected address, or any other
identity, and do not author on someone else's behalf. If a commit already
exists under another identity, report it to the lead rather than rewriting
history: an artifact's `build-info` names the commit it was built from, and
re-authoring a commit changes that name.
