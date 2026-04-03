# White Noise Speaker Implementation Report

## Status

| Item | Status | Notes |
|------|--------|-------|
| MP3 control | Done | Play, pause, prev, next, track select, volume 0~100 mapped to MP3 0~30 |
| TJC screen input | Done | `PREV/PLAY/NEXT/TIME/LIGHT/MODE/VOL+/VOL-` parsed in `task_user1` |
| TJC screen feedback | Done | `t3/t4/t5/t7/t10` refreshed by dirty flags |
| Timer off | Done | Supports `0/1/5/10/15/30/60` minutes |
| WS2812 light control | Done | Two WS2812 channels on `USER_IO_1/USER_IO_2`, no white or near-white colors |
| Scene mode linkage | Done | Sleep, Focus, Meditate, Baby scenes link track, brightness, and default light effect |
| Breath light effect | Done | Static and breath effects kept |
| Chase light effect | Removed | No longer used in `task_user1` |
| APP remote control | Done | Brightness, timer, mode, track, and state upload kept |

## Light Behavior

- Removed the old 50ms WS2812 timing test resend function from `task_user1`
- Removed chase light usage from `task_user1`
- `LIGHT` key now cycles brightness steps `0 -> 25 -> 50 -> 75 -> 100 -> 0`
- `MODE` key keeps selecting scene mode, and scene mode decides the current light mode
- APP `SET_LIGHT` still controls brightness percent `0~100`
- Screen `t5` now shows brightness only
- APP TX mapping:
- `remoteVar_TX[4]`: current light mode index
- `remoteVar_TX[5]`: current light effect type
- `remoteVar_TX[6]`: current brightness

## Scene Light Mapping

| Scene | Track | Brightness | Light mode | Effect |
|------|-------|------------|------------|--------|
| Sleep | 1 | 25% | `BLUE` | Static |
| Focus | 6 | 35% | `JADE` | Static |
| Meditate | 4 | 45% | `VIO-B` | Breath |
| Baby | 5 | 30% | `PNK-B` | Breath |

## Timer Notes

- Added `1min` timer option
- Screen `t4` shows seconds when remaining time is less than 60 seconds

## Driver Notes

| File | Change | Reason |
|------|--------|--------|
| `MDK-ARM/userDriver/driver_ws2812.c` | Timing parameters set to `T0H=7/T0L=34/T1H=32/T1L=1` | Current oscilloscope tuning baseline |
| `MDK-ARM/userDriver/driver_ws2812.c` | Direct `DWT->CYCCNT` read in critical timing path | Reduce call overhead and shorten pulse width |

## Main Code Locations

- `MDK-ARM/userApp/task_user1.h`
- Adds `TIMER_1MIN`
- `MDK-ARM/userApp/task_user1.c`
- Removes chase effect scheduling
- Restores `LIGHT` key to brightness control
- Keeps breath effect update loop
- Adds `1min` timer handling and `<60s` display

## Suggested Test

1. Press `TIME` and confirm timer order is `OFF -> 1m -> 5m -> 10m -> 15m -> 30m -> 60m -> OFF`.
2. Start a 1 minute timer and confirm `t4` changes from `1m` to second display under 60 seconds.
3. Press `LIGHT` and confirm brightness cycles `0 -> 25 -> 50 -> 75 -> 100 -> 0`.
4. Press `MODE` and confirm each scene restores its default track, brightness, and light effect.
5. Confirm Meditate and Baby scenes are breath effects, and no chase effect appears anymore.
