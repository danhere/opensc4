"""What every save decoder uses: the record reader and the header the save
framework writes before each object's own fields."""
import struct


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, fmt):
        value = struct.unpack_from('<' + fmt, self.data, self.pos)
        self.pos += struct.calcsize('<' + fmt)
        return value[0] if len(value) == 1 else value

    def u8(self): return self.take('B')
    def s8(self): return self.take('b')
    def u16(self): return self.take('H')
    def s16(self): return self.take('h')
    def s32(self): return self.take('i')
    def u32(self): return self.take('I')
    def s64(self): return self.take('q')
    def f32(self): return self.take('f')
    def f64(self): return self.take('d')

    def ref(self):
        # A reference to another serialised object (0x005D99A0): its
        # address-ID when saved and its class ID; none is a single 0.
        address = self.u32()
        return (address, self.u32()) if address else (0, 0)

    def gzstr(self):
        # cIGZIStream::GetGZStr: uint32 length, then the characters.
        n = self.u32()
        text = self.data[self.pos:self.pos + n]
        if len(text) != n:
            raise struct.error('string runs past the record')
        self.pos += n
        return text.decode('latin-1')

    def ref_list(self):
        # 0x005DAFF0: count, then references.
        return [self.ref() for _ in range(self.u32())]


def header(r):
    return {'size': r.u32(), 'crc': r.u32(), 'id': r.u32()}
