// interface_counter: thunks forward to the wrapped object with the right
// `this`, arguments and return values (integer, float, 64-bit), count calls by
// slot, and reference counting releases the wrapped object.
#include "core/interface_counter.h"

#include <cstdint>
#include <cstdio>

namespace {

namespace counter = opensc4::interface_counter;

constexpr uint32_t kIid = 0x12345678;

struct Fake {
    virtual bool QueryInterface(uint32_t iid, void** object)
    {
        if (iid == kIid || iid == 1) {
            *object = this;
            AddRef();
            return true;
        }
        return false;
    }
    virtual uint32_t AddRef() { return ++refs; }
    virtual uint32_t Release()
    {
        if (--refs == 0)
            released = true;
        return refs;
    }
    virtual int Add(int a, int b) { return a + b + base; }                // slot 3
    virtual float Scale(float x) { return x * static_cast<float>(base); } // slot 4
    virtual int64_t Wide(int32_t hi) { return (static_cast<int64_t>(hi) << 32) | base; } // slot 5
    virtual void Store(int v, int* out) { *out = v + base; }              // slot 6

    int base = 100;
    uint32_t refs = 1;
    bool released = false;
};

int Fail(const char* what)
{
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

} // namespace

int main()
{
    Fake fake;
    counter::Counters counters{"fake", kIid, 7, {}};
    auto* wrapped = static_cast<Fake*>(counter::Wrap(&fake, counters));
    if (wrapped == &fake)
        return Fail("not wrapped");

    if (wrapped->Add(2, 3) != 105)
        return Fail("integer arguments or this");
    if (wrapped->Scale(1.5f) != 150.0f)
        return Fail("float return");
    if (wrapped->Wide(7) != ((int64_t{7} << 32) | 100))
        return Fail("64-bit return");
    int out = 0;
    wrapped->Store(5, &out);
    wrapped->Store(6, &out);
    if (out != 106)
        return Fail("void method with pointer argument");

    if (counters.calls[3] != 1 || counters.calls[4] != 1 || counters.calls[5] != 1 || counters.calls[6] != 2)
        return Fail("per-slot counts");

    void* again = nullptr;
    if (!wrapped->QueryInterface(kIid, &again) || again != wrapped)
        return Fail("QueryInterface for the wrapped IID should return the wrapper");
    if (wrapped->QueryInterface(0x99, &again))
        return Fail("unknown IIDs should be forwarded (and refused by the fake)");

    wrapped->Release(); // the QueryInterface reference
    if (fake.released)
        return Fail("released too early");
    wrapped->Release(); // the wrapper's own reference
    if (!fake.released)
        return Fail("the wrapped object should be released with the wrapper");

    counter::Log(counters);
    std::puts("interface counter test OK");
    return 0;
}
