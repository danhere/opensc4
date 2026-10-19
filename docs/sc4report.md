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

In real saves the record starts with a uint16 1 before the record's version; we take it to be the segment stream's own version (inferred: all 491 forum saves checked have it). Checked on the 491 GitHub forum saves (test-saves README), all of which decode: `commercial_jobs` is the sum of the five `pop_C*` types, `industrial_jobs` is `pop_Id + pop_Im + pop_Ih` plus a fifth of `pop_Ir` (farm jobs count one fifth), and `residents` is within a few percent of the three `pop_R*` types (it is figured apart). So the type IDs are right. The map also holds types 0x1001, 0x10x0, 0x2xxx and 0x3xx0 (in `pop_other`), meaning not known. Names are UTF-8. The tax column order is still assumed.

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

Unconfirmed on real saves: that the departments' current expense is what the budget window shows a month.

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

Checked on the 491 GitHub forum saves (test-saves README): all decode, demand records version 1 (unfounded stock tiles) and 2, no leading stream version like the regional city's. Every active demand is within -6000 and 6000 and every cap within 0 and 1. Unconfirmed: that the columns match the in-game RCI bars (needs the game).

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

Checked on the 491 GitHub forum saves (test-saves README): all decode, 20 ordinance records each (the base class 0x8BD4FC74 has none of its own), versions 1 to 4 (316 files below version 2, so no `income_ordinance`). 45 cities have ordinances enacted, up to 18 (Predo, expense 13,260 a month). Names are in the game's language: 88 saves hold Russian names, so compare by `id` across saves. Unconfirmed: that `expense` matches the budget window's ordinance line (needs the game).

The idea of the census is Null 45's Region Census DLL; no code of it is used.

Tests: `tests/python/test_sc4report.py` builds a version 13 regional city record by hand, `tests/python/test_sc4report_budget.py` a budget with two departments and three line items, `tests/python/test_sc4report_demand.py` three demand objects, `tests/python/test_sc4report_ordinances.py` an ordinance simulator and three ordinances (ctest `python_tools`, or `python3 -m unittest discover -s tests/python`).
