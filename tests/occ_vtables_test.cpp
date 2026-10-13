// Vtable copies (core/occ_vtables.h): the copy and its leading word,
// Redirect's counts and effect, and what IsTable answers.
#include "core/occ_vtables.h"

#include <cstdint>
#include <cstdio>

using namespace opensc4;

namespace {

int Fail(const char* what)
{
    std::printf("occ vtables test FAILED: %s\n", what);
    return 1;
}

void* Pointer(std::uintptr_t value)
{
    return reinterpret_cast<void*>(value);
}

} // namespace

int main()
{
    // A word before, a table of five slots (two of them the same function)
    // and the first slot of the table after.
    const std::uintptr_t kFake[] = {0xAAAA0000u, 0x1000u, 0x2000u, 0x3000u, 0x2000u, 0x5000u, 0xBBBB0000u};
    std::uintptr_t fake[7];
    for (int i = 0; i < 7; ++i)
        fake[i] = kFake[i];
    const void* const table = fake + 1;
    const std::uintptr_t tableAddress = reinterpret_cast<std::uintptr_t>(table);

    occ_vtables::VtableCopy<5> copy;
    if (copy.Built())
        return Fail("a new copy is not built");
    if (occ_vtables::IsTable(copy.Address(), tableAddress))
        return Fail("a copy not built yet is nobody's table");
    copy.Build(table);
    if (!copy.Built())
        return Fail("Build marks the copy built");
    if (reinterpret_cast<std::uintptr_t>(copy.Table()) != copy.Address())
        return Fail("Table and Address are the same place");
    if (copy.Address() == tableAddress)
        return Fail("the copy is not the source");
    for (int i = 0; i < 5; ++i)
        if (copy.Table()[i] != Pointer(kFake[i + 1]))
            return Fail("every slot is copied");
    if (copy.Table()[-1] != Pointer(0xAAAA0000u))
        return Fail("the word before the table is copied");

    // Redirect: every slot that holds the function, and only those.
    if (copy.Redirect(Pointer(0x9000u), Pointer(0x7000u)) != 0)
        return Fail("a function the table does not hold changes nothing");
    if (copy.Redirect(Pointer(0xAAAA0000u), Pointer(0x7000u)) != 0 || copy.Table()[-1] != Pointer(0xAAAA0000u))
        return Fail("the word before the table is not a slot");
    if (copy.Redirect(Pointer(0xBBBB0000u), Pointer(0x7000u)) != 0)
        return Fail("the word after the table is not a slot");
    if (copy.Redirect(Pointer(0x2000u), Pointer(0x7000u)) != 2)
        return Fail("both slots that hold the function are counted");
    if (copy.Table()[1] != Pointer(0x7000u) || copy.Table()[3] != Pointer(0x7000u))
        return Fail("both slots are changed");
    if (copy.Table()[0] != Pointer(0x1000u) || copy.Table()[2] != Pointer(0x3000u) ||
        copy.Table()[4] != Pointer(0x5000u))
        return Fail("the other slots are left alone");
    if (copy.Redirect(Pointer(0x2000u), Pointer(0x8000u)) != 0)
        return Fail("a second Redirect of the same function finds nothing");
    if (copy.Redirect(Pointer(0x5000u), Pointer(0x8000u)) != 1 || copy.Table()[4] != Pointer(0x8000u))
        return Fail("the last slot is a slot");
    for (int i = 0; i < 7; ++i)
        if (fake[i] != kFake[i])
            return Fail("the source table is not modified");

    // RedirectAll: the total over several copies.
    occ_vtables::VtableCopy<5> second;
    occ_vtables::VtableCopy<1> third;
    second.Build(table);
    third.Build(fake + 2);
    if (third.Table()[0] != Pointer(0x2000u) || third.Table()[-1] != Pointer(0x1000u))
        return Fail("a table of one slot is copied with the word before it");
    if (occ_vtables::RedirectAll(Pointer(0x2000u), Pointer(0x7000u), second, third) != 3)
        return Fail("RedirectAll adds the copies' counts up");
    if (second.Table()[1] != Pointer(0x7000u) || second.Table()[3] != Pointer(0x7000u) ||
        third.Table()[0] != Pointer(0x7000u))
        return Fail("RedirectAll changes each copy");

    // IsTable: the game's table itself, or a copy of that table.
    if (!occ_vtables::IsTable(tableAddress, tableAddress))
        return Fail("the table is itself");
    if (!occ_vtables::IsTable(copy.Address(), tableAddress) || !occ_vtables::IsTable(second.Address(), tableAddress))
        return Fail("a copy of the table is the table");
    if (occ_vtables::IsTable(third.Address(), tableAddress))
        return Fail("a copy of another table is not the table");
    if (!occ_vtables::IsTable(third.Address(), reinterpret_cast<std::uintptr_t>(fake + 2)))
        return Fail("each copy knows its own source");
    if (occ_vtables::IsTable(reinterpret_cast<std::uintptr_t>(fake), tableAddress))
        return Fail("some other address is not the table");

    std::printf("occ vtables test OK\n");
    return 0;
}
