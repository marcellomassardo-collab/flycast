# PS Vita build notes

Notes from building the `dev_vita` branch outside the CI container (Windows host, VitaSDK with GCC 15, CMake 3.22, Ninja)
and from testing it on a real PS Vita (3.65 Enso, SD2Vita, kubridge).

## Libraries

Build and install these into the VitaSDK first, in this order:

1. [SceShaccCgExt](https://github.com/bythos14/SceShaccCgExt). With GCC 15 it needs `-DCMAKE_C_FLAGS=-std=gnu17`.
2. [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK), current master (vitaGL needs `shark_set_shader_association_path`).
3. [vitaGL](https://github.com/Rinnegatamante/vitaGL), current master:
   `make DEPTH_STENCIL_HACK=1 HAVE_SHADER_CACHE=1 HAVE_WRAPPED_ALLOCATORS=1 NO_DEBUG=1 NO_SPLASHSCREEN=1 install`
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

`pvr.AutoSkipFrame=2` and `rend.ModifierVolumes=no` made no measurable difference in Rez.
