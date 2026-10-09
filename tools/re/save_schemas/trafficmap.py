"""Save decoders of the traffic network map (tools/re/sc4save.py). SCHEMAS
maps class IDs to (name, decoder)."""
import json
import os
import struct

from .common import header


def _canonical(value):
    """An entry as the parity harness compares it (sc4save.comparable: a
    reference keeps whether it is set and its class), as a sort key."""
    def strip(v):
        if isinstance(v, tuple):
            return [v[0] != 0, v[1]]
        if isinstance(v, list):
            return [strip(x) for x in v]
        if isinstance(v, dict):
            return {k: strip(x) for k, x in v.items()}
        return v
    return json.dumps(strip(value), sort_keys=True)


def _f32(r):
    # A float; NaN (uninitialised approach times) as its bits, so that two
    # decodes of the same bytes compare equal.
    bits = r.u32()
    value = struct.unpack('<f', struct.pack('<I', bits))[0]
    return value if value == value else 'nan %08X' % bits


def _sorted(entries):
    # Hash tables keyed by heap pointers (or written in bucket order) come out
    # in an order that differs between runs: sort them by what is compared.
    return sorted(entries, key=_canonical)


def traffic_network_map(r):
    """cSC4TrafficNetworkMap (0x6A0F82B2): Write at 0x004B1CE0 (version 7),
    Read at 0x004B4DE0 (versions 3 to 7). Field names are ours; offsets are
    from the object's start (the cIGZSerializable part sits at +0xC). See
    docs/traffic-map.md. The signals, cell objects and lanes are hash tables
    written in bucket order: decoded as lists sorted by content."""
    out = header(r)
    out['version'] = v = r.u16()
    if not 3 <= v <= 7:
        raise struct.error('version %d not known' % v)
    out['cells'] = r.u32()  # width * height
    records = []
    for _ in range(r.u32()):
        rec = {'cell': r.u32(), 'occupant': r.ref()}
        lanes = []
        for _ in range(r.u32()):
            index = r.u32()
            lanes.append({'index': index, 'entries': [[r.u32(), _f32(r)] for _ in range(r.u32())]})
        rec['lanes'] = lanes  # +0x04, by travel type - 1
        rec['automata'] = r.ref_list()  # +0x1C
        rec['unused_bool'] = r.u8()
        rec['flags'] = [r.u32() for _ in range(3)]  # +0x20, +0x24, +0x28
        approach, turns = [], []
        for _ in range(4):
            approach.append(_f32(r))  # rounded to uint16 on load
            turns.append([r.u16() if v >= 4 else _f32(r) for _ in range(4)])
        rec['approach_traffic'] = approach  # +0x2C
        rec['turn_traffic'] = turns  # +0x34
        rec['unused_tail'] = r.u16() if v >= 4 else _f32(r)
        if v > 4:
            rec['paths'] = [[r.s32(), _f32(r), r.u16()] for _ in range(r.u32())]  # +0x10
        records.append(rec)
    out['records'] = records  # in cell order, each cell's list in order

    signals = []
    for _ in range(r.u32()):
        sig = {'occupant': r.ref()}
        if v > 6:
            sig['byte58'] = r.u8()
            sig['waiting'] = r.ref_list()  # +0x68
        sig['approaches'] = []
        for _ in range(4):
            kind, green, times = r.u32(), r.u8(), [_f32(r) for _ in range(3)]
            # A kind-0 approach (none) never reads its times; the game leaves
            # them uninitialised (they differ from run to run), so they are
            # not compared.
            sig['approaches'].append({'kind': kind, 'green': green, 'times': times if kind else None})
        signals.append(sig)
    out['signals'] = _sorted(signals)  # +0x28

    objects = []
    if v >= 2:
        for _ in range(r.u32()):
            key = r.u32()
            objects.append({'x': key >> 16, 'z': key & 0xFFFF, 'objects': r.ref_list()})
    out['cell_objects'] = _sorted(objects)  # +0xA70

    out['rect'] = [r.s32() for _ in range(4)]  # +0x60
    out['blink'] = [_f32(r), r.u8(), _f32(r), r.u8()]  # +0xA60 to +0xA6C
    lanes = []
    if v >= 6:
        for _ in range(r.u32()):
            lane = {'occupant': r.ref(), 'ways': []}
            for _ in range(4):
                values = [r.u32() for _ in range(r.u32())]
                index = r.u16()
                # The index of a way without values is never read; the game
                # leaves it uninitialised, so it is not compared.
                lane['ways'].append({'values': values, 'index': index if values else None})
            lanes.append(lane)
    out['lanes'] = _sorted(lanes)  # +0x40
    if os.environ.get('OPENSC4_TRAFFICMAP_FULL') == '1':
        return out
    return _frame_free(out)


def _frame_free(out):
    # The map runs on frame time (docs/traffic-map.md): its signal timers,
    # lane and path entries, automata lists, approach and turn traffic and
    # blink timers (and so the record's size) differ from run to run even with
    # the original game. By default only what does not is kept, so parity runs
    # compare that alone; OPENSC4_TRAFFICMAP_FULL=1 decodes everything (for
    # runs of 0 days).
    keep = {k: out[k] for k in ('crc', 'id', 'version', 'cells', 'rect') if k in out}
    keep['records'] = [{'cell': r['cell'], 'occupant': r['occupant']} for r in out['records']]
    keep['signals'] = _sorted([{'occupant': s['occupant']} for s in out['signals']])
    keep['cell_objects'] = out['cell_objects']
    keep['lanes'] = _sorted([{'occupant': l['occupant'], 'ways': [w['values'] for w in l['ways']]}
                             for l in out['lanes']])
    return keep


SCHEMAS = {0x6A0F82B2: ('cSC4TrafficNetworkMap', traffic_network_map)}
