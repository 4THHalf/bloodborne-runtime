# Renderer changes from bbport

From N4 onward, the shared renderer core and Vulkan backend are maintained here. For each bbport
release, merge non-renderer changes as usual. Review changes to `renderer_vulkan/`,
`buffer_cache/`, `texture_cache/` and `shader_recompiler/` individually. Record what was ported,
skipped or deferred, why, and the build and game measurements that checked a port.

| bbport release | Renderer changes | Decision |
|---|---|---|
| 0.51 (2026-10-11) | Buffer-cache handling of GPU-written copies and low-VRAM allocation; texture-cache copy and eviction changes; swapchain retirement on Wayland; DLSS and temporal-upscaler changes and diagnostics. No shader-recompiler files changed from 0.5 in the four tracked directories. | Full release merge completed before N4. These changes are already in the Vulkan reference path; no hand port was needed. |

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
