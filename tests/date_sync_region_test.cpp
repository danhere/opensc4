// Which region a city file belongs to, for the date sync.
#include "core/date_sync_region.h"

#include <cstdio>

using namespace opensc4;

namespace {

int Fail(const char* what)
{
    std::printf("date sync region test FAILED: %s\n", what);
    return 1;
}

} // namespace

int main()
{
    const std::string london = date_sync_region::Of("C:\\SimCity 4\\Regions\\London\\City - Camden.sc4");
    if (london != "c:\\simcity 4\\regions\\london")
        return Fail("folder");
    if (date_sync_region::Of("c:/simcity 4/REGIONS/london/City - Hackney.sc4") != london)
        return Fail("same folder, other spelling");
    if (date_sync_region::Of("C:\\SimCity 4\\Regions\\Paris\\City - Camden.sc4") == london)
        return Fail("other region");
    if (!date_sync_region::Of("City - Camden.sc4").empty() || !date_sync_region::Of("").empty() ||
        !date_sync_region::Of("\\City.sc4").empty())
        return Fail("no folder");
    std::printf("date sync region test OK\n");
    return 0;
}
