# Phantom budget rows

A cheat that takes budget rows of buildings that are gone out of a department. Such a row is usually left when a plugin is removed from the Plugins folder while a city still has its building: the building goes, its cost stays in the budget. An addition beyond the original that is part of the base game ([ROADMAP](../ROADMAP.md), "Improvements: the rules"): it is there without any setting, and nothing changes unless the cheat is typed. It is the replacement `game.phantom_budget`, on by default; `game.phantom_budget = original` in `[replacements]` leaves the cheat out.

## The cheat

In a city, in the cheat box (Ctrl+X):

```
RemovePhantomBudgetItems <category> [all]
```

`<category>` is one of `Fire`, `Police`, `Jail`, `Power`, `Education`, `Health`. Without `all`, only the items whose building has lost its exemplar are removed; with `all`, every locally funded item of the category is removed, also those of buildings that stand. The answer comes in a message box (we have no in-game interface of our own yet), and the log has the counts. The change is in the city only once the city is saved.

## What it does

[src/core/game_phantom_budget.cpp](../src/core/game_phantom_budget.cpp) registers the cheat with the game's cheat code manager once, at startup (the log then says the cheat is available), and asks to be told of typed cheats. That is all that is installed: no hook, no code that runs with the simulation. Typed outside a city, the cheat says so. For the category's departments (Education and Health have two each: staff and coverage) it asks the department for its locally funded items of the category's purpose (`GetLocallyFundedItemsByPurpose`), and for each item that is a phantom, a property holder without an exemplar type (property 0x10), it calls `RemoveLocallyFundedObject` and `RemoveLineItem` for the building's type.

Everything goes through the public interfaces of the budget simulator and its departments ([budget.md](budget.md): department slots 32, 23 and 19), so it is the same with the game's budget code and with ours (`simulators.budget.*`). The game's `GetLocallyFundedItemsByPurpose` (0x006AA9C0) leaves no result in AL, so ours goes by the list it fills and not by the declared bool.

## Credit

The cheat, its name, the categories with their department and purpose IDs and the rule for what is a phantom are from [sc4-remove-phantom-budget-items](https://github.com/0xC0000054/sc4-remove-phantom-budget-items) by Nicholas Hayes (MIT License; the notice is in the source file). Where the mod answers in a game dialog, ours uses a message box. Ours registers the cheat under the mod's ID.

## Checked

2026-10-11, unattended, Making Money, with the test switch `OPENSC4_TEST_PHANTOM_BUDGET` (a list of argument strings handed to the cheat code manager's `DoDefaultCheatCodeProcessing`, the call the cheat box makes, when the city has started and again when it closes), once with the game's budget code and once with ours (`simulators.budget.* = ours`); both runs gave the same lines:

- no arguments: the usage text; `Bogus`: unknown category;
- `Fire`, `Police`, `Power`: one locally funded item each, none a phantom; `Jail`: none; `Education`, `Health`: two each (one building in two departments), none a phantom; the answer is "No orphaned budget items found.";
- `Education all`: both items removed (one building type), and `Education` after it finds none; `RemoveLocallyFundedObject` and `RemoveLineItem` ran.

A run without the switch has one line, that the cheat is available; with `game.phantom_budget = original` it has none.

Not checked: the removal of a real phantom row (it needs a city saved with a plugin's building and loaded without the plugin; the tutorial cities have none), the cheat typed by hand in the cheat box, and the message box.
