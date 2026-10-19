# sc4report: reports from save files

`tools/sc4report.py` reads save files without starting the game. Each report is a subcommand; the decoding comes from the save schemas in `tools/re/save_schemas` ([save-format.md](save-format.md)), so a record `sc4save.py --check` covers can feed a report.

## region: the region census

    python3 tools/sc4report.py region "<region folder>" [-o census.csv]

One CSV row per `City - *.sc4` file in the folder, from each file's regional city record (0xCA027EDB / 0xCA027EE1 / 0; [region.md](region.md), "The regional city"). That record is what the region view knows of a city without loading it, so the figures are as of the city's last save.

| Column | From |
|---|---|
| file, status | the file name; `ok`, `unfounded` (a tile with no city founded yet, which still has a file), or why the file could not be read or decoded |
| city, mayor | the names (+0xD0, +0xF8) |
| x, y, size_km | position in the region and size, km |
| established | 1 once the city is founded |
| residents, commercial_jobs, industrial_jobs | +0x24 to +0x2C |
| pop_R$ to pop_Ih | the population map (+0x3C) by type; `pop_other` lists any other types as `type=number` |
| mayor_rating, difficulty | +0x34, +0x38 |
| budget, income | +0x170, +0x178 |
| tax_0 to tax_11 | the twelve tax rates in the budget's tax group order (7.0 for groups the city does not have) |
| power, water, garbage produced and demanded | the commodity map (+0x180) |
| connections | the number of neighbour connections |

In real saves the record starts with a uint16 1 before the record's version; we take it to be the segment stream's own version (inferred: all 491 forum saves checked have it). Checked on the 491 GitHub forum saves (test-saves README), all of which decode: `commercial_jobs` is the sum of the five `pop_C*` types, `industrial_jobs` is `pop_Id + pop_Im + pop_Ih` plus a fifth of `pop_Ir` (farm jobs count one fifth), and `residents` is within a few percent of the three `pop_R*` types (it is figured apart). So the type IDs are right. The map also holds types 0x1001, 0x10x0, 0x2xxx and 0x3xx0 (in `pop_other`), meaning not known. Names are UTF-8. The tax columns are in the game's tax group order: 0 to 2 R$ to R$$$, 3 to 5 Cs$ to Cs$$$, 6 and 7 Co$$ and Co$$$, 8 to 11 agriculture, dirty, manufacturing and high-tech industry. Confirmed from the game: the table at 0x00AB0840 that CollectTaxes reads gives the group by building purpose (residence, services, offices, tourism as services, agriculture, processing, manufacturing, high tech) and wealth in that order, and the demand simulator pairs tax group n's rate figure (0xAABAAA9F + n) with the same RCI group's demand (src/core/demand_region.cpp).

Record versions 9 to 13 decode (13 is what the game writes, 9 and 10 are on unfounded stock tiles; before 12 there are no points of interest, before 13 no difficulty, which reads as 2). Version 9 has no workforce share (it reads as 0.5) and decodes exactly on all 252 forum files that have it; none older was seen.

## budget: funds, bonds, taxes and departments

    python3 tools/sc4report.py budget "<city.sc4 or folder>" [--departments] [-o budget.csv]

One CSV row per city file (a folder gives every `*.sc4` in it), from the budget record (class 0xE990BE01; [budget.md](budget.md)) and the department budget and line item records (0xE990BFFC, 0xAA313C9F). The figures are as of the city's last save.

| Column | From |
|---|---|
| file, status | the file name; `ok`, or why there is no row: `unreadable`, `no budget record`, `undecoded` (a record version the decoders do not know) |
| funds | the funds (+0x28) |
| bonds, bonds_owed, bond_payments | the number of bonds, the principal outstanding (+0x38), the sum of their monthly payments |
| tax_income | last month's tax income (+0x670) |
| department_expense, department_income | the sum over the departments of the `--departments` expense and income |
| tax_0 to tax_11 | the twelve tax groups' rates (the same order as the census's) |
| assessed_0 to assessed_11 | each tax group's assessed value (+0xE0) |

With `--departments`, one row per department budget instead, in the budget's order:

| Column | From |
|---|---|
| file | the file name |
| group | the budget group's name (from the budget's group records) |
| department | the department's name (+0x14), often empty: the game names most through text instance `name_instance` (+0x28). For a file with no row, the reason, as `status` above |
| id | the department ID (+0x10) |
| funding, max_funding, fixed | the funding and its maximum as fractions (1.0 is 100%), whether it is fixed |
| full_expense, expense | the sums of its expense lines' full (+0x38) and current (+0x40) expense: what the city would pay at full funding and pays now, a month |
| income | the sum of its lines' income (+0x48) |
| lines, local_objects | its line items and its locally funded objects |

Checked in the game (below, "Checked against the game"): funds, tax income, each group's rate, and every department's `expense`, `income` and `funding` are what the budget simulator holds after the load. The budget window's total expense is `department_expense` plus the ordinances' `expense` plus, in some cities, the neighbour deals and trade (369 a month more in Binko), which the report does not read.

## demand: RCI demand

    python3 tools/sc4report.py demand "<city.sc4 or folder>" [--all] [-o demand.csv]

One CSV row per city file (a folder gives every `*.sc4` in it), from the city's demand objects (class 0x8990C12D, all in one entry; [demand.md](demand.md)). The figures are as of the city's last save.

| Column | From |
|---|---|
| file, status | the file name; `ok`, or why there is no row: `unreadable`, `no demand record`, `undecoded` (a record version the decoder does not know) |
| demands | the number of demand objects (174 in every forum save) |
| R$ to Ih | the active demand (+0x34) of each RCI group's own city demand (ID 0x21010 for R$, 0x23110 Cs$, 0x23320 Co$$, 0x24100 Ir, and so on): what its bar shows, from -6000 to 6000 with the stock exemplars |
| R$_cap to Ih_cap | the same demands' caps (+0x44) as fractions: how much of the demand other demands hold back (1.0 is all) |

With `--all`, one row per demand object instead, in ID order: `id`, `supply`, `demand`, `min`, `max`, `active`, `economy`, `tax` (the modifiers) and `cap`. For a file with no row, the reason is in `id`.

Checked on the 491 GitHub forum saves (test-saves README): all decode, demand records version 1 (unfounded stock tiles) and 2, no leading stream version like the regional city's. Every active demand is within -6000 and 6000 and every cap within 0 and 1. The columns are the active demand as saved: in the game the caps always match, and the active demands matched in one city of three (Kalo, all twelve), but in the other two the game reworks the demands' supply and demand while it loads, before the first day (Binko's R$ supply goes from 94,427 saved to 4,824,773), so right after loading the bars show other values for 5 (Panem) and 8 (Binko) of the twelve groups. The saved values are consistent: each is what the game's update (slot 35) gives from the saved supply, demand, modifiers and cap. The change comes from the demand simulator's Init on a loaded city: `Neighbors` with reset (0x006A5CB0) sets each regional demand (ID + 0x10000) to the neighbouring cities' current totals, and the passes (`Propagate`, 0x006A4E70) then carry the change into the city's own demands (docs/demand.md, "The step"). So the saved values are right for the region as it was at save time; the region has moved on since. A report from one city file cannot redo this: it needs the neighbours' live figures and the demand links of the game's exemplars. Kalo matches because no neighbour of it is developed. Confirmed 2026-10-19 with `OPENSC4_TEST_PANELS_ALL=1` against `demand --all`: Kalo 0 demands changed; Panem 16 (3 regional, each carried exactly into its 0x2xxxx supply, the rest through the links); Binko 108 (52 regional, from tens of thousands to millions, e.g. 0x11000 supply 40,949 to 9,747,812; 56 of the city's own, every supply change equal to its regional's). The bars follow the demand changed message (0x426840A0), which SimulationBegin sends at load with the active demand.

## utilities: power and water

    python3 tools/sc4report.py utilities "<city.sc4 or folder>" [-o utilities.csv]

One CSV row per city file (a folder gives every `*.sc4` in it), from the power simulator record (class 0x0990C005, [power.md](power.md)) and the water simulator record (class 0x0990C075, [water.md](water.md)). The power and water figures are last month's, as of the city's last save.

| Column | From |
|---|---|
| file, status | the file name; `ok`, `ok (no power record)` or `ok (no water record)`, or why there is no row: `unreadable`, `no power or water record`, `power undecoded` / `water undecoded` (a record version the decoder does not know) |
| power_plants | the plant count (+0x2DC) |
| power_produced, power_used, power_imported, power_exported, power_demand | last month's production, consumption, import, export and demand (+0x50 to +0x60) |
| power_surplus | power_produced − power_demand (negative: short of power) |
| power_needed, power_consumers, power_unpowered | the consumers now: their total need (+0x74), their count (+0x68) and those without power (+0x70) |
| water_producers | the pumps and towers (the producer list, +0xBC) |
| water_capacity, water_used, water_imported, water_exported | last month's capacity, consumption, import and export (+0x110 to +0x128) |
| water_demand | the buildings' total water demand (+0x130, property 0xC8ED2D84 summed) |
| water_surplus | water_capacity − water_demand (negative: short of water) |
| water_pipes, water_distressed_pipes, water_burst_pipes | pipe counts (+0x174, +0x178, +0x17C) |

Checked on the 491 GitHub forum saves (test-saves README): all decode, every file has both records. 157 cities have plants or pumps; 15 of them are short of power or water. Checked in the game on three cities: every column the power and water simulators answer (produced, demand, used, imported, exported, plants; capacity, used, demand, sources) matches.

## ordinances: which ordinances are on

    python3 tools/sc4report.py ordinances "<city.sc4 or folder>" [--all] [-o ordinances.csv]

One CSV row per city file (a folder gives every `*.sc4` in it), from the ordinance simulator's record (class 0xA990C08E) and each ordinance's own record (written under its class ID; [ordinance.md](ordinance.md)). The figures are as of the city's last save.

| Column | From |
|---|---|
| file, status | the file name; `ok`, or why there is no row: `unreadable`, `no ordinance records`, `undecoded` (a record version the decoder does not know) |
| ordinances | the number of ordinance records |
| available, enacted | how many are available (+0x91), and how many are enacted: available and on (+0x92), as `IsOn` reads it |
| expense, income | the simulator's last month's ordinance expense and income (+0x18, +0x20) |
| pending | the enactment and repeal income since the month began (+0x28) |
| enacted_names | the enacted ordinances' names, `;` between them, in ID order |

With `--all`, one row per ordinance instead, in ID order: `id`, `name`, `available`, `on`, `enacted`, `enabled`, `year` (first available), `enactment`, `repeal` and `constant` (incomes), `factor`, `adjusted` (this month's income) and `income_ordinance`. For a file with no row, the reason is in `id`. The game loads the exemplar again after Read, so the names and incomes are the exemplar's as saved, not necessarily a mod's.

Checked on the 491 GitHub forum saves (test-saves README): all decode, 20 ordinance records each (the base class 0x8BD4FC74 has none of its own), versions 1 to 4 (316 files below version 2, so no `income_ordinance`). 45 cities have ordinances enacted, up to 18 (Predo, expense 13,260 a month). Names are in the game's language: 88 saves hold Russian names, so compare by `id` across saves. Checked in the game on three cities: `expense` and `income` are the ordinance simulator's monthly figures, and each enacted ordinance's `adjusted` is its cost this month.

## all: every report of a region

    python3 tools/sc4report.py all "<region folder>" [-o reports]

Runs `region`, `budget`, `demand`, `ordinances` and `utilities` over the `City - *.sc4` files of the folder and writes one CSV per report to the output folder (default `reports`, created if missing): `region.csv`, `budget.csv`, `demand.csv`, `ordinances.csv`, `utilities.csv`. Each CSV is what that command alone gives (summary rows, not `--departments` or `--all`), except that only city files are read. Each file is read once, not once per report.

Run on the 491 GitHub forum saves (8 region folders): every file gives an `ok` row in every report (region rows are `ok` or `unfounded`), and each CSV is byte for byte what the single command gives. Timbuktu (91 cities, about 300 MB) takes about 90 seconds.

## Checked against the game

`OPENSC4_TEST_PANELS=1` (src/core/panels_autotest.cpp) logs, right after a city loads in the autotest (`OPENSC4_TEST_DAYS=0`), what the budget, ordinance, RCI, power and water windows read from the game's simulators: `panels: ...` lines in `opensc4.log`. `OPENSC4_TEST_PANELS_ALL=1` also logs every demand object (`panels: all <id> supply demand active`), to compare with `demand --all`. Run on 2026-10-19 with no replacements and no mods on Panem (gh-panem, `Panem (new)`), Binko (gh-makaf Timbuktu) and Kalo (gh-makaf New York): every budget, tax, department, ordinance, power and water figure the reports give matched; the RCI demand as above.

The idea of the census is Null 45's Region Census DLL; no code of it is used.

Tests: `tests/python/test_sc4report.py` builds a version 13 regional city record by hand, `tests/python/test_sc4report_budget.py` a budget with two departments and three line items, `tests/python/test_sc4report_demand.py` three demand objects, `tests/python/test_sc4report_utilities.py` a power and a water record, `tests/python/test_sc4report_ordinances.py` an ordinance simulator and three ordinances, `tests/python/test_sc4report_all.py` a region of these for `all` (ctest `python_tools`, or `python3 -m unittest discover -s tests/python`).
