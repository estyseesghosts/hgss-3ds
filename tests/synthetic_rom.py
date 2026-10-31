"""Small, synthetic Nintendo DS images for the builder tests.

No game data: every byte here is constructed by the test. The images have a
real DS header (a 128 KiB device, as far as its device-size byte says), an
FNT tree, a FAT table and optionally an ARM9 overlay table, so the extractor
parses the same structures a retail ROM has.
"""
from __future__ import annotations

import struct

HEADER_SIZE = 0x200
ROM_SIZE = 0x20000          # device size 0 (1 << (17 + 0))


def _align(value: int, boundary: int) -> int:
    return (value + boundary - 1) & ~(boundary - 1)


def build_nds(tree: dict, *, overlay_count: int = 0,
              rom_size: int = ROM_SIZE, game_code: bytes = b"IPKE",
              unit_code: int = 0, device_size: int | None = None,
              title: bytes = b"POKEMON HG") -> tuple[bytes, dict]:
    """Build an image and its layout metadata.

    `tree` maps a directory path ("" for the root) to
    {"files": [(name, file_id)], "dirs": [child_name]} in name-table order.
    Named files get their data at their ID's slot; overlay IDs 0..N-1 get
    b"ovlNN" and an ARM9 overlay table entry. Any other FAT ID is a
    zero-length entry at the start of the data area.

    Returns (image, meta); meta has the FNT/FAT/overlay offsets, each file's
    data offset, each directory-reference's absolute offset
    (meta["dir_refs"][(dir_path, child_name)]) and each name's absolute
    offset (meta["name_offsets"][(dir_path, file_name)]).
    """
    if "" not in tree:
        raise ValueError("tree must have a root ('' ) entry")
    order = [""]

    def full(path: str, child: str) -> str:
        return f"{path}/{child}" if path else child

    def visit(path: str) -> None:
        for child in tree[path]["dirs"]:
            order.append(full(path, child))
            visit(full(path, child))

    visit("")
    dir_ids = {path: 0xF000 + index for index, path in enumerate(order)}

    # ---- FNT subtable bodies (child IDs are zero until the offsets exist)
    subtables: dict[str, bytearray] = {}
    dir_refs: dict[tuple[str, str], int] = {}     # offset within the subtable
    name_offsets: dict[tuple[str, str], int] = {}
    first_ids: dict[str, int] = {}
    for path in order:
        body = bytearray()
        files = tree[path]["files"]
        first_ids[path] = files[0][1] if files else 0
        for name, _file_id in files:
            encoded = name.encode("ascii")
            body.append(len(encoded))
            name_offsets[(path, name)] = len(body)
            body += encoded
        for child in tree[path]["dirs"]:
            encoded = child.encode("ascii")
            body.append(0x80 | len(encoded))
            name_offsets[(path, child)] = len(body)
            body += encoded
            dir_refs[(path, child)] = len(body)
            body += b"\0\0"
        body.append(0)
        subtables[path] = body

    dir_table_size = 8 * len(order)
    offsets: dict[str, int] = {}
    cursor = dir_table_size
    for path in order:
        offsets[path] = cursor
        cursor += len(subtables[path])
    fnt_size = cursor
    fnt = bytearray(fnt_size)
    for index, path in enumerate(order):
        struct.pack_into("<IHH", fnt, index * 8, offsets[path],
                         first_ids[path], dir_ids[order[index - 1]] if index else len(order))
    for path in order:
        start = offsets[path]
        fnt[start:start + len(subtables[path])] = subtables[path]
        for child in tree[path]["dirs"]:
            struct.pack_into("<H", fnt, start + dir_refs[(path, child)],
                             dir_ids[full(path, child)])

    # ---- data and FAT
    data: dict[int, bytes] = {index: f"ovl{index}".encode() for index in range(overlay_count)}
    for path in order:
        for name, file_id in tree[path]["files"]:
            if file_id in data:
                raise ValueError(f"duplicate file ID {file_id}")
            data[file_id] = f"data:{path}/{name}".encode()
    count = max(data) + 1 if data else 0
    if count == 0:
        raise ValueError("an image needs at least one file")

    fnt_offset = HEADER_SIZE
    fat_offset = _align(fnt_offset + fnt_size, 4)
    overlay_offset = _align(fat_offset + count * 8, 4)
    overlay_size = overlay_count * 32
    data_offset = _align(overlay_offset + overlay_size, 0x200)

    if rom_size < data_offset:
        raise ValueError("rom_size is too small for the tables")

    image = bytearray(rom_size)
    image[0:len(title)] = title
    image[0x0C:0x10] = game_code
    image[0x10:0x12] = b"01"
    image[0x12] = unit_code
    image[0x14] = (rom_size.bit_length() - 1) - 17 if device_size is None else device_size
    if rom_size % 0x200:
        raise ValueError("rom_size must be a multiple of 0x200")
    # Plausible ARM9/ARM7 extents (inside the header, unused by the tests).
    struct.pack_into("<IIII", image, 0x20, 0x200, 0x02000000, 0x02000000, 0x100)
    struct.pack_into("<IIII", image, 0x30, 0x300, 0x02380000, 0x02380000, 0x100)
    struct.pack_into("<IIII", image, 0x40, fnt_offset, fnt_size,
                     fat_offset, count * 8)
    struct.pack_into("<II", image, 0x50, overlay_offset, overlay_size)

    image[fnt_offset:fnt_offset + fnt_size] = fnt

    extents: dict[int, tuple[int, int]] = {}
    cursor = data_offset
    for file_id in range(count):
        payload = data.get(file_id, b"")
        extents[file_id] = (cursor, cursor + len(payload))
        image[cursor:cursor + len(payload)] = payload
        cursor = _align(cursor + len(payload), 4)
    for file_id in range(count):
        start, end = extents[file_id]
        struct.pack_into("<II", image, fat_offset + file_id * 8, start, end)

    for index in range(overlay_count):
        payload = data[index]
        struct.pack_into("<IIIIIIII", image, overlay_offset + index * 32,
                         index, 0x02000000, len(payload), 0, 0, 0, index, 0)

    meta = {
        "fnt_offset": fnt_offset,
        "fnt_size": fnt_size,
        "fat_offset": fat_offset,
        "overlay_offset": overlay_offset,
        "overlay_size": overlay_size,
        "data_offset": data_offset,
        "extents": extents,
        "dir_ids": dir_ids,
        "dir_entry_offsets": {path: fnt_offset + index * 8
                              for index, path in enumerate(order)},
        "dir_refs": {(path, child): fnt_offset + offsets[path] + at
                     for (path, child), at in dir_refs.items()},
        "name_offsets": {(path, name): fnt_offset + offsets[path] + at
                         for (path, name), at in name_offsets.items()},
    }
    return bytes(image), meta


def default_tree() -> dict:
    """root: a.bin (ID 2), d/; d: sub/; d/sub: b.bin (ID 3)."""
    return {
        "": {"files": [("a.bin", 2)], "dirs": ["d"]},
        "d": {"files": [], "dirs": ["sub"]},
        "d/sub": {"files": [("b.bin", 3)], "dirs": []},
    }


def synthetic_nds(*, overlay_count: int = 2, **kwargs) -> tuple[bytes, dict]:
    """The default two-level tree plus two synthetic overlays (IDs 0 and 1)."""
    return build_nds(default_tree(), overlay_count=overlay_count, **kwargs)
