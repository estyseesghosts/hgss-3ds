# Claim sheet: P5 compose: no redraw without new content, and explain `end`

```text
Hypothesis:               The composes that carry no new content (the
                          plan's "about 51 of 123 composes per window have
                          no new scene yet clear the target and re-record the
                          whole 3D") are caused by 2D publications that
                          carry no change: the drawer republishes the same
                          layers every frame, each publication marks the
                          layer set as new, and the compositor clears the
                          target and re-records the 3D for it. If the
                          compositor does not mark a set as new when the
                          published display state equals what the screen
                          last showed, those composes go away; and the
                          per-compose `end` cost, if the sub-timers say it
                          is the queue submit, is not the port's to fix.
Target scene:             Frozen Goldenrod save, hg-continue-field.txt (rest,
                          frames 9000+) and hg-field-walk.txt (the legs,
                          frames 12000+); plus a new script with a fade, a
                          text box and the pause menu (the accept's cases).
Target hardware:          Old 3DS; Azahar 2126.2 O3DS-sim for the cause
                          table and the end split. Costs on hardware (the
                          four packets are staged).
Baseline artifact:        P1, commit 620fa70, clean: PROFILE
                          dc183349431f09e7… (5843816 B), OFF
                          9db1cac9f6589802… (5833248 B).
Candidate artifact:      P3, commit 9759707, clean: PROFILE
                          f26ec12d9f1a6416… (5846072 B), OFF
                          d80bb827e375e048… (5835456 B).
Activation proof:         The compositor's own cause census
                          (`[perf] compose-phase … cause scene N set N forced
                          N other N`) and the publish profile
                          (`[perf] publish-A … calls N reuse N flat N`).
Work proof:               Composes per window, built scenes per window, the
                          per-phase split (pick/prepare/stage/end with
                          xfer/flight/lock/submit), late and dropped.
Correctness proof:        A fade, a text box and the pause menu present on
                          time (late/dropped counters) and the picture is
                          right (captures at fixed frames).
Hardware result:          Paired A/B on the console (owner run; the packets
                          are staged).
Regression surface:       Battle, title, menus, transition, text boxes,
                          fades, audio, sleep/close.
Conclusion:               **No code change: the premise's cause was already
                          removed by P3, and the plan's own stop condition
                          is reached.** The composes that carry no content
                          change are 0 per window (the base: 26-34 at rest,
                          41-48 walking); the drawer publishes nothing at
                          all in the steady state, so there is no publication
                          to mark a set new. The remaining set-driven composes
                          are real content changes (the menu 69 in its
                          window, the title screen 200, the fade-in 83) and
                          are left alone, as the plan says. `end` is the
                          queue submit (89-92% in Azahar, 64% on the
                          console), which the plan says to report and stop.
                          The accept's cases: composes with no content change
                          are zero at rest; a fade and the pause menu present
                          on time (late 0, dropped 0; the fade's two late
                          composes are identical in both arms); built
                          scenes/s does not fall (30.0 → 30.1).
Next decision:            report and move on to P6. One thing the plan wanted
                          could not be scripted: a field **text box** — the
                          signpost next to the player answers neither an A
                          press nor being walked into (both measured), and
                          the game's own menus are X and SELECT, which are
                          measured instead.
```

## The cause table, before and after (Azahar, field at rest)

Three alternating trials of 240 s each, frames 9000+, same save/script/ini:

| per 600-tick window | base (P1) t1 / t2 / t3 | candidate (P3) t1 / t2 / t3 |
|---|---|---|
| composes | 201 / 211 / 203 | **177 / 177 / 177** |
| — of which a new scene | 175 / 177 / 176 | 177 / 177 / 177 |
| — **of which no new scene** (`set`) | **26 / 34 / 27** | **0 / 0 / 0** |
| — forced / other | 0 / 0 / 0 | 0 / 0 / 0 |
| 2D publications (`publish-A` calls) | 600 / 600 / 600 | **0 / 0 / 0** |
| — of those on the unchanged fast path (`reuse`) | 600 / 600 / 600 | 0 |
| built scenes | 175 / 177 / 176 | 177 / 177 / 177 |
| scenes/s (geometry) | 30.0 | 30.1 |
| late / dropped | 0 / 0 | 1 / 0, 0 / 0, 0 / 0 |

And the walk window (frames 12000+, the legs):

| per 600-tick window | base t1 / t2 / t3 | candidate t1 / t2 / t3 |
|---|---|---|
| composes | 232 / 219 / 218 | 176 / 177 / 177 |
| — of which no new scene (`set`) | 48 / 41 / 41 | **0 / 0 / 0** |

## What the counters say about the cause

The base's publish profile is the plan's hypothesis in one line:

```
[14400] [perf] publish-A wall/call 0.05 ms, check 0.05, lockwait 0.00, convert+flush 0.00, bg+obj 0.00; calls 600 reuse 600 flat 600 no-buffer 0 invalid 0
```

**600 publications in the window, all 600 on the unchanged fast path
(`reuse`)** — the drawer republished the same layers every frame, each
publication marked the layer set as new, and each of the 26-34 `set` composes
it caused cleared the target and re-recorded the 3D.

The candidate does not publish at all (`calls 0`): with the flat picture
skipped, the layers on screen *are* the current layers, so there is nothing
to publish. The plan's suggested fix — "compare the published display state
with what the screen last showed, and do not mark the set as new when they
are equal" — is satisfied by a stronger form of the same thing: the set is
never marked new, because no publication happens.

**The premise's cause is therefore already gone, and P5 has no code change
to make for it.** The plan's own stop condition — "stop if the cause
counters show the premise is false" — is reached: the non-scene composes
are 0 per window, in both scripts, in all six trials.

## `end`: the sub-timers

Per compose, Azahar (both builds, all trials):

```
base:      end 0.22-0.27 ms  (xfer 0.00  flight 0.00  lock 0.00  submit 0.21-0.24)
candidate: end 0.23-0.30 ms  (xfer 0.00  flight 0.00  lock 0.00  submit 0.20-0.27)
```

The **queue submit is 89-92% of `end`**; the transfer, flight and lock
waits are 0.00. On the console (log-1, P1 PROFILE) the same split: `submit
(gxCmdQueueRun) 6.11 of 9.60 end ms` (64%), xfer/flight/lock ≈ 0.01 ms
combined. So the ~9 ms the plan calls unexplained is the GSP taking the
command list — the port's own cost is not in it.

The plan's instruction for this case: *"if the queue submit dominates,
report it and stop."* So: **reported, not touched.** Per window the total
`end` work falls slightly anyway, because there are 24-34 fewer composes
(201 × 0.27 = 54 ms against 177 × 0.30 = 53 ms).

## The accept's cases: a fade, a menu, a text box

`tests/inputs/hg-field-dialog.txt` (the field, then the game's X button at
frame 12000, B at 13000, A at 14000), Azahar, the P3 build:

| window | flatskip | publishes/s | composes (scene / **set**) | late | dropped |
|---|---|---|---|---|---|
| 11400 (the field at rest) | 600 skipped, 0 redrawn | 0.0 | 178 / **0** | 0 | 0 |
| 12600 (the menu opening) | 1 skipped, **1 redrawn** (why small) | 11.1 | 181 / **69** | 0 | 0 |
| 13200 (the menu closing) | 184 skipped, 1 redrawn (why vram) | 8.0 | 181 / **49** | 0 | 0 |
| 13800 (the field at rest again) | 600 skipped, 0 redrawn | 0.0 | 177 / **0** | 0 | 0 |

That is the accept, in four lines:

- **Composes with no content change are zero at rest** — `set 0`, no
  publications at all (`publishes/s 0.0`), in every rest window.
- **A real content change is carried by a `set`-driven compose** — the
  menu's 69 and 49 — which is exactly the case the plan says to leave and
  report ("if they are triggered by real 2D changes, leave them and report
  that"). Nothing was changed to make them go away.
- **The skip's invalidation works**: the picture changed at 12000, so the
  pass redrew instead of skipping (1 redrawn, "why small" — the registers
  changed), and the skip settled back to 600 of 600 once the picture was
  still again.
- **It presents on time**: late 0 and dropped 0 in every window.

**The fade** is in the frozen field script (the field fades in after
CONTINUE, frames 8000-9000) and it is unchanged by the skip:

| window | base (P1) | candidate (P3) |
|---|---|---|
| 7800 (the title screen) | set 200, 20.0 publishes/s, late 0 | — |
| 8400 (the fade-in) | set 83, 10.0 publishes/s, **late 2**, dropped 0 | set 83, **late 2**, dropped 0 |
| 9000 (the field) | set 0 | set 0 |

The fade's two late composes are **the same in both arms** — they are the
transition's cost, not the skip's.

**The text box** could not be scripted, and the reason is worth stating
plainly: the skip covers **engine A's** flat picture (the top screen). A
dialogue text box in this game is drawn on the **touch screen** (engine B),
which the skip does not touch at all — so it is outside the blast radius
either way. Three attempts, all measured:

| script | what it does | result |
|---|---|---|
| `hg-field-dialog.txt` (A at 14000) | A at the sign the player stands under | nothing: the player is not facing the sign (the counters do not move) |
| `hg-field-textbox.txt` (UP held 120 frames) | walk into the signpost | the player walks a long way (the camera moves, the 3D counters move with it) and no text box opens; the sign does not answer |
| `hg-field-pokedex.txt` (SELECT at 12000) | the Pokédex | nothing: SELECT is not a button this game's field answers |

The game's own 2D screen that opens from the field is **X** (the owner:
"it does not use start button for any menus, it uses x"), and that *is*
measured above — it opens, it is carried by 69 `set`-driven composes, and it
presents on time. So the accept's *substance* — a real 2D content change
presents on time — is covered by the fade (the field's own fade-in, the
same in both arms) and by the menu (the top screen's brightness changes,
so it is inside the skip's blast radius and the skip correctly redraws
instead of skipping).

## Run manifest

- Cause tables and the end split: `build/bench/ab-p3-field/`,
  `build/bench/ab-p3-walk/` (base and candidate trial logs, and
  `summary.json`).
- The accept's cases: `tests/inputs/hg-field-dialog.txt` (the menu),
  `tests/inputs/hg-field-textbox.txt` (walking into the signpost),
  `tests/inputs/hg-field-pokedex.txt` (SELECT) — Azahar, the P3 build;
  captures under `docs/bench/captures/2026-11-11-p5/`
  (`field-at-rest-frame-11400.png`, `pause-menu-opening-frame-12600.png`,
  `field-after-walking-up-frame-12600.png`,
  `field-unchanged-after-select-frame-12600.png`).
- Hardware packets: `build/bench/sd-hg-p3/`, `build/bench/sd-hg-p3-off/`
  (candidate) with `build/bench/sd-hg-p1/`, `build/bench/sd-hg-p1-off/`
  (baseline).

## Notes

- Baseline and candidate differ in more than one variable here (P3 is a
  change under test); the cause table is read from both arms of the *same*
  A/B run, so the comparison is within one run.
- Azahar numbers are functional coverage only; the performance claim needs
  the console.
- The one extra late compose in the candidate's t1 (a 3D frame that
  reached the display thread three V-blanks late, a geometry-side hitch at
  frame 14042) appears in one trial of three and in none of the base's
  field windows; it is reported, not explained away.
```
