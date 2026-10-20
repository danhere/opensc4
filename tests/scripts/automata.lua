-- Calls the automata script system's Lua functions (sc4game.automata, the table
-- its Init registers; docs/automata.md, "Part s2") with fixed arguments and
-- leaves the results in _test_result (OPENSC4_TEST_LUA_SCRIPT; docs/lua.md).
-- The objects the functions push are script numbers made from pointers:
-- only whether one came back is recorded, not its value.
-- The occupant arguments are nil (a script cannot name an occupant here), so
-- the attach and detach functions read their names and stop there.
-- create_controller and create_automaton change the city: a free controller
-- of the template Car_Abduction and a vehicle (cSC4Vehicle, 0x896E714A) at
-- the middle of a 64-cell city; alike on both sides.
_test_result = ""
local function try(n, ...)
  local f = sc4game and sc4game.automata and sc4game.automata[n]
  if not f then
    _test_result = _test_result .. n .. "=missing;"
    return
  end
  local ok, a = pcall(f, unpack(arg))
  local seen = "nil"
  if type(a) == "number" then
    if a == 0 then seen = "0" else seen = "nonzero" end
  elseif a ~= nil then
    seen = type(a)
  end
  _test_result = _test_result .. n .. "=" .. tostring(ok) .. ":" .. seen .. ";"
end
try("get_source_building_count", 0)
try("get_source_building_count", 1)
try("get_source_building_count", 3)
try("get_source_building_count", "x")
try("create_controller", "Car_Abduction", 512, 300, 512)
try("create_controller", "opensc4 no such template", 512, 300, 512)
try("attach_controller", "Car_Abduction", nil)
try("attach_controller_group", "Car_Abduction", nil)
try("detach_controller", "Car_Abduction", nil)
try("detach_controller_group", "Car_Abduction", nil)
try("create_automaton", 0, 512, 300, 512)
try("create_automaton", "896e714a", 512, 300, 512)
try("create_automaton", "896e714a", 520, 300, 520, 0)
