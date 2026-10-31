# Progress records

This folder is the project's memory. It is written for someone resuming with no context: what was
done, how it was verified, what is still open, and what to run next.

## Read in this order

1. **[STATE.md](STATE.md)** — the current snapshot. One page: what works today, what is in flight,
   exact commands with expected output, the open decisions, and a dated log of sessions. Start here.
2. **[phase-0.md](phase-0.md)** — the baseline: container, matching DS builds, tools.
3. **[phase-1.md](phase-1.md)** — the SDK patch stack, layout probe, DS-side gate and port wiring.
   This is the phase in progress.
4. **`agent-notes/`** — one note per work slice, written by the agent that did it. They are raw: the
   commands it ran, the measurements it took, and what it handed to the lead. The lead folds the
   results into `STATE.md` and the phase record.

## Conventions

- **A phase record is append-only history.** Do not rewrite a result that was true when it was
  written. Add a dated section at the top ("Update YYYY-MM-DD") and change the status line. If a
  number is superseded, say what it was and what it is now.
- **`STATE.md` is the living page.** It is rewritten in place. Every entry points at the phase
  record or agent note that holds the evidence.
- **Every claim needs a command.** "Works" means a command in this repo printed the expected result
  in the container, on the date given. If it was not run, write "not verified" instead.
- **Never put game content here.** No ROMs, saves, extracted assets, golden frames or `.pak` files.
  Paths under `/work` are fine to mention; their contents are not copied into the repo.
- **Absolute paths inside the container** are `/workspaces/pokeheartgold-3ds`, `/work`,
  `/private`. On the host, the repo is wherever you cloned it; `build/` is a volume and appears
  empty on the Mac.

## Updating a record

1. Run the check you are about to claim. Keep the exact command and its summary line.
2. Add the date, the command, the result and any caveat.
3. Update `STATE.md` first (status, next actions, log entry), then the phase record if the phase's
   exit criteria changed.
4. If a slice had its own brief or agent note, keep the note; it is the audit trail for that diff.
