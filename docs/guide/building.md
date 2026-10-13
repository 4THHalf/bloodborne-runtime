# Building from source

[← Documentation](../README.md)

## Requirements

- macOS on Apple Silicon (macOS 15 or newer recommended), or Linux (the `shell.nix` environment).
- macOS: Xcode Command Line Tools and [Homebrew](https://brew.sh):

  ```bash
  brew install bash pkgconf glslang nasm cmake ninja
  ```

- A game folder prepared as in
  [Game files](https://github.com/bloodborne-native/bloodborne-mac/blob/main/docs/guide/game-files.md).

## Build and run

```bash
git clone --recursive https://github.com/bloodborne-native/bloodborne-runtime.git
cd bloodborne-runtime
bash build.sh                                  # the first run also builds the libraries (deps/)
BB_GAME_DIR=/path/to/your/game bash run.sh
```

- `build.sh` builds the native program (on Apple Silicon bbcpu, from the `recomp/` submodule,
  translates the game's code). `BB_ARCH=x86_64` builds the older Rosetta 2 one.
- The first build compiles the dependencies (MoltenVK, SDL3, FFmpeg, ...) into `deps/` and takes
  a while.
- From a checkout, saves and the shader cache go to `user/` and settings to `bbport.ini`.
- The recompiled game: build a library with [bloodborne-recomp](https://github.com/bloodborne-native/bloodborne-recomp)
  (in `recomp/`) and run with `BB_RECOMP_LIB=recomp/out/recomp/librecomp.dylib`.

The macOS app (launcher, DMG, releases) is
[bloodborne-mac](https://github.com/bloodborne-native/bloodborne-mac), which builds this repository.

## Environment variables

`run.sh` reads its options from the environment. The launcher sets them from its settings.

| Variable | Effect |
|---|---|
| `BB_GAME_DIR` | The game folder |
| `BB_RENDER_RES=WxH`, `BB_OUTPUT_RES=WxH` | Render and output size |
| `BB_UPSCALER=fsr3\|off` | Upscaler |
| `BB_FRAME_STATS=1` | Frame statistics in the log |
| `BB_LANGUAGE` | The game's language (PS4 language code) |
| `BB_UI_LANGUAGE` | The in-game menu's language (`en`, `pt-BR`, ...) |
| `BB_MODS_DIR`, `BB_MODS_ENABLED=0` | Mods folder, or no mods |
| `BB_PATCHES_DIR` | Third-party patches folder |
| `BB_VK_DRIVER=kosmickrisp` | Mesa's KosmicKrisp Vulkan driver instead of MoltenVK (macOS 26+, slower today) |
| `BB_SKIP_GAME_CHECK=1` | Skip the game folder check |

## Developer tools

- `tools/mac_bench.sh NAME [VAR=VALUE...]`: starts the game in the background, enters the level
  and prints frame-time statistics.
- `tools/mst_summary.py`, `tools/mst_passes.py`: read Metal System Traces.
- `tools/grab.sh [--screen] NAME`: saves the game's own frame as a PNG (`--screen` includes the
  in-game menu, which `touch out/menu.trigger` opens).
- `python3 -m unittest discover -s tests`: the Python tests.
- `bash build.sh --test`: native runtime tests, including `slot-vector-test` (50,000 stable
  entries, alignment, iteration, slot reuse and destruction). This test needs no game or GPU
  device and also runs in Linux CI.

Upstream's GTK4 launcher (`bash launcher/bb-launcher.sh`) also works, with
`brew install gtk4 libadwaita pygobject3`.

## Opening-video release check on macOS

Before a release, test both copied-save gameplay with `tools/test_run.sh` and the isolated
new-save opening with `tools/test_opening_video.sh NAME`. The latter creates a fresh user
directory, navigates setup with one Up press for Next, and reports whether playback completed.
`VIDEO_GRAB=1` captures the game's frame at character creation. Tell the user before batches;
check `pgrep -x bb-probe` first. Never use the real save directory or capture the user's screen.
