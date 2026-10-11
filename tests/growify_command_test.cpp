// The "growify" cheat's command and building filter.
#include "core/growify_command.h"

#include <cstdint>
#include <cstdio>

using namespace opensc4;
using growify_command::Command;
using growify_command::Kind;

namespace {

int Fail(const char* what)
{
    std::printf("growify command test FAILED: %s\n", what);
    return 1;
}

bool Parses(const char* text, Kind kind, int32_t zone, bool historical)
{
    Command command;
    return growify_command::Parse(text, &command).empty() && command.kind == kind && command.zone == zone &&
           command.historical == historical;
}

bool Rejects(const char* text)
{
    Command command;
    command.zone = 99;
    return !growify_command::Parse(text, &command).empty() && command.zone == 99;
}

} // namespace

int main()
{
    if (!Parses("growify Residential Low", Kind::Residential, 1, true) ||
        !Parses("Growify res med", Kind::Residential, 2, true) || !Parses("growify R H", Kind::Residential, 3, true) ||
        !Parses("growify Commercial Low", Kind::Commercial, 4, true) ||
        !Parses("growify c high", Kind::Commercial, 6, true) || !Parses("growify Agriculture", Kind::Agriculture, 7, true) ||
        !Parses("growify Ag Low", Kind::Agriculture, 7, true) || !Parses("growify I M", Kind::Industrial, 8, true) ||
        !Parses("growify Industrial High", Kind::Industrial, 9, true))
        return Fail("zones");
    if (!Parses("growify R M false", Kind::Residential, 2, false) ||
        !Parses("growify R M 0", Kind::Residential, 2, false) || !Parses("growify R M TRUE", Kind::Residential, 2, true) ||
        !Parses("growify   C   L   1", Kind::Commercial, 4, true))
        return Fail("historical");
    if (!Rejects("growify") || !Rejects("growify Residential") || !Rejects("growify X Low") ||
        !Rejects("growify R Zero") || !Rejects("growify I Low") || !Rejects("growify R M maybe") ||
        !Rejects("growify R M true extra"))
        return Fail("invalid commands leave the command alone");
    if (!growify_command::Matches(Kind::Residential, 1) || !growify_command::Matches(Kind::Commercial, 2) ||
        !growify_command::Matches(Kind::Commercial, 3) || !growify_command::Matches(Kind::Agriculture, 5) ||
        !growify_command::Matches(Kind::Industrial, 6) || !growify_command::Matches(Kind::Industrial, 7) ||
        !growify_command::Matches(Kind::Industrial, 8))
        return Fail("purposes that match");
    for (uint32_t purpose : {0u, 4u, 9u, 10u})
        for (Kind kind : {Kind::Residential, Kind::Commercial, Kind::Agriculture, Kind::Industrial})
            if (growify_command::Matches(kind, purpose))
                return Fail("none, tourism and other never match");
    if (growify_command::Matches(Kind::Residential, 2) || growify_command::Matches(Kind::Industrial, 5))
        return Fail("purposes of another kind");
    std::printf("growify command test OK\n");
    return 0;
}
