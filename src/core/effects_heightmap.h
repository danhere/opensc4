// The particles' height map, written by hand from the game's code
// 0x00585AD0 to 0x00586AB0 (vtable 0x00A9F008; the effects manager makes
// one at its +0xD98 and is built on one at its +0xC). Included by
// water_effects.cpp.
//
// The object, 0x88 bytes: +0 its table, +4 the counted base (table
// 0x00A9EFF0, the count at +8), +0xC the terrain it holds, +0x10 the
// terrain's sea level (its slot 0x74), +0x14 a vector of layers, each a
// vector of floats, one per vertex of the terrain's grid; +0x20 and +0x24
// the cell counts (slots 0x1C and 0x20), +0x28 and +0x2C one more of each
// (the vertices in a row and a column), +0x30 the cell size (slot 0x18,
// truncated), +0x34 its inverse, +0x38 to +0x44 the grid's bounds as a
// cell rectangle (0, 0, the counts), +0x48 set when a layer changes, +0x4C
// a 0x3C-byte list of lines it draws itself with (the game's).
//
// The x87 runs at 53 bits: intermediates are doubles, rounded to float
// where the game stores a float, in the game's order; a value the game
// answers on the x87 stack is answered as a double.

constexpr std::size_t kHeightMapSize = 0x88;
constexpr uint32_t kHeightMapId = 0x0A5EAA1A;

// What ours deletes with: in compare mode the deletion is only noted.
void (*g_hmDelete)(void*) = nullptr;

template <typename R, typename... Args>
R HmSlot(char* object, uint32_t slot, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(At<uint32_t>(At<char*>(object, 0), slot))(object,
                                                                                                         args...);
}

// An integer as the x87 loads it for these: as unsigned (fild, and 2^32
// added when negative).
double HmUnsigned(uint32_t value)
{
    return static_cast<double>(value);
}

// The game's floor: v - 0.5 rounded to the nearest through 1.5 x 2^23
// (0x00A9EFAC) and a float, then one more when that is v itself.
uint32_t HmFloor(double value)
{
    const float rounded = static_cast<float>(value - 0.5 + 12582912.0);
    uint32_t bits = 0;
    std::memcpy(&bits, &rounded, 4);
    const uint32_t floor = bits + 0xB4C00000u;
    return value == static_cast<double>(static_cast<int32_t>(floor + 1)) ? floor + 1 : floor;
}

uint32_t HmLayers(char* self)
{
    return static_cast<uint32_t>((At<char*>(self, 0x18) - At<char*>(self, 0x14)) / 0xC);
}

float* HmLayer(char* self, uint32_t index)
{
    return At<float*>(At<char*>(self, 0x14) + index * 0xC, 0);
}

double HmCell(char* self, uint32_t layer, uint32_t x, uint32_t z)
{
    return HmSlot<double>(self, 0x1C, layer, x, z);
}

// Slot 0 (0x00585AD0): for 0x0A5EAA1A the map itself, held (slot 1); any
// other id is the counted base's (0x009457C6, on +4).
uint32_t QueryHeightMap(char* self, uint32_t id, void** out)
{
    if (id == kHeightMapId) {
        *out = self;
        HmSlot<uint32_t>(self, 4);
        return 1;
    }
    return CallThis<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self + 4, id, out);
}

// Slot 7 (0x00586030): a vertex's height. The terrain's (its slot 0x38,
// kept as a float) plus the first layer's value there; under the sea, the
// higher of the terrain and the sea level plus the value less 50. The
// layer asked for is not looked at.
double HeightMapCell(char* self, uint32_t layer, uint32_t x, uint32_t z)
{
    (void)layer;
    const float ground = static_cast<float>(HmSlot<double>(At<char*>(self, 0xC), 0x38, x, z));
    const float value = HmLayer(self, 0)[At<uint32_t>(self, 0x28) * z + x];
    if (!(ground < At<float>(self, 0x10)))
        return static_cast<double>(value) + ground;
    const double raised = static_cast<double>(value) + At<float>(self, 0x10) - 50.0;
    return raised < ground ? static_cast<double>(ground) : static_cast<double>(static_cast<float>(raised));
}

// Slot 6 (0x00585B00): the height at a place, from the four vertices
// around it (slot 7). As the game has it, the weight along z is the place's
// fraction in x and the other way round; the two values at x are kept as
// floats, the two at x + 1 are not.
double HeightMapHeight(char* self, uint32_t layer, float x, float z)
{
    const double inverse = At<float>(self, 0x34);
    const double cx = static_cast<double>(x) * inverse;
    const float cz = static_cast<float>(static_cast<double>(z) * inverse);
    const uint32_t ix = HmFloor(cx), iz = HmFloor(cz);
    const float fx = static_cast<float>(cx - HmUnsigned(ix));
    const float h00 = static_cast<float>(HmCell(self, layer, ix, iz));
    const double h01 = HmCell(self, layer, ix, iz + 1);
    const float atX = static_cast<float>((h01 - h00) * fx + h00);
    const float h10 = static_cast<float>(HmCell(self, layer, ix + 1, iz));
    const double h11 = HmCell(self, layer, ix + 1, iz + 1);
    const double atX1 = (h11 - h10) * fx + h10;
    return (atX1 - atX) * (static_cast<double>(cz) - HmUnsigned(iz)) + atX;
}

// Slot 8 (0x005860A0): the ground's normal at a place, into out (three
// floats), answering out. Slopes in z and x from sixteen vertex heights
// (slot 7) of the cells around the place, their indices kept on the grid;
// the normal (-dx k, 1, -dz k) with k half the inverse cell size, scaled
// by one over its length.
float* HeightMapNormal(char* self, float* out, uint32_t layer, float x, float z)
{
    const double inverse = At<float>(self, 0x34);
    const double cx = static_cast<double>(x) * inverse, cz = static_cast<double>(z) * inverse;
    const uint32_t ix = HmFloor(cx), iz = HmFloor(cz);
    const float fx = static_cast<float>(cx - HmUnsigned(ix));
    const float fz = static_cast<float>(cz - HmUnsigned(iz));
    const int32_t width = At<int32_t>(self, 0x20), depth = At<int32_t>(self, 0x24);
    const auto low = [](uint32_t value) { return static_cast<int32_t>(value - 1) > 0 ? value - 1 : 0u; };
    const auto high = [](uint32_t value, int32_t limit) {
        return static_cast<int32_t>(value) < limit ? value : static_cast<uint32_t>(limit);
    };
    const uint32_t x0 = low(ix), x2 = high(ix + 1, width), x3 = high(ix + 2, width);
    const uint32_t z0 = low(iz), z2 = high(iz + 1, depth), z3 = high(iz + 2, depth);

    const float a0 = static_cast<float>(HmCell(self, layer, ix, z0));
    const float a1 = static_cast<float>(HmCell(self, layer, ix, iz));
    const double a2 = HmCell(self, layer, ix, z2);
    const float a2f = static_cast<float>(a2);
    const float da = static_cast<float>(a2 - a0);
    const double a3 = HmCell(self, layer, ix, z3);
    const float slopeZ0 = static_cast<float>(((a3 - a1) - da) * fz + da);
    const float b0 = static_cast<float>(HmCell(self, layer, x2, z0));
    const float b1 = static_cast<float>(HmCell(self, layer, x2, iz));
    const double b2 = HmCell(self, layer, x2, z2);
    const float b2f = static_cast<float>(b2);
    const float db = static_cast<float>(b2 - b0);
    const double b3 = HmCell(self, layer, x2, z3);
    const double slopeZ1 = ((b3 - b1) - db) * fz + db;
    const float slopeZ = static_cast<float>((slopeZ1 - slopeZ0) * fx + slopeZ0);

    const double c0 = HmCell(self, layer, x0, iz);
    const float dc = static_cast<float>(b1 - c0);
    const double c1 = HmCell(self, layer, x3, iz);
    const float slopeX0 = static_cast<float>(((c1 - a1) - dc) * fx + dc);
    const double c2 = HmCell(self, layer, x0, z2);
    const float dd = static_cast<float>(b2f - c2);
    const double c3 = HmCell(self, layer, x3, z2);
    const double slopeX1 = ((c3 - a2f) - dd) * fx + dd;
    const double slopeX = (slopeX1 - slopeX0) * fz + slopeX0;

    const double k = static_cast<double>(At<float>(self, 0x34)) * 0.5;
    const float nx = static_cast<float>(-slopeX * k);
    const float nz = static_cast<float>(-static_cast<double>(slopeZ) * k);
    const float scale = static_cast<float>(
        1.0 / std::sqrt(static_cast<double>(nz) * nz + static_cast<double>(nx) * nx + 1.0));
    out[0] = static_cast<float>(static_cast<double>(nx) * scale);
    out[1] = scale;
    out[2] = static_cast<float>(static_cast<double>(nz) * scale);
    return out;
}

// The rectangle (x0, z0, x1, z1) cut to the grid's bounds (the game's
// SC4Rect::Intersect, in place); false when it is off the grid.
bool HmCut(char* self, int32_t* box)
{
    return CallThis<uint8_t>(addr::SC4Rect_Intersect, box, box, self + 0x38) != 0;
}

// The three changes of a layer (1 the first; another is left alone):
// count + 1 rings, from the rectangle out, each cut to the grid and one
// wider on every side than the one before; then +0x48 set.
template <typename Ring, typename Cell>
void HmRings(char* self, uint32_t layer, const int32_t* rect, uint32_t rings, Ring&& ring, Cell&& cell)
{
    const uint32_t index = layer - 1;
    if (index >= HmLayers(self))
        return;
    int32_t box[4] = {rect[0], rect[1], rect[2], rect[3]};
    do {
        if (HmCut(self, box)) {
            float* const values = HmLayer(self, index);
            for (int32_t z = box[1]; z <= box[3]; ++z) {
                for (int32_t x = box[0]; x <= box[2]; ++x)
                    cell(values[At<uint32_t>(self, 0x28) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x)]);
            }
        }
        ring();
        --box[0];
        --box[1];
        ++box[2];
        ++box[3];
    } while (--rings != 0);
    At<uint8_t>(self, 0x48) = 1;
}

// Slot 9 (0x005863D0): amount over count + 1 added in each ring, so the
// middle gets all of it.
void HeightMapRaise(char* self, uint32_t layer, const int32_t* rect, float amount, uint32_t count)
{
    const uint32_t rings = count + 1;
    const float step = static_cast<float>(static_cast<double>(amount) / HmUnsigned(rings));
    HmRings(self, layer, rect, rings, [] {}, [&](float& value) { value = static_cast<float>(static_cast<double>(step) + value); });
}

// Slot 10 (0x005864F0): a value below height raised to it, the height
// times 1 - 1 / (count + 1) for each ring further out.
void HeightMapLift(char* self, uint32_t layer, const int32_t* rect, float height, uint32_t count)
{
    const float factor = static_cast<float>(1.0 - 1.0 / (HmUnsigned(count) + 1.0));
    HmRings(
        self, layer, rect, count + 1, [&] { height = static_cast<float>(static_cast<double>(factor) * height); },
        [&](float& value) {
            if (value < height)
                value = height;
        });
}

// Slot 11 (0x00586640): the rectangle's values cleared, one ring.
void HeightMapClear(char* self, uint32_t layer, const int32_t* rect)
{
    HmRings(self, layer, rect, 1, [] {}, [](float& value) { value = 0.0f; });
}

// The constructor (0x00586890): the counted base made (0x0090DA1E), the two
// tables, no terrain, no layers, the line list made (0x008012F0). The
// game's first puts a table of 0x00A9EFB4 and its base's at +0 and +4; only
// the final ones are written here.
char* ConstructHeightMap(char* self)
{
    CallThis<void*>(addr::cRZBaseUnknown_Construct, self + 4);
    At<uint32_t>(self, 0) = 0x00A9F008;
    At<uint32_t>(self, 4) = 0x00A9EFF0;
    At<uint32_t>(self, 0xC) = 0;
    At<uint32_t>(self, 0x14) = 0;
    At<uint32_t>(self, 0x18) = 0;
    At<uint32_t>(self, 0x1C) = 0;
    At<uint8_t>(self, 0x48) = 0;
    CallThis<void*>(addr::SC4HeightMapLines_Construct, self + 0x4C);
    return self;
}

// The deleting destructor (0x00586940, the base's slot 3 through
// 0x005868F0): the tables put back, the line list ended (0x008011F0), the
// layers freed (0x004638B0), the terrain let go (slot 2; the pointer is
// left), the base ended (0x0090D990), and with flag 1 the object deleted.
char* DestroyHeightMap(char* self, uint32_t flags)
{
    At<uint32_t>(self, 0) = 0x00A9F008;
    At<uint32_t>(self, 4) = 0x00A9EFF0;
    CallThis<void>(addr::SC4HeightMapLines_Destroy, self + 0x4C);
    CallThis<void>(addr::SC4ShortVectorVector_Destroy, self + 0x14);
    if (char* const terrain = At<char*>(self, 0xC))
        HmSlot<uint32_t>(terrain, 8);
    CallThis<void>(addr::cRZBaseUnknown_Destruct, self + 4);
    if (flags & 1)
        g_hmDelete(self);
    return self;
}

// The shutdown (0x00586990): the terrain let go and cleared, the layers
// erased (0x004643B0; their storage kept). Answers true.
uint32_t ShutdownHeightMap(char* self)
{
    if (char* const terrain = At<char*>(self, 0xC)) {
        At<char*>(self, 0xC) = nullptr;
        HmSlot<uint32_t>(terrain, 8);
    }
    CallThis<char*>(addr::SC4FloatVectorVector_Erase, self + 0x14, At<char*>(self, 0x14), At<char*>(self, 0x18));
    return 1;
}

// The init (0x005869C0), with no layers yet: the terrain at 0x00B43CF8
// held (the one before let go), its sea level, cell counts and cell size
// (truncated, as _ftol2) read, the bounds set, and one layer of zeros, a
// value for each vertex. Answers true.
uint32_t InitHeightMap(char* self)
{
    if (At<char*>(self, 0x14) != At<char*>(self, 0x18))
        return 1;
    char* const terrain = M<char*>(addr::SC4_RegionTerrain);
    char* const before = At<char*>(self, 0xC);
    if (terrain != before) {
        if (terrain)
            HmSlot<uint32_t>(terrain, 4);
        At<char*>(self, 0xC) = terrain;
        if (before)
            HmSlot<uint32_t>(before, 8);
    }
    char* const held = At<char*>(self, 0xC);
    At<float>(self, 0x10) = static_cast<float>(HmSlot<double>(held, 0x74));
    At<uint32_t>(self, 0x20) = HmSlot<uint32_t>(held, 0x1C);
    At<uint32_t>(self, 0x24) = HmSlot<uint32_t>(held, 0x20);
    const uint32_t size = static_cast<uint32_t>(static_cast<int64_t>(HmSlot<double>(held, 0x18)));
    At<uint32_t>(self, 0x30) = size;
    At<uint32_t>(self, 0x28) = At<uint32_t>(self, 0x20) + 1;
    At<uint32_t>(self, 0x2C) = At<uint32_t>(self, 0x24) + 1;
    At<float>(self, 0x34) = static_cast<float>(1.0 / HmUnsigned(size));
    At<uint32_t>(self, 0x38) = 0;
    At<uint32_t>(self, 0x3C) = 0;
    At<uint32_t>(self, 0x40) = At<uint32_t>(self, 0x20);
    At<uint32_t>(self, 0x44) = At<uint32_t>(self, 0x24);
    char* empty[3] = {};
    CallThis<void>(addr::SC4FloatVectorVector_Resize, self + 0x14, 1u, empty);
    const float zero = 0.0f;
    CallThis<void>(addr::SC4FloatVector_Resize2, At<char*>(self, 0x14),
                   At<uint32_t>(self, 0x2C) * At<uint32_t>(self, 0x28), &zero);
    return 1;
}
