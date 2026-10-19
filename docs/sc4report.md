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

Record versions 10 to 13 decode (13 is what the game writes; before 12 there are no points of interest, before 13 no difficulty, which reads as 2). Older versions are reported as undecoded.

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

The idea of the census is Null 45's Region Census DLL; no code of it is used.

Tests: `tests/python/test_sc4report.py` builds a version 13 regional city record by hand, `tests/python/test_sc4report_budget.py` a budget with two departments and three line items (ctest `python_tools`, or `python3 -m unittest discover -s tests/python`).
