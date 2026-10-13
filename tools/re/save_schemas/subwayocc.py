"""Save decoder of the city's subway pieces (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder).

The Rush Hour Tutorial city has 16 subway pieces (class 0xCA16374F), so
every save made from it holds one record of 11,328 bytes: 14 objects of 696
bytes (piece key 0x4B00, two side walls of four corners) and 2 of 792 bytes
(the line's ends, piece key 0x300, three side walls). In the parity corpora
that is 64 objects in parity/saves (the four Rush Hour saves of the 12), 16
in parity/saves-networks (t0000) and none in parity/saves-plopped (Big City
only); all decode with every byte consumed. Big City and Making Money have
no record of the class."""
import struct

from .budget import every
from .common import header
from .pipeocc import network_record, vector


def intermediate(r, out):
    """The record of the intermediate class of the underground pieces (Write
    0x0065B140, Read 0x0065E510; the pipe's base too, as
    tools/re/save_schemas/pipeocc.py reads it inside the pipe's record): its
    own version 3, the network occupant's whole record (0x0061A190), then
    its fields. Names give the object offsets (src/core/subwayocc_sim.h)."""
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
    # float (+0x25C), two words (+0x264: the class ID Init was given, +0x268).
    out['matrix_1fc'] = [r.f32() for _ in range(16)]
    out['signed_23c'] = [r.s32(), r.s32()]
    out['bytes_248'] = [r.u8(), r.u8(), r.u8()]
    out['floats_24c'] = [r.f32() for _ in range(4)]
    out['float_25c'] = r.f32()
    out['words_264'] = [r.u32(), r.u32()]


def subway(r):
    """The subway occupant (0xCA16374F): Write at 0x00653010 (version 2),
    Read at 0x00653040 (version 2 only); both get the object + 8. After the
    version comes the intermediate class's record and nothing else: the
    class saves no field of its own (its four corner records at +0x270 are
    made again by slot 106, 0x00653130, when the path information is made).
    Write leaves the intermediate class's fields out when the network
    occupant's Write fails; Read then fails too."""
    start = r.pos
    out = header(r)
    out['version'] = v = r.u16()
    if v != 2:
        raise struct.error('version %d not known' % v)
    intermediate(r, out)
    if r.pos - start != out['size']:
        raise struct.error('subway occupant used %d bytes of %d' % (r.pos - start, out['size']))
    return out


# One record holds every subway piece, one after the other.
SCHEMAS = {0xCA16374F: ('cSC4SubwayOccupant', every(subway))}
