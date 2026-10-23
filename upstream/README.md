# D2R Android

Runs the **original ARM64 build of Diablo II: Resurrected** natively on Android (NVN → Vulkan rendering, AAudio sound, touch and gamepad input); the repository holds only the host/port code and tools, and **no game content**.

中文：[README.zh-CN.md](README.zh-CN.md)

## Quick start

**Option 1: download the built artifacts (recommended)**

From [Releases](https://github.com/sydeEvans/d2r-android/releases) (CI publishes one whenever a `v*` tag is pushed):

1. `D2R-Android-port.apk` — the phone app. It contains **no game data**; the data comes from the package below.
2. `d2r-package-kit-<version>.zip` — the kit for your PC: the source (the packing tools live in it) + the `d2r-package` skill + that APK + a one-page readme.

Then:

1. Open the extracted kit in an AI agent (Claude Code or similar) and say: **"use the d2r-package skill to build my package and install it on my phone"**.
2. The agent reads `.claude/skills/d2r-package/SKILL.md` and walks you through preparing **your own** game files (`.nsz` / `.nsp` / `.xci`) and `prod.keys`, then builds the data package locally (about an hour; `--reuse` makes later runs a few minutes).
3. Following the skill's "install straight from adb" section, the agent installs the APK and pushes the package; you finish with two taps on the phone: import the package, then start the game.

Your keys and game files never leave your computer.

**Option 2: build it yourself (Linux x86_64)**

```bash
python3 dm/android/tools/bootstrap.py          # NDK / JDK / build-tools / platform (~800 MB, once)
python3 dm/android/tools/build.py --game-run   # writes dm/android/build/D2R-Android-port.apk (generic)
python3 dm/android/tools/ci_checks.py          # optional: the same checks the repository CI runs
```

To embed the game code in the APK instead of importing it, first generate the modules from your own game files as [the build notes](docs/ANDROID_BUILD.md) describe, then pass `--game-source <dir>` to `build.py`.

## Screens

Captured on the device (OPPO OPD2409 / Adreno 830). The block in the top-left corner of the game frames is the debug overlay, live: frame rate, frame time, CPU / GPU time, power draw and draw counts.

**Launcher** — package import, display profile, output resolution, save import / export. It is an ordinary app screen, so it has no frame overlay:

![Launcher](docs/images/launcher.jpg)

**Hero selection** — the character came back after importing a save from a backup:

![Hero selection](docs/images/hero.jpg)

**Camp** — Act I camp, measured 60.0 FPS / 16.7 ms per frame:

![Camp](docs/images/camp.jpg)

## Progress (2026-10-17)

- The game's ARM64 code runs on an Android host with NVN → Vulkan; AAudio sound, system-keyboard name entry, virtual stick and buttons, **gamepad support (D-pad and ZL/ZR included)** and an FPS/CPU/GPU overlay (off by default, switchable from the in-game menu).
- **Launcher**: data-package import, display profile, output resolution (follow profile / 1080p / 1440p), save import/export.
- **In-game menu** (on the system back key, modelled on Eden): frame rate display, on-screen controls, quality (four internal 3D resolution steps, effective immediately), export save, import save, exit. An accidental edge swipe no longer quits the run.
- **Lifecycle**: going to the background asks the game to save before suspending, and the process is kept (window and swapchain rebuilt on return); tapping the icon after backgrounding goes straight back into the game.
- **Performance**: camp on OPD2409 / Adreno 830 averages about 60 FPS (142 of 151 consecutive 120-frame windows had no frame over 33 ms). Cross-launch pipeline warm-up is verified on device (127 records rebuilt in 447.7 ms; menu pipeline creation fell from 826 ms to 1.1 ms). The remaining gap is first use of shader combinations on ground the player has not visited, plus occasional long frames.
- **Not yet sustained 60 FPS in every scene**, and not a fully validated release. The [port progress](docs/ANDROID_PORT_PROGRESS.zh-CN.md) is authoritative.

## Documentation

- [Android port progress (resume entry point, authoritative)](docs/ANDROID_PORT_PROGRESS.zh-CN.md)
- [Android build and deployment](docs/ANDROID_BUILD.md)
- [Data package format and packing](docs/ANDROID_PACKAGE.zh-CN.md)
- [Launch settings](docs/ANDROID_LAUNCH_SETTINGS.zh-CN.md)
- [Snapshot boundaries and known issues](docs/ANDROID_RELEASE_SNAPSHOT.zh-CN.md)
- [Original upstream README](docs/UPSTREAM_README.md)

## Dependencies and provenance

- **Upstream**: [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) at `2eb5758771f110cd53082b999d88d242766610da`; its [MIT license](LICENSE) and third-party notices are retained.
- **Optimization references**: Eden (a Switch emulator; its rendering and upscaling ideas are referenced, but its shader translation is GPL-3.0 and is **not** merged into this MIT project) and LASRecomp.
- **Bundled graphics driver**: [Turnip](https://gitlab.freedesktop.org/mesa/mesa) (Mesa's open-source Vulkan driver, MIT) — the T30 Adreno build by **@Mr_Purple_666** (`vulkan.purple.so`, `26.3.0-T30-1.4.359`), shipped unmodified at `dm/android/prebuilt/turnip/` together with its `meta.json`. The launcher can also run on the phone's own driver.
- **Build dependencies**: Android NDK r28c, JDK 17, Android SDK build-tools / platform 35 (pinned by `bootstrap.py`).
- **Packing game data additionally needs** (on the player's PC only): a Python venv (`lz4`, `capstone`, ...), `hactool`, and the .NET 10 SDK; the skill's prerequisites table lists them.

**This repository does not contain** game executables, RomFS, keys, saves, translated game shaders or APKs. The game modules and the data package are produced locally from **your own** files and are never distributed here or in a release.

It **does** ship one third-party binary: a **Turnip** Vulkan driver (`dm/android/prebuilt/turnip/vulkan.purple.so`, Mesa, MIT — T30 build by **@Mr_Purple_666**), which the build packs into the APK. The launcher can switch between it and the phone's own driver; on the test device the system Adreno driver renders **blocky lighting around the player** where Turnip renders it correctly, at the same ~60 FPS. Turnip needs Android 11+ (`minApi 30`), and the game's shaders are built for a subgroup size of 64, so the launcher writes the package's own `host-subgroup-size.txt` value into the driver configuration. Building with `--no-turnip` produces a driver-free APK instead. See [the driver notes](docs/ANDROID_GPU_DRIVER.zh-CN.md).

CI (GitHub Actions) builds the APK and the kit described above; see [the build notes](docs/ANDROID_BUILD.md).
