"""Save decoder of the occupant manager (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header

# The four automaton classes (registered together at 0x00514A00; occupant
# types 0xC7CC75F1 and 0x496E5783: vehicles, pedestrians, animals). They move
# by frames, and their saved number and cells differed between runs of the
# original in the parity batches.
AUTOMATA = {0x896E714A, 0x4A5B69ED, 0xCBB32B0A, 0x896E75AF}


def occupant_manager(r):
    """cSC4OccupantManager (0x098F964D): Write at 0x0049ECF0 (version 1),
    Read at 0x004A1A20 (version 1 only; both get the object + 4). Names give
    the object offsets (src/core/occmgr_sim.h). The grid +0x38 is saved
    whole: every column (192) and every cell of it (192) with the number of
    its entries and the occupants as references, in the list's order (front
    to back: that order is game state). An occupant lying in several cells
    is in each of them. Only the cells with entries are listed here, as
    [x, z, [occupants]] in the saved order (x then z), without the automata."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    out['size_1c'] = [r.f32(), r.f32()]
    out['cells_24'] = [r.s32(), r.s32()]
    out['city_cells_2c'] = [r.s32(), r.s32()]
    out['columns'] = columns = r.u32()
    if columns > 4096:
        raise struct.error('%d columns' % columns)
    rows = []
    cells = []
    entries = 0
    automata = 0
    for x in range(columns):
        depth = r.u32()
        if depth > 4096:
            raise struct.error('%d cells in column %d' % (depth, x))
        rows.append(depth)
        for z in range(depth):
            count = r.u32()
            if count:
                # Automata (vehicles, pedestrians) move by
                # frames, not game days: their number and cells differ
                # between two runs of the original (docs/occmgr.md). Counted
                # apart, left out of the lists.
                refs = [r.ref() for _ in range(count)]
                kept = [ref for ref in refs if ref[1] not in AUTOMATA]
                automata += len(refs) - len(kept)
                if kept:
                    cells.append([x, z, kept])
                entries += len(kept)
    # Every column has the same number of cells in the game's saves: one
    # number then, the list otherwise.
    out['rows'] = rows[0] if rows and all(d == rows[0] for d in rows) else rows
    out['entries'] = entries
    # The record's size follows the number of automata: not compared.
    out['size'] = None
    out['automata'] = None if automata else 0
    out['cells_38'] = cells
    return out


SCHEMAS = {0x098F964D: ('cSC4OccupantManager', occupant_manager)}
