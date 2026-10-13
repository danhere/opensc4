"""Save decoder of the city's network pieces drawn from a pre-built model
(tools/re/sc4save.py). SCHEMAS maps class IDs to (name, decoder).

The bridge and tunnel occupants derive from this class and their records
hold this class's: prebuilt_record(r) decodes that part (from this class's
version to its last field) for their decoders."""
import struct

from .budget import every
from .common import Reader, header
from .netocc import network


def network_record(r):
    """The network occupant's record (src/core/netocc_life.cpp Write) that
    follows the version: tools/re/save_schemas/netocc.py decodes a whole
    object, with its own header and a size check, so it is given a header
    made here. Its size is found by a first pass, which ends in the check's
    error with the reader at the end of the record (as in
    tools/re/save_schemas/pipeocc.py)."""
    start = r.pos

    def sub(size):
        rest = Reader(struct.pack('<III', size, 0, 0) + r.data[start:])
        rest.pos = 0
        return rest

    probe = sub(0)
    try:
        network(probe)
    except struct.error as error:
        if not str(error).startswith('network occupant used'):
            raise
    used = probe.pos - 12
    fields = network(sub(used + 12))
    del fields['size'], fields['crc'], fields['id']
    r.pos = start + used
    return fields


def prebuilt_fields(r, v):
    """The class's own fields, after the network occupant's record, for the
    class's version v (3 or 4). Names give the object offsets
    (src/core/prebuiltocc_sim.h; the offsets in Write and Read are from the
    object + 8)."""
    # Where the model stands from the piece's position: x, y, z.
    out = {'model_offset_178': [r.f32(), r.f32(), r.f32()]}
    if v < 4:
        # Six uint32 that Read drops (each into the same local).
        out['dropped_v3'] = [r.u32() for _ in range(6)]
    # A piece ID and a rotation and flip byte that stand in for the
    # object's own when the piece's effects are looked up (0: none).
    out['piece_override_1b0'] = r.u32()
    out['rotation_override_1b4'] = r.u8()
    # The support pylon's model (0 for none); with one, its angle and where
    # it stands from the model: x, y, z.
    out['pylon_19c'] = pylon = r.u32()
    if pylon:
        out['pylon_angle_1a0'] = r.f32()
        out['pylon_offset_1a4'] = [r.f32(), r.f32(), r.f32()]
    if v < 4:
        out['dropped_v3_last'] = r.u32()
    return out


def prebuilt_record(r):
    """The whole of what this class's Write (0x0061D8E0) puts in a record:
    its version (4; Read 0x0061D9B0 takes 3 and 4), the network occupant's
    record (0x0061A190) and its own fields. For the decoders of the derived
    classes, whose Writes call this one."""
    out = {'version': r.u16()}
    v = out['version']
    if not 3 <= v <= 4:
        raise struct.error('pre-built occupant version %d not known' % v)
    out['network'] = network_record(r)
    out.update(prebuilt_fields(r, v))
    return out


def prebuilt(r):
    """The pre-built-model network occupant (0x49C1A034): Write at
    0x0061D8E0 (version 4), Read at 0x0061D9B0 (versions 3 and 4); both get
    the object + 8."""
    start = r.pos
    out = header(r)
    out.update(prebuilt_record(r))
    if r.pos - start != out['size']:
        raise struct.error('pre-built occupant used %d bytes of %d' % (r.pos - start, out['size']))
    return out


# One record holds every object of the class, one after the other.
SCHEMAS = {0x49C1A034: ('cSC4PrebuiltNetworkOccupant', every(prebuilt))}
