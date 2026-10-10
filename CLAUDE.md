# bloodborne-runtime

Runs Bloodborne (PS4, CUSA03173 1.09) outside a PS4: the loader, the PS4 system libraries
(`src/runtime_*.c`), and the renderer (`gpu/`, derived from shadPS4, Vulkan through MoltenVK on
macOS). Built on bbport, whose releases are merged here. The game's code runs through bbcpu from
the `recomp/` submodule (bloodborne-recomp), or as its recompiled library (`BB_RECOMP_LIB`).

## Layout

- `src/probe.c` (the program, `out/bb-probe`), `src/runtime*.c`, `src/platform.{c,h}` (Linux and
  macOS differences: `bb_shared_memory`, the `BB_UC_*` signal-context macros, the image base).
- `gpu/`: `shadps4/` (video core, shader recompiler), `shim/` (window, overlay menu, guest hooks,
  Metal residency), `layer/`, `cp/`, `games/` (game profile). Built by CMake into `libbbgpu`.
- `scripts/`: `prepare.py`, `link_modules.py` (boot image), `patches.py`, `game_check.py`, `mods.py`,
  `launcher_api.py`; `scripts/macos/build-deps.sh` (MoltenVK, SDL3, FFmpeg, ... into `deps/`).
- `run.sh` (runs the game; the app calls it), `build.sh`, `patches/Bloodborne.xml`.
- `tools/`: `mac_bench.sh` (background run, frame statistics; `SAMPLE=1`, `TRACE=1`),
  `test_run.sh` (the same with a copy of the player's save: use this one), `grab.sh`, `press.sh`
  (pad input), the Metal trace readers (`mst_*.py`), `fsr4cap/`.
- `docs/`: `guide/building.md`, `guide/how-it-works.md`, `MACOS_PERFORMANCE.md`, `CREDITS.md`,
  upstream's notes (many in Russian).

## Build, run, test

```sh
bash build.sh                                   # macOS: builds deps/ the first time (~1 h)
BB_GAME_DIR=~/Downloads/PS4/Bloodborne/CUSA03173 bash run.sh
tools/test_run.sh NAME [VAR=VALUE...]           # background run with a copy of the save
cd tests && python3 -m unittest discover -p 'test_*.py'
```

`test_probe.LoaderTests` fails on macOS (its images use 4 KiB-aligned segments; Macs have 16 KiB
pages): known, not a regression. A healthy run in the Hunter's Dream is ~58–62 FPS on the user's
M3 Pro; FPS is bound by rendering, so CPU-side changes are measured in CPU time.

## Rendering bugs

Bisect bbport's optimizations first: `BB_TOGGLE_FILE` holds a mask of switches (bits 0–19, 30,
32–50; re-read every 250 ms); run with all off, then halve groups with one fixed mask per run.
The user watches and says what they see. Example: the cutscene flashing was bit 42
(`WRITE_DATA` pipelined on the recording thread).

## Upstream merges

bbport releases come as squashed commits; remote `upstream`
(https://github.com/deadinside28/bloodborne_pc). Merge their tag (`git merge <tag>`), keep both
sides in conflicts, then check what assumes Linux or native x86:

- no `MAP_FIXED_NOREPLACE`, `memfd_create`, `fallocate`, `gregs`/`REG_RIP` on macOS: use
  `bb_shared_memory`, `bb_shared_memory_zero`, `BB_UC_RIP`/`BB_UC_RSP`;
- the image is not at 0x800000000: use `BB_IMAGE_BASE`, and image offsets in game profiles;
- host code cannot run x86 stubs or call guest functions directly under bbcpu: such features stay
  int3 traps or are off on macOS (upstream's guest-hook detours, the game-menu pages);
- libc++ needs complete types where GCC does not.

Then build, run the game a few times (`tools/test_run.sh`), update `docs/CREDITS.md` ("merged up
to") and `docs/upstream/README*.md`, and bump the submodule in bloodborne-mac. On macOS the
"Auto" memory model picks the 0.3 one (non-AMD GPU).

## Working rules (all three repositories)

- **The project is three repositories**, nested as submodules:
  [bloodborne-mac](https://github.com/PabloVSouza/bloodborne-mac) (the app) ⊃ `runtime/` =
  [bloodborne-runtime](https://github.com/PabloVSouza/bloodborne-runtime) ⊃ `recomp/` =
  [bloodborne-recomp](https://github.com/PabloVSouza/bloodborne-recomp). Commit in the repository
  that owns the file, push it, then bump the submodule pointer in the parent (runtime → recomp,
  mac → runtime) and push that too.
- **Before any push, check `git remote get-url origin`** (a clone once still pointed at a local
  repository and a force-push landed there). Push to `main`; ask before force-pushing anything
  that existed before this session.
- **Commits:** a short subject, a body that says what changed and why (numbers when measured), and
  the trailer the session asks for (`Co-Authored-By: ...`).
- **Credits:** this work is built on [bbport](https://github.com/deadinside28/bloodborne_pc) by
  deadinside28 and on [shadPS4](https://github.com/shadps4-emu/shadPS4); keep them credited where
  they are (README, docs/CREDITS.md, the wikis). Code taken from another project or fork is
  credited in the same commit: CREDITS.md, the wiki's credits page, the commit message and a short
  code comment.
- **No game code, ever:** no decompiled or generated game code, no game files, no records of game
  memory in any repository. Docs and wikis talk about the recompiler only (not about decompiled
  code or private folders).
- **Testing the game:** run it in the background with `runtime/tools/test_run.sh NAME [VAR=...]`
  (bloodborne-runtime's `tools/test_run.sh`): it copies the player's save first and never touches
  the real one. Check `pgrep -x bb-probe` first; one game at a time. Test windows open on the
  user's screen: say so before long batches of runs (bisections), and stop when asked.
- **Never capture the user's screen.** Pictures come from the game itself: `tools/grab.sh NAME`
  (its frame) or `tools/grab.sh --screen NAME` (with the in-game menu). Screenshots in English
  (`BB_LANGUAGE=1 BB_UI_LANGUAGE=en`).
- **Shell:** no `rm -rf` on variable paths (literal paths only); no `xargs ... sh -c '...'` (write
  a script file instead).
- **Docs:** plain, precise English; numbers measured, not guessed. When a milestone or a measured
  number changes, update the docs and the wiki status pages (progress bars: 20 blocks, one per 5%).
