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

The idea is Null 45's Region Census DLL; no code of it is used.

Tests: `tests/python/test_sc4report.py` builds a version 13 record by hand (ctest `python_tools`, or `python3 -m unittest discover -s tests/python`).
