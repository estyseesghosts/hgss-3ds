# AI disclosure

This project is developed with help from Claude.

## What the AI does

- Writes much of the port's tooling, scripts and documentation.
- Rebases the SDK patch stacks and researches the DS hardware and SDK sources.
- Reviews build failures and proposes fixes.

## How AI-written changes are handled

- Commits written with AI help carry a `Co-Authored-By: Claude` line, so the
  history shows where it was involved.
- Every change is built by CI and tested by a human before it is merged;
  anything about hardware or SDK behavior must cite the source it comes from
  (the decomp's `lib/` or `sub/`, the Platinum port's header comments, GBATEK).
- The project owner decides what gets merged and is responsible for the
  repository's contents.

## What the AI is not used for

- Generating, recreating or "cleaning up" game assets. All game content comes
  from the player's own cartridge dump.
- Reproducing code from leaked sources or from ports that bundle game assets.
- Handling proprietary SDK or compiler files beyond copying them from the
  user's read-only private folder into git-ignored paths.
