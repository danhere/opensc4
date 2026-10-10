-- Calls the native script functions that work on the current advice and
-- leaves the results in _test_result. Run with OPENSC4_TEST_LUA_SCRIPT and
-- OPENSC4_TEST_LUA_ADVICE=1 (docs/lua.md), which makes one of the city's
-- advices the current one. Left out: advice_info (a dialog) and the camera
-- ones.
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
  return a
end
local f = (game and game.advice_subject) or advice_subject
local subject = f and f()
try("advice_subject")
try("event_building_distance")
try("event_disaster_distance")
for group = 0, 3 do
  try("building_is_group", subject, group)
end
try("building_is_group", subject, 4096)
try("nd_cancel", subject)
for kind = 0, 2 do
  try("mysim_distance_to_closest_building", kind)
  try("mysim_distance_to_closest_landmark_or_reward", kind)
  try("mysim_local_school_grade", kind)
  try("mysim_local_hospital_grade", kind)
end
try("mysim_dispatch_tool", 1)
try("event_feedback")
try("event_feedback", 7)
try("difficulty_level")
try("expire_advice")
try("retire_advice")
