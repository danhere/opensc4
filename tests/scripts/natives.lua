-- Calls the game's native script functions with fixed arguments and leaves
-- the results in _test_result (OPENSC4_TEST_LUA_SCRIPT; docs/lua.md).
-- Left out: the natives that need a current advice or a MySim, and the
-- camera and dialog ones.
_test_result = ""
local function try(n, ...)
  local f = (game and game[n]) or _G[n]
  if not f then
    _test_result = _test_result .. n .. "=missing;"
    return
  end
  local ok, a = pcall(f, unpack(arg))
  if type(a) == "userdata" then a = "userdata" end
  _test_result = _test_result .. n .. "=" .. tostring(ok) .. ":" .. tostring(a) .. ";"
end
local figure = 2321806092
local ordinances = {13660733, 46079482, 2697883945}
try("difficulty_level")
try("is_tutorial")
try("random_chance", 50)
try("random_chance", 50)
try("random_chance", 10)
try("random_chance", 90)
try("_combine_guids", 1234, 5678)
try("_combine_guids", 1234)
try("trend_value", figure, 0)
try("trend_value", figure, 3)
try("trend_count", figure, 12, 0)
try("trend_slope", figure, 12, 0)
try("trend_slope", figure, 6, 1)
for i = 1, 3 do
  local id = ordinances[i]
  try("ordinance_is_available", id)
  try("ordinance_is_on", id)
  try("ordinance_enact_fee", id)
  try("ordinance_repeal_fee", id)
  try("ordinance_monthly_cost", id)
  try("ordinance_name", id)
  try("ordinance_description", id)
end
try("ordinance_set_on", ordinances[1])
try("ordinance_is_on", ordinances[1])
try("ordinance_set_off", ordinances[1])
try("ordinance_is_on", ordinances[1])
try("ordinance_set_on", ordinances[2])
try("reward_instance_count", 1)
try("get_reward_building_state", 1)
try("unlock_reward_building", 1)
try("get_reward_building_state", 1)
try("reward_instance_count", 1)
try("trigger_event", 12345)
try("event_feedback")
try("expire_advice")
try("retire_advice")
