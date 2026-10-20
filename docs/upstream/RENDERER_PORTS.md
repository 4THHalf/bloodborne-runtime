# Renderer changes from bbport

From N4 onward, the shared renderer core and Vulkan backend are maintained here. For each bbport
release, merge non-renderer changes as usual. Review changes to `renderer_vulkan/`,
`buffer_cache/`, `texture_cache/` and `shader_recompiler/` individually. Record what was ported,
skipped or deferred, why, and the build and game measurements that checked a port.

| bbport release | Renderer changes | Decision |
|---|---|---|
| 0.51 (2026-10-11) | Buffer-cache handling of GPU-written copies and low-VRAM allocation; texture-cache copy and eviction changes; swapchain retirement on Wayland; DLSS and temporal-upscaler changes and diagnostics. No shader-recompiler files changed from 0.5 in the four tracked directories. | Full release merge completed before N4. These changes are already in the Vulkan reference path; no hand port was needed. |
| 0.6 (2026-10-19, `8185b88`) | Push constants written after the render pass begins: the occlusion results' compute dispatch, recorded when a pass begins, overwrote the draw's push constants (single frames shifted by half the screen). Draws without a pixel shader rebuild their fragment runtime info from their own registers instead of keeping the previous draw's (clip-distance discard shader). | Ported by hand into `vk_rasterizer.cpp` (both draw paths) and `vk_pipeline_cache.cpp`. Copied-save runs on MoltenVK and Metal: correct picture, no faults. The second fix cannot act on macOS (clip-distance emulation is NVIDIA-only). |

The 2026-10-12 pre-N4 check found upstream HEAD at the 0.51 release commit
`3c57da4198ad62039f63f33bfd205b9916518acf`. There was no newer release to merge.

## Windows-fork ports

The Supermedo Windows branch was reviewed at `e5d04e2` (1.7.2) on 2026-10-13. Changes are
ported individually; this does not change the imported bbport compatibility level of 0.51.
N4 remains paused.

| Source | Change and decision | Verification |
|---|---|---|
| [Supermedo/bloodborne_pc `3a7cde8`](https://github.com/Supermedo/bloodborne_pc/commit/3a7cde8389e58423a9a33b41eb6380a0f497d3f4), João Pedro Valadares | Ported stable `SlotVector` storage and reacquisition of image references after inserting depth/stencil images. The old pool moved live objects during growth while callers retained references. Reserve 1 GiB of virtual address space per pool and make 2 MiB chunks writable as needed. This consumes address space and metadata; it is not a 1 GiB physical allocation. POSIX uses `mprotect` inside its existing reservation; Windows retains reserve/commit operations. Exhaustion and page-operation failures are fatal. | `build.sh --test` passes on macOS arm64. A 50,000-entry test uses non-movable, aligned objects whose entries straddle chunk boundaries; address stability, iteration, reuse and destruction also pass AddressSanitizer and UndefinedBehaviorSanitizer. Linux CI builds the runtime and runs this test. One isolated 90-second copied-save hold completed: 48.2 FPS, p90 21.8 ms, sampled game CPU 504%, GPU utilization 98%. No fault or GPU assertion appeared. This is a smoke check, not an A/B performance measurement. Windows page operations were retained but not tested here. |

This fixes a source-level lifetime defect. It has not been established as the cause of the
historical sampler assertions or video flashes, and no performance improvement is claimed.

### Texture upload baselines

Ported [Supermedo/bloodborne_pc `7e8bd6c1`](https://github.com/Supermedo/bloodborne_pc/commit/7e8bd6c1c85661cf0912531509ace5cf7cfc05a0),
authored by devilismyfriend and committed by ayozetr, with
[Supermedo's `136a72d0`](https://github.com/Supermedo/bloodborne_pc/commit/136a72d0c2b79efc4f7d6f8e163ae6783c388d4a)
cost limit. Our existing whole-image hash-range fix from bmy/bbport-mac remains.

An upload records a guest-memory baseline for mips of at most 64 KiB, writable images, and
GPU-modified images not marked for a newer buffer upload. Later unchanged guest bytes do not
replace GPU-produced image contents. Actual CPU changes and newer GPU buffer sources still
refresh the image. Baselines have explicit validity and are independent per mip. Large streamed
textures are not hashed merely to establish a baseline. Hashes in precise-readback mode read
through the existing backing view, using reusable thread-local scratch storage.

Baseline capture remains opt-in (`BB_IMAGE_UPLOAD_BASELINE=1`); unset or `=0` retains the
previous baseline timing. Both modes retain explicit baseline representation, safe backing-view
reads and the whole-image guard against skipping newer GPU buffer sources. The control mode
is not a byte-for-byte old build. The startup log reports the selected mode.

`build.sh --test` includes `image-upload-hash-test`, which checks GPU preservation, real CPU
writes beyond the first pixels, newer GPU buffer imports, first storage binding, independent
mips, a zero-valued digest and the hashing limit. It passes on macOS arm64 and under
AddressSanitizer and UndefinedBehaviorSanitizer. Linux CI builds and runs this standalone test.
It exercises the upload decision with synthetic data; it does not emulate Vulkan image uploads
or prove correctness of every aliasing and synchronization case.

`BB_IMAGE_UPLOAD_HASH_STATS=1` now measures aggregate hash calls, bytes and elapsed time in
shared, extra-small and extra-writable buckets; it records no guest addresses or contents.
A capped diagnosis found approximately four large writable mip hashes per frame, costing
0.05–0.06 ms per frame at 40 FPS. Small-mip capture stopped after loading. Two 45-second
capped off/on comparisons, one instrumented and one with counters off, did not show a
repeatable stage-B/game-CPU regression. The earlier uncapped difference remains unresolved,
so capture remains opt-in. Full hash checks are retained; no unsafe digest caching was added.
Details: [macOS performance](../MACOS_PERFORMANCE.md#hash-cost-diagnosis-and-capped-repeat-2026-10-13).

A subsequent uncapped off/on Metal trace pair, normalized to 168/205 complete Metal frames,
measured 502.9/499.8 encoders and 18.70/18.17 ms GPU busy time per frame. Main dispatch,
copy and clear counts matched closely; the trace did not show increased GPU work on.
Post-trace holds measured 47.2/46.9 FPS and 22.9/23.2 ms p90. The earlier larger uncapped
difference remains unresolved; keep baseline capture opt-in. The trace reader's new
`--complete-frames` option and six synthetic tests make the normalization reproducible.
[Measurements and limits](../MACOS_PERFORMANCE.md#uncapped-gpu-trace-comparison-2026-10-13).

### Motion-vector pipeline preload guard

Ported [Supermedo/bloodborne_pc `f33e7617`](https://github.com/Supermedo/bloodborne_pc/commit/f33e7617bc1486dec0698786bd87baa40c02eab7),
authored by João Pedro Valadares. `LoadGraphicsPipeline` rejects motion-vector keys before
loading stage metadata or creating Vulkan objects. Cached metadata can describe a plain
shader permutation, which bypasses the existing motion-vertex metadata check. Motion shaders
also embed session-local buffer addresses. These pipelines rebuild at first use; ordinary
graphics and compute pipelines still preload. Serialization versions are unchanged because
this changes admission, not the cache format. This applies to the Vulkan backend on all hosts.

`build.sh --test` passed on macOS arm64. The untraced copied-save run `mv_cache_warm`
completed a 45-second hold: 44.8 FPS, p90 24.7 ms, sampled game CPU 486%, GPU utilization
97% and stage-B time 4.63 us/draw (last eight log windows). No fault, GPU assertion or
invalid-mutex panic appeared. This is a startup/gameplay smoke check, not an A/B speed claim.
The default motion-vector setting was off, so these runs do not reproduce the foreign
motion-cache failure or validate motion output. No motion-cache skip count was measured.

The fresh-save opening `mv_cache_opening` used the warm shared cache with `BB_GPU_LOG=info`:
372 pipelines preloaded, playback completed, and the maintainer reported perfect playback.
The game frame dump showed character creation. Neither run logged a fault, GPU assertion or
invalid-mutex panic. Historical assertion counts are unchanged; N4 remains paused.

### Fetch shaders in cached pipelines (bbport 0.6-pre1)

Ported from [deadinside28/bloodborne_pc `72e5c8c`](https://github.com/deadinside28/bloodborne_pc/commit/72e5c8ce22ec0fa39481bc81aefbb761869d0691)
(0.6-pre1). `LoadShaderMeta` keeps the vertex stage's fetch shader when a later stage (geometry)
has none, instead of erasing it, and `LoadGraphicsPipeline` clears the previous entry's stages
before loading the next. Without it, a cached pipeline with a geometry stage was built with an
empty fetch shader. Serialization versions are unchanged: the cache format is the same. This was
tested during the stretched-geometry investigation; it did not cause that symptom
([stability notes](../STABILITY.md#stretching-the-cause-and-the-fix-2026-10-18)). The rest of
0.6-pre1 (its new memory model and own renderer) has not been reviewed for porting.

## bbport 0.6 and Windows fork review (2026-10-19)

bbport 0.6 (`8185b88`) and the Supermedo Windows branch up to `de2e38a` (1.9.2, from `e5d04e2`)
were reviewed. bbport 0.6 replaced shadPS4's layout with its own renderer, so its changes are
ported by hand; the imported compatibility level stays 0.51. Planned items are tracked in
bloodborne-mac's `docs/ROADMAP.md`.

| Source | Decision |
|---|---|
| bbport 0.6: blur-kernel assertion `0xa0020007` | Ported (`runtime_kernel.c`). Under bbcpu the call site is the import's guest return address (`bbcpu_import_return`) instead of a host stack scan. |
| bbport 0.6: larger GX per-frame command heap | Ported as a patch, on by default (`BB_GX_HEAP=0`: off): bbport also guards the sites with hooks, which this port does not have. |
| bbport 0.6: thread records below 40 bits | Not needed: the runtime's C allocations already come from its own heap below 1 TiB (`runtime_heap.h`). |
| Fork `3e3b760` (parsifal295): impossible number conversion | Ported, and extended to `SurfaceFormat` (our watch-list assertions `6/1`, `15/4`): the same data format with an existing number format, logged. |
| Fork `c07f0ff` (parsifal295): garbage PM4 header | Ported: the rest of the buffer is dropped, the first 16 reported with `ReportBadPacket`; `BB_PM4_STOP=1` stops as before. |
| Fork `2c4ddeb`: null vertex-buffer table | Ported, with a log line for each null table read. bbport 0.6 fixes the same crash ("139"). |
| Fork `76f5b5f` (from Awaken's port): flip IRQ ahead of its label | Ported. |
| bbport 0.6: Laurence's cinematic, pixel-shader-less pipelines, push constants after the pass begins, frame-rate/input/effect/aspect hooks, online module | Deferred: on the roadmap. Hooks that emit x86 thunks become bbcpu trap handlers. |
| Fork `9566a1d`, `5012c6f` (stretched polygons) | Skipped: bugs of the fork's shadPS4 0.20.1 base (56 bindings, index capacity clamp); our push block and index binding do not have them. |
| Fork `08cd804` (barrier memo) | Already here (`AccessMemo`). |
| Fork `4f3dd91` (`VK_EXT_device_fault`) | Skipped: MoltenVK lacks it. |
| bbport 0.6 new memory model, fork VRAM budget changes, NVIDIA/Intel/Windows/DLSS/frame generation/FSR 4.1.1 | Not applicable on this port's unified pool and Apple GPUs. |

Checks: `build.sh` and the runtime tests (unchanged pre-existing failures). Two copied-save
playtests (`batchA_lamps`, `batchA_play2`; lamp travel, fights) ran without a fault, assertion
or any of the new fallbacks firing; the maintainer reported no regression. Blood decals drawn
pale and an enemy's body drawn as colour noise were reported in these runs as older bugs.

## macOS Metal renderer ports

| Source | Change and decision | Verification |
|---|---|---|
| [Wooloomooloo2/bloodborne_mac_port `0b50589`](https://github.com/Wooloomooloo2/bloodborne_mac_port/tree/0b50589) (with Huax-1839/bloodborne-macos `ee3120a`) | Adapted the Metal backend's shader compiler, shader rewrites, pipeline-state cache and format tables into `renderer_metal/`; split the pipeline keys out and shared the blend rules. Their Metal-only renderer is not merged (ours stays multiplatform). | `build.sh --test` on macOS arm64. `BB_METAL_SHADOW=1` copied-save run: 465 graphics and 35 compute pipelines built in Metal, 0 failed, 57.6 FPS with MoltenVK drawing. |
| [Wooloomooloo2/bloodborne_mac_port `78b59f5`](https://github.com/Wooloomooloo2/bloodborne_mac_port/tree/78b59f5) | Adapted its Vulkan-to-Metal command layer (`mtl_vk_layer`), frame encoder (`mtl_frame`) and shadow builder (`mtl_shadow`, now on `PipelineFactory`), plus `mtl_depth_stencil`, `mtl_object` and `mtl_raster_util` from `0b50589`. Behind `BB_METAL_PRESENT` / `BB_METAL_FRAME`; MoltenVK stays the default. | Shadow run: 550 pipelines (510 graphics, 40 compute), 0 failed. Two alternating 60-second pairs: MoltenVK 58.0/58.1 FPS, Metal frame 65.5/61.7 FPS, p90 20.4/18.9 vs 16.3/17.6 ms; same picture; no faults. |
