# PS Vita build notes

This fork of [scribam/flycast](https://github.com/scribam/flycast) (`dev_vita` branch) adds two crash fixes for the
PS Vita port, an optional test mode and these notes. The crashes were tracked down and fixed with the help of
Claude (Anthropic), working from crash dumps, logs and automated runs on a real console.

Notes from building the `dev_vita` branch outside the CI container (Windows host, VitaSDK with GCC 15, CMake 3.22, Ninja)
and from testing it on a real PS Vita (3.65 Enso, SD2Vita, kubridge).

## Libraries

Build and install these into the VitaSDK first, in this order:

1. [SceShaccCgExt](https://github.com/bythos14/SceShaccCgExt). With GCC 15 it needs `-DCMAKE_C_FLAGS=-std=gnu17`.
2. [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK), current master (vitaGL needs `shark_set_shader_association_path`).
3. [vitaGL](https://github.com/Rinnegatamante/vitaGL), current master:
   `make HAVE_SHADER_CACHE=1 HAVE_WRAPPED_ALLOCATORS=1 NO_DEBUG=1 NO_SPLASHSCREEN=1 install`
   Do **not** use `DEPTH_STENCIL_HACK=1` (see "GPU crashes" below).
   Add `HAVE_SHARK_LOG=1` to get the shader compiler's error text in `flycast.log` when a shader fails to compile.
4. [SDL, `vitagl` branch](https://github.com/Northfear/SDL): `-DVIDEO_VITA_VGL=ON`.

The `asio` header shipped with the VitaSDK needs the same patch the CI applies (`struct hostent` → `#include <netdb.h>`
in `asio/detail/impl/socket_ops.ipp`).

## Problems met when configuring and linking

| Symptom | Cause | Workaround |
|---|---|---|
| `'zip_uint64_t' does not name a type` | libzip's type checks fail when CMake cannot link test programs | `-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` and pass the `HAVE_*=0` answers for the functions the Vita lacks |
| `SDL.h: No such file or directory` | `find_package(SDL2)` does not look in the VitaSDK prefix | `-DSDL2_DIR=$VITASDK/arm-vita-eabi/lib/cmake/SDL2` |
| `vita-elf-create: Invalid relocation type 25` | zstd (libchdr dependency) is built as position independent code | set `POSITION_INDEPENDENT_CODE Off` for `libzstd_static` |
| `Enable multithreading to use std::thread: Not owner` at start | `pthread_cancel` stays a weak undefined symbol, so libstdc++ believes threads are unavailable | `-DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,nocopyreloc -Wl,--undefined=pthread_cancel"` |

## Runtime notes

- The shader cache in `ux0:data/shader_cache/FLYCASTDC` is not compatible between vitaGL versions. A cache written by an
  older build makes every shader "compile" to nothing and the app crashes at start. Delete or rename the folder when
  changing vitaGL version.
- Old configuration files keep `Dynarec.downclock`, which is ignored now. The SH4 speed is `Sh4Clock` (MHz, default 200).
- `Dynarec.smcChecks = 0` crashed Rez while loading the first level; `1` (reduced) is stable.

## GPU crashes that power the console off

With vitaGL built with `DEPTH_STENCIL_HACK=1` the depth/stencil surfaces have no memory behind them. That only
works as long as the GPU never does a partial render, which happens when the parameter buffer fills up in a
heavy scene. When it does, the GPU crashes (`psp2core-*-GPUCRASH.psp2dmp` in `ux0:data`), the console shows a
GPU error and powers off. Seen three times: Virtua Tennis within a minute (twice) and Rez in its third level.

Fix: build vitaGL without the hack and give sceGxm an 8 MB parameter buffer instead of 6. After that Virtua
Tennis ran three minutes of matches without a crash.

## Measurements (PS Vita 1000, CPU at 500 MHz, widescreen hack on)

Measured with the test mode (`autostart.txt` + `diag.txt` in `ux0:data/flycast`), 2-9 minutes per game, random button presses.

| Game | Settings | Result |
|---|---|---|
| Rez (PAL) | `Sh4Clock=130`, 360 lines | 46.5 of 50 vblanks/s |
| Rez (PAL) | `Sh4Clock=100`, 360 lines | 49.8 of 50 vblanks/s |
| Rez (PAL) | `Sh4Clock=100`, 480 lines | 50.2 of 50 vblanks/s, 9 minutes without a crash |
| Soulcalibur | `Sh4Clock=100`, 480 lines | 60 fps in menus (after the palette shader fix) |
| Marvel vs. Capcom 2 | same | 60 fps |
| Street Fighter III 3rd Strike | same | 52-60 fps |
| Power Stone 2 | same | 55-58 fps at first, 28-35 in heavier scenes |
| Sonic Adventure (PAL) | same | about 36 of 50 vblanks/s in 3D areas |

**Correction (same day):** the Rez figures above were measured against 50 vblanks per second, but the game
runs in 60 Hz mode: in-game speed was about 71-80%, not ~100%. Audio crackled the whole time. The table below
is measured against 60, with the audio underrun counter of the test mode.

## Where the time goes (Rez attract demo, 500 MHz unless noted)

| Build | In-game speed | Audio underruns per second |
|---|---|---|
| upstream settings, 444 MHz | 71% | 12.5 |
| + idle loop skipping | 76% | 10.1 |
| + `Sh4Clock=80` | 79% (90% in the last minute) | 9.3 (4.6) |
| same at 500 MHz | 81% (95%) | 8.2 (2.0) |
| + AICA batches and per-channel mixing | 81% (96%) | 7.9 (1.3) |

- The render thread is busy only ~39% of the time and the emulation almost never waits for it, so resolution,
  frame skipping, vsync and per-strip sorting change nothing in Rez. The emulation thread is the limit.
- The AICA took about 38% of the emulation thread before batching.
- Memory faults are negligible in game; thread affinity makes no difference.
- Jet Set Radio is render bound instead: about 12 ms per frame reading the TA commands, 6 ms building the
  passes and 13 ms drawing, at 18 fps.
- An LTO build was tried and dropped: mixed results in Jet Set Radio and Rez no longer reached its demo.
- Virtua Tennis (USA) crashes the GPU (`GPUCRASH` dump) and powers the console off.

`pvr.AutoSkipFrame=2` and `rend.ModifierVolumes=no` made no measurable difference in Rez.

## Per-game settings (PS Vita 1000, CPU at 444 MHz)

Flycast reads per-game overrides from a section named after the game ID in `emu.cfg`, for example:

```
[MK-5105850]
config.Sh4Clock = 52
config.aica.Arm7Clock = 50
```

Global settings for these runs: `Sh4Clock=80`, `aica.Arm7Clock=100`, 480 lines. Each row is the last 60 seconds of an
85-95 second run started from the same savestate.

| Game (ID) | Sh4Clock / Arm7Clock | fps | vblanks/s (of 60) | audio underruns/s |
|---|---|---|---|---|
| Jet Set Radio (MK-5105850) | 80 / 100 | 19.7 | 43.5 | 12.1 |
| | **52 / 50** | 19.2 | 51.8 | 7.1 |
| | 45 / 50 | 18.1 | 55.7 | 3.7 |
| Crazy Taxi 2 (MK-5113650) | 80 / 100 | 36.0 | (see note) | 10.2 |
| | **50 / 50** | 38.7 | (see note) | 5.0 |
| Dead or Alive 2 (RDC-0140) | 80 / 100 | 11.3 | 23.3 | 27.5 |
| | 50 / 50 | 12.3 | 33.6 | 19.2 |

- Jet Set Radio: 52/50 is the chosen compromise. 45/50 is smoother but visibly slows the game logic.
- Crazy Taxi 2: the vblank counter reads above 60 in this game, so judge it by fps and underruns. The 50/50 result was
  repeated three times.
- Dead or Alive 2 stays emulation bound (about 31-35 ms per frame). With 50/50, turning off modifier volumes or fog,
  `pvr.AutoSkipFrame=2` and `rend.DelayFrameSwapping=no` made no difference.
- Battery temperature stayed at 30.3 °C during these runs (read with `scePowerGetBatteryTemp`), and the CPU reported 444 MHz.

## Start crash in the memory fault handler

At start the pages being remapped can stay unreachable for hundreds of milliseconds. A fault could then hit the abort
handler's own callees, and the handler gave up at the first nested fault (SIGSEGV, seen with Crazy Taxi 2). It now waits
10 ms and retries, up to 50 times, before giving up.
