# Android build and deployment

The maintained step-by-step instructions are in [README.zh-CN.md](ANDROID_BUILD.zh-CN.md). See the repository [status](../README.md) and [snapshot boundaries](ANDROID_RELEASE_SNAPSHOT.zh-CN.md).

GitHub Actions (`.github/workflows/build.yml`) builds a **generic APK** on every push (host and frontends, no game code; the game modules come from the package imported on the phone) and a **pack kit** for the player's PC (source + `d2r-package` skill + that APK + a readme). Pushing a `v*` tag publishes a Release carrying both. The APK with embedded game code is still built locally with `--game-source`, because that code derives from the player's own files and stays out of the repository and CI. The portable checks CI runs are the same ones available locally: `python3 dm/android/tools/ci_checks.py`.

Build on Linux x86_64 using `python3 dm/android/tools/bootstrap.py` (NDK r28c, API35, JDK), then install Python dependencies in a virtual environment: `lz4 capstone unicorn numpy pillow zstandard nsz`. Tools default to `~/dm-work/tools/android`; override with `DM_ANDROID_TOOLS`. The build also fetches its pinned `libadrenotools`/`liblinkernsbypass` sources; see [GPU driver notes](ANDROID_GPU_DRIVER.zh-CN.md) for the caches and overrides.

Prepare your own RomFS, rewritten `main`, `sdk` and `subsdk0` modules, and a matching SPIR-V cache. The repository provides `extract_packages.py`, `patch_nso.py` with `DM_TARGET=android`, and `build_shaders.py`; it does not distribute game data, keys or the generated artifacts. Shader translation additionally requires a .NET 10 SDK (default `~/dm-work/tools/dotnet/dotnet`, installable with the official dotnet-install script; the first NuGet restore needs network).

Before the first build in a clean checkout, activate the Python environment and generate the host tables from your own RomFS:

```bash
make -C dm/host dm_vault_tab.h dm_hud_exp.h dm_bright_levels.inc \
  ROMFS="$HOME/dm-work/data/switch/3.3.94012/Update/RomFS"
```

These generated files are Git-ignored. After changing game resources, remove those three files and regenerate them.

```bash
python dm/android/tools/build.py --game-run \
  --game-source ~/dm-work/build/android/game \
  --shader-cache ~/dm-work/build/android/shaders/spirv
```

Output: `dm/android/build/D2R-Android-port.apk`. It includes locally derived game code and is not part of the source publication. Install using `adb -s DEVICE install -r`, and deploy RomFS and shaders once into `/sdcard/Android/data/org.d2r.android.port/files/{romfs,shaders}`. Start `org.d2r.android.port/.D2RActivity`. Updating the APK preserves app data; do not overwrite saves in private `files/save`.

Touch controls, gamepads, keyboard input, native name entry, AAudio and a performance overlay are implemented. The display preserves the game's aspect ratio. Logs are readable with `adb -s DEVICE shell run-as org.d2r.android.port tail -100 files/game.log`.

The current source includes private `files/pipeline-cache` persistence and cross-launch pipeline warm-up (`dm/android/native/nvn_vulkan_warm.inc`). Both are verified on device: the driver cache reloads (`load=accepted`) and a 127-record manifest warmed in 447.7 ms in the background. First use of a shader combination in ground the player has not visited still compiles on demand. Diagnostic/test sources remain available; no new tests were added or run during publication preparation.
