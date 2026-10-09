# Timelapse

Watch a city grow: the game keeps a dated copy of the city at every save, and a script turns the copies into frames from one fixed view, then into a video.

## Keeping copies

```ini
[replacements]
game.timelapse = ours

[timelapse]
folder =          ; optional; default <user folder>\Timelapse
```

`game.timelapse` ([src/core/game_timelapse.cpp](../src/core/game_timelapse.cpp)) copies the city's file after each successful save (`cSC4App::SaveCity`, 0x00455E20; the in-place save and Ctrl+S go through it) to `<user folder>\Timelapse\<region>\<city file name>\<yyyy-mm-dd>.sc4`, named by the game date. It only reads the file once the game has finished writing it, so saving is unchanged; a second save on the same game day replaces that day's copy. A timelapse is as fine-grained as the saves: save every year or so for a smooth one.

## Rendering

```bash
B="$LOCALAPPDATA/opensc4/build/release"
scripts/render-timelapse.sh "$B" "<user folder>/Timelapse/<region>/City - <name>" "<output folder>"
```

For each copy, oldest first, [scripts/render-timelapse.sh](../scripts/render-timelapse.sh) puts it in a disposable user folder (`%LOCALAPPDATA%\opensc4\timelapse-env`) with a copy of its region, loads it unattended with the simulation paused, hides the interface, centres the view on the middle of the city and saves one frame with the game's own snapshot code (autotest switches `OPENSC4_TEST_SHOTS`, `OPENSC4_TEST_SHOT_VIEWS`, `OPENSC4_TEST_SHOT_CENTER`, `OPENSC4_TEST_SHOT_HIDE_UI`). Frames are named after the copies; frames already there are kept, so an interrupted run resumes. With ffmpeg on the PATH they also become `timelapse.mp4` at four frames a second.

- `OPENSC4_TIMELAPSE_VIEW=<zoom>:<rotation>`: the view, default `0:0`. The camera's zoom index (+0x108 of the camera control) runs from 0, the farthest, which shows a whole small city in a 1024 x 768 window, to 5; rotation 0 to 3. Large cities need a bigger window (`OPENSC4_GAME_WINDOW`) to fit at zoom 0.
- `OPENSC4_TIMELAPSE_USERDIR`: where the region comes from, if not the user folder the copies sit in.
- Automata and clouds are off in the frames, and seeds fixed, so frames differ only where the city did.
- The player's own user folder is never loaded or written.

Checked on 2026-10-09 with four saves of Making Money (as found, 90 days, 1 year, 3 years on): four frames of the whole city from the same view, the town growing between them; and an in-game Ctrl+S during a run made a dated copy.

## Why not a replay

A timelapse that re-simulates the city from its founding, from the seed and the player's recorded input, was tried first: the simulation can be made to replay exactly with the determinism replacements, but the interface runs on real time in many places (keyboard scrolling, the pause while the camera moves, timers), so recorded input lands differently on playback. Copies at saves need none of that.
