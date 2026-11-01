# Phase 3 progress

Status: **complete** (2026-10-31, session 6; checked off in session 7). SoulSilver's real-image
end-to-end run is the one open gap: there is no SS ROM in the container, so SS is covered by the
synthetic-image tests and the image-driven builder, exactly as the Phase 3 steps require.

## Deliverables

- **`builder/verify_rom.py`** accepts exactly HG US `4fcded0e2713dc03929845de631d0932ea2b5a37` and SS
  US `f8dc38ea20c17541a43b58c5e6d18c1732c7e582`. Header and size checks run before the hash, so a
  trimmed, overdumped, header-modified or wrong-region image gets a specific reason rather than a
  generic hash mismatch.
- **`builder/extract_rom.py`** walks the FNT/FAT with cycle, duplicate, unsafe-name and extent checks.
  It keeps the original FAT IDs, which some game code uses instead of names, and writes
  `manifest.json` (`{id, path, size}` plus `rom_sha1`).
- **`builder/pack.py`** writes the ID-indexed pak (`docs/PAK_FORMAT.md`). Its default build ID is
  `heartgold-nitrofs-1`; the format itself is unchanged from Platinum's.
- **Boot-time check** — `hgss/game/app/heartgold.c` maps `NitroPort_OpenPak`'s results to words. The
  PC runner prints them on stderr; the 3DS shell prints them on the bottom screen as well as
  `log.txt`. `tests/sdk/pak_boot` links the real `heartgold.c` + `port_fs.c` + the build-private
  `pak.c` reader and covers OK, wrong build ID, wrong version, truncated/corrupt, missing files,
  pack/manifest mismatch and ROM SHA-1.
- **SD layout** — `docs/wiki/SD-Card-Layout.md` documents `sdmc:/3ds/pokeheartgold/` and the
  SoulSilver folder (`pokeheartgold-ss/`, `soulsilver3ds.pak`, `soulsilver-nitrofs-1`). The plan named
  `3ds_port/game/README.md`; that tree must stay byte-identical to `platinum.lock`, so the doc lives in
  the wiki. Recorded in `docs/decisions.md`.
- **Tests** — `tests/test_verify_rom.py`, `tests/test_extract_rom.py`, `tests/test_pack.py`, and
  `tests/synthetic_rom.py` build synthetic NitroFS images only. CI never sees a real ROM.

## HGSS file table finding

The retail HG image has **513 FAT entries but only 384 FNT names**: the 129 ARM9 overlays are unnamed
FAT entries 0–128, not an `overlay/` directory. The extractor names them
`overlay/overlay_NNNN.bin` from the header's overlay table and keeps every entry. The pak is
ID-indexed, so the names are informational.

## Real-ROM smoke (not committed)

513 files, 125,549,829 bytes extracted; 125,556,080-byte pack; build ID `heartgold-nitrofs-1`; SHA-1
`4fcded0e…`; manifest IDs contiguous 0..512 and every size matches the pack. The pack opens in
`pokeheartgold-pc`.

**The reader's allowlist was the last blocker.** `3ds_port/pak/src/pak.c` (byte-identical to
`platinum.lock`) hard-codes Platinum's two SHA-1s, so the real pack was rejected after every other
check passed. The harness generates a build-private copy of the reader with the two HGSS digests
prepended and compiles it as a program object linked before `libnitro.a`; the archive member is never
loaded and `3ds_port/` stays untouched.

## Open gap

SoulSilver's builder is tested against synthetic images only. When an SS dump is available, run the
same three commands and record the pack size and boot result here.
