"""Save decoder of the city's water pipe pieces (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .common import Reader, header
from .netocc import corner, network


def vector(r):
    """The intermediate class's vectors of 0x18-byte records (0x0065B070
    writes one, 0x006574A0 reads one): a uint32 count, then per record the
    same fields in the same order as a network occupant's corner."""
    return [corner(r) for _ in range(r.u32())]


def network_record(r):
    """The network occupant's record (src/core/netocc_life.cpp Write) that
    follows the two versions: tools/re/save_schemas/netocc.py decodes a whole
    object, with its own header and a size check, so it is given a header
    made here. Its size is found by a first pass, which ends in the check's
    error with the reader at the end of the record."""
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


def pipe(r):
    """cSC4PipeOccupant (0x49C05B9F): Write at 0x00648800 (version 3), Read
    at 0x00648840 (version 3 only); both get the object + 8. After the
    version comes the intermediate class's record (Write 0x0065B140, Read
    0x0065E510: its own version 3, the network occupant's whole record
    (0x0061A190), then its fields), and last the pipe's one word. The
    intermediate class (code 0x00652000 to 0x0065D000, vtable 0x00AAAD40) is
    also the base of another class and is not described here; its fields are
    named by their object offsets (the offsets in Write are from the
    object + 8). Names give the object offsets (src/core/pipeocc_sim.h)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 3:
        raise struct.error('version %d not known' % v)
    out['base_version'] = b = r.u16()
    if b != 3:
        raise struct.error('intermediate version %d not known' % b)
    out['network'] = network_record(r)
    # Two bytes of the model state (+0x50, +0x51) and a float (+0x60).
    out['state_50'] = [r.u8(), r.u8()]
    out['float_60'] = r.f32()
    out['word_17c'] = r.u32()
    # Six vectors, the objects' +0x180, +0x18C, +0x198, +0x1A4, +0x1B4, +0x1C0.
    out['vectors_180'] = [vector(r) for _ in range(6)]
    # Sixteen floats (a 4 by 4 matrix, +0x1FC), then two signed words
    # (+0x23C, +0x240), three bytes (+0x248..), four floats (+0x24C..), a
    # float (+0x25C), two words (+0x264, +0x268).
    out['matrix_1fc'] = [r.f32() for _ in range(16)]
    out['signed_23c'] = [r.s32(), r.s32()]
    out['bytes_248'] = [r.u8(), r.u8(), r.u8()]
    out['floats_24c'] = [r.f32() for _ in range(4)]
    out['float_25c'] = r.f32()
    out['words_264'] = [r.u32(), r.u32()]
    # The pipe's own field: 1 for a pipe that is watered.
    out['watered_270'] = r.u32()
    return out


# One record holds every pipe, one after the other.
SCHEMAS = {0x49C05B9F: ('cSC4PipeOccupant', every(pipe))}
