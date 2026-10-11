# Documentation

[← Back to the project](../README.md)

## Playing

The player guides (installation, game files, settings, mods, troubleshooting) are in
[bloodborne-mac](https://github.com/bloodborne-native/bloodborne-mac/tree/main/docs/guide).

## Developing

- [Building from source](guide/building.md): build, run, environment variables, tools.
- [How it works](guide/how-it-works.md): the translator, the runtime, the renderer.
- [macOS performance](MACOS_PERFORMANCE.md): measurements, what was changed, what could still be
  gained.
- The CPU layer and the recompiler: [bloodborne-recomp](https://github.com/bloodborne-native/bloodborne-recomp)
  ([native arm64](https://github.com/bloodborne-native/bloodborne-recomp/blob/main/docs/ARM64_NATIVE.md),
  [recompilation](https://github.com/bloodborne-native/bloodborne-recomp/blob/main/docs/RECOMPILATION.md)).

## Project

- [Credits and licenses](CREDITS.md)

## Upstream notes

Design notes inherited from [bbport](https://github.com/deadinside28/bloodborne_pc), the Linux port
this project is built on. Most of them are in Russian, and they describe the Linux version.

- [Upstream README](upstream/README.md) ([Русский](upstream/README.ru.md))
- [Roadmap](ROADMAP.md), [development log](DEVELOPMENT_LOG.ru.md), [mods](MODS.md)
- Renderer: [upscaling](upscaler.md), [motion vectors](motion_vectors.md),
  [depth-adaptive TAA](DEPTH_ADAPTIVE_TAA.md), [parallel GPU processing](parallel_gpu.md),
  [TAA review](TEMPORAL_REVIEW_2026-10-01.md)
- Change notes: [2026-10-02](CHANGES_2026-10-02.md), [2026-10-03](CHANGES_2026-10-03.md),
  [2026-10-06](CHANGES_2026-10-06.md), [history/](history)
