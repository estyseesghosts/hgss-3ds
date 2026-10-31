# pokeheartgold-3ds wiki

A native 3DS port of Pokémon HeartGold and SoulSilver, built on the ctrl-cheeb-del/pokeheartgold decompilation. The repo does not contain the game. You supply your own compiler, SDK and ROM. Status: [README](../../README.md#current-status), or the one-page [current state](../progress/STATE.md). The plan is [gameplan.md](../../gameplan.md).

## Pages

| Page | What it covers |
| --- | --- |
| [Getting-Started](Getting-Started.md) | From nothing to a green `make test`: OrbStack, the container, your private files, the decomp clone, the DS toolchain, the matching build, bootstrap and tests. Includes troubleshooting for problems the repo has recorded. |
| [Build-Reference](Build-Reference.md) | The toolchain and build lookup table: versions, environment variables, every build target and its exact command, outputs, flags and ABI, cleanup, and build-level troubleshooting. **Read this before changing anything about the build.** |
| [Daily-Workflow](Daily-Workflow.md) | The loop for working on the port: rebuilding `build/upstream`, the survey tools, the host test suites, the layout probe, the port layer, the DS-side gate, authoring a patch, and the pitfalls. |
| [Tools-Reference](Tools-Reference.md) | Every script in `tools/` and every test runner in `tests/`: purpose, usage, inputs, outputs, and whether it needs the container, MWCC or a ROM. |
| [Architecture](Architecture.md) | How the port is built: the game, the SDK and NitroSystem, the hardware layer and the platform shells, and what exists today against the plan. |
| [Patch-Stack](Patch-Stack.md) | How the decomp is used without editing it: the pin, bootstrap, numbering, the `SDK_PORT_3DS` guard, and a catalog of the patches. |
| [Save-Compatibility](Save-Compatibility.md) | Why the 524,288-byte save format must not change, how the game picks a slot, what `savetool` checks, and the layout probe. |
| [SDK-Differences](SDK-Differences.md) | How HGSS's NitroSDK and NitroSystem differ from the copy the Platinum port was written for, and what is not compared yet. |
| [Decisions-Explained](Decisions-Explained.md) | The main plan and implementation decisions, each with its reason and the place in the repo where it is recorded. |
| [Contributing](Contributing.md) | The hard rules (no game content, decomp changes only as patches, the byte-identical DS build, the save format), with a checklist for reviewing a patch. |
| [Glossary](Glossary.md) | The terms used across the project: MWCC, PXI, ARM7, host32, shim, NitroFS, overlays and others. |

## Repo documents

These are not wiki pages, but the wiki links to them.

- [gameplan.md](../../gameplan.md): the plan, the phases, the layers, the save specification and the risk register.
- [docs/progress/STATE.md](../progress/STATE.md): **the living status page**: what is green today, the exact commands and results, the next actions, the decisions still open, and a dated session log.
- [docs/progress/README.md](../progress/README.md): how the progress records are structured and how to update them.
- [docs/progress/phase-0.md](../progress/phase-0.md): what Phase 0 delivered, the matching SHA1s and the open items.
- [docs/progress/phase-1.md](../progress/phase-1.md): the SDK patch stack, the layout probe, the DS-side gate, and what is not done.
- [docs/progress/agent-notes/](../progress/agent-notes/): per-slice raw notes (commands, measurements, hand-off).
- [docs/decisions.md](../decisions.md): the decision log for choices the plan did not specify.
- [docs/devcontainer.md](../devcontainer.md): the container, its volumes, the private folder layout and the Rosetta and Wine notes.
- [docs/BASE.md](../BASE.md): the pinned decomp and Platinum commits, and the matching SHA1s.
- [docs/ci.md](../ci.md): the two CI workflows and what they run.
- [docs/sdk-diff.md](../sdk-diff.md): HGSS SDK against Platinum's, partial.
- [docs/port-wiring.md](../port-wiring.md): why the vendored port layer does not build against HGSS yet, with the current error map.
- [CONTRIBUTING.md](../../CONTRIBUTING.md), [NOTICE.md](../../NOTICE.md), [AI_DISCLOSURE.md](../../AI_DISCLOSURE.md): rules for contributions, licensing, and how the project is developed.
