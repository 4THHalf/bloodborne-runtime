// SPDX-License-Identifier: GPL-2.0-or-later
// bbport 0.6's hooks in the game's code (bbport_game_hooks.h). Taken from bbport 0.6's
// bbport_guest_hooks.cpp (8185b88, deadinside28/bloodborne_pc); its x86 detours are written as
// Core::InstallGuestCodeHook observers here, and host code calls the game's functions through
// runtime_guest_call (bbcpu on macOS).
#include "bbport_game_hooks.h"
#include "bbport_effect_flags.h"
#include "bbport_guest_code_hooks.h"
#include "bbport_settings.h"
#include "cpu/bbcpu.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include "common/types.h"

extern "C" {
int runtime_memory_is_mapped(uintptr_t address, uint64_t size);
int runtime_extra_direct_map(uint64_t size, uint64_t alignment, void** out);
uint64_t runtime_guest_call(const void* fn, int count, const uint64_t* args);
}

namespace BbGameHooks {
namespace {
using u64 = std::uint64_t;
using u8 = std::uint8_t;

u64 image_base = 0;

u64 Word(u64 address) {
    return runtime_memory_is_mapped(address, 8) ? *reinterpret_cast<const u64*>(address) : 0;
}

bool Hook(u64 offset, std::span<const u8> code, u64 tag, Core::GuestCodeHook fn) {
    return Core::InstallGuestCodeHook(reinterpret_cast<void*>(image_base + offset), code, tag, fn);
}

// ---- Laurence's cinematic above 30 FPS ----

// The game's render jobs take a slot from a pool and give it back when they end. A job that fails
// (as many do at high frame rates in Laurence's cinematic) kept its slot: the pool ran dry and
// the cinematic hung. The slot now goes back at the end of the failed run (0x1068092), and when a
// runner is destroyed or reset with one still held (0x106798a, 0x1067a5c).
std::atomic<u64> job_slots_released{0};
constexpr u64 GiveBackSlot = 0x1056990; ///< (pool, item)
constexpr u64 SlotPool = 0x54b8788;     ///< the pool's global

void GiveBack(u64 pool, u64 runner) {
    u64& held = *reinterpret_cast<u64*>(runner + 0x120);
    const u64 item = held;
    if (!item || !pool) {
        return;
    }
    const u64 args[2] = {pool, item};
    runtime_guest_call(reinterpret_cast<const void*>(image_base + GiveBackSlot), 2, args);
    held = 0;
}

void PS4_SYSV_ABI JobSlotFix(u64, const Core::GuestRegisterSnapshot* r) {
    // r14 the runner, r12 the pool, r15d the run's result (1: failed), eax the error.
    if (std::uint32_t(r->r15) != 1 || !Word(r->r14 + 0x120)) {
        return;
    }
    GiveBack(r->r12, r->r14);
    const u64 n = job_slots_released.fetch_add(1) + 1;
    if (n <= 5 || (n & (n - 1)) == 0) {
        std::printf("Game hooks: a failed render job gave its slot back (%llu so far); error=%u\n",
                    static_cast<unsigned long long>(n), unsigned(std::uint32_t(r->rax)));
    }
}

void PS4_SYSV_ABI JobSlotDrop(u64, const Core::GuestRegisterSnapshot* r) {
    // rbx the runner, about to clear its held slot.
    if (!Word(r->rbx + 0x120)) {
        return;
    }
    GiveBack(Word(image_base + SlotPool), r->rbx);
    const u64 n = job_slots_released.fetch_add(1) + 1;
    if (n <= 5 || (n & (n - 1)) == 0) {
        std::printf("Game hooks: a render job destroyed with its slot gave it back (%llu so far)\n",
                    static_cast<unsigned long long>(n));
    }
}

bool InstallJobSlotFix() {
    if (const char* env = std::getenv("BB_JOB_SLOT_FIX"); env && env[0] == '0') {
        return false;
    }
    // mov rax, [rbx]; cmp rax, [rbp - 0x30]: the stack check every exit of that function ends in.
    static constexpr std::array<u8, 7> exit_code = {0x48, 0x8b, 0x03, 0x48, 0x3b, 0x45, 0xd0};
    // mov qword [rbx + 0x120], 0
    static constexpr std::array<u8, 11> drop_code = {0x48, 0xc7, 0x83, 0x20, 0x01, 0x00,
                                                     0x00, 0x00, 0x00, 0x00, 0x00};
    return Hook(0x1068092, exit_code, 0, JobSlotFix) & Hook(0x106798a, drop_code, 0, JobSlotDrop) &
           Hook(0x1067a5c, drop_code, 1, JobSlotDrop);
}

// The render resource cache (its range heap set up right after the allocation at 0x216b21d) has
// 160 MiB of its own. At high frame rates it fills before the cinematic's geometry is prepared;
// failed allocations then leave models exploded or missing. 320 MiB still failed on 4 MiB requests
// in the same load; 512 MiB survived that peak. Taken from the game's own allocator the larger
// backing starved what areas load into, so the game allocates its 160 MiB as always and the cache
// gets the port's direct memory past the game's size instead (runtime_extra_direct_map,
// BB_DMEM_EXTRA_MB). BB_RENDER_RESOURCE_CACHE_MB=160: the game's cache as it is.
void PS4_SYSV_ABI ResourceCacheBacking(u64, const Core::GuestRegisterSnapshot* r) {
    constexpr u64 native_bytes = 160ull << 20;
    std::uint32_t requested = 0;
    std::memcpy(&requested, reinterpret_cast<const void*>(r->r14 + 0x54), sizeof(requested));
    if (requested != native_bytes || !r->rax) {
        return;
    }
    static const std::uint32_t bytes = [] {
        const char* env = std::getenv("BB_RENDER_RESOURCE_CACHE_MB");
        const long mb = env ? std::strtol(env, nullptr, 10) : 512;
        return std::uint32_t(std::clamp(mb, 160l, 512l)) << 20;
    }();
    if (bytes == native_bytes) {
        return;
    }
    void* block = nullptr;
    if (const int error = runtime_extra_direct_map(bytes, 64ull << 10, &block); error || !block) {
        std::printf("Render resource cache: no port memory for %u MiB (error %#x): the game's "
                    "160 MiB\n", bytes >> 20, unsigned(error));
        return;
    }
    // The creation record can be a stack object: not necessarily in the runtime's map.
    std::memcpy(reinterpret_cast<void*>(r->r14 + 0x54), &bytes, sizeof(bytes));
    const_cast<Core::GuestRegisterSnapshot*>(r)->rax = reinterpret_cast<u64>(block);
    std::printf("Render resource cache: %u MiB in the port's memory at %p (the game's 160 MiB "
                "stays allocated)\n", bytes >> 20, block);
}

bool InstallResourceCacheSize() {
    // mov rcx, rbx; mov [rcx + 0x10], rax: right after the allocator's Allocate(size, 16) returns.
    static constexpr std::array<u8, 7> code{0x48, 0x89, 0xd9, 0x48, 0x89, 0x41, 0x10};
    return Hook(0x216b21d, code, 0, ResourceCacheBacking);
}
// ---- Effects switched while the game runs ----
// Ambient occlusion (SSAO), motion blur, the game's own anti-aliasing and the shadows of dynamic
// lights are bytes of the renderer's options (+0x2968, +0x2969, +0x296a, +0x296e; the patches
// change the immediates that set them in the constructor). The renderer tests them while it builds
// each frame. The constructor's tail (0x22c254c) reports the options' address; they are written
// there and on every change. Chromatic aberration (0x229faa8), depth of field (0x21d7bbc) and
// bloom (0x21d72f0) are code the patches replace: written back and forth here; the game's own
// bytes are kept as they were found. Screen-space reflections stay a patch at start (it also sizes a target in the
// constructor). BB_HOT_EFFECTS=0: the patches at start, as before (patches.py).
constexpr std::size_t MaxEffectObjects = 64;
std::array<std::atomic<u64>, MaxEffectObjects> effects_objects{};
std::atomic<std::size_t> effects_object_count{0};
std::array<std::atomic<int>, BbSettings::EffectCount> effects_applied{};
std::atomic<bool> hot_effects{false};
struct HotCode {
    int effect;               ///< BbSettings::Effects index
    u64 offset;
    std::array<u8, 12> patch; ///< the community patch's bytes (the effect off)
    std::size_t size;
    std::array<u8, 12> original{};
    bool ready = false;
};
std::array<HotCode, 3> hot_code = {{
    {0, 0x229faa8, {0xc7, 0x83, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90}, 12},
    {1, 0x21d7bbc, {0xe9, 0xdf, 0x00, 0x00, 0x00, 0x90}, 6},
    {7, 0x21d72f0, {0x31, 0xf6, 0x0f, 0x1f, 0x40, 0x00}, 6}, // bloom ("Disable Bloom", Kyo)
}};
struct HotFlag {
    int effect;
    std::uint32_t offset;
};
constexpr std::array<HotFlag, 4> hot_flags = {{{2, 0x2969}, {3, 0x2968}, {4, 0x296a}, {5, 0x296e}}};

/// Writes `size` bytes of code from the last to the first (the first byte, an opcode, is the one
/// that makes the new code reachable); bbcpu drops what it translated from the old bytes.
void WriteHotCode(u64 address, const u8* bytes, std::size_t size) {
    const long page_size = sysconf(_SC_PAGESIZE);
    void* page = reinterpret_cast<void*>(address & ~u64(page_size - 1));
    const std::size_t span = (address + size) - reinterpret_cast<u64>(page);
#if defined(__APPLE__)
    mprotect(page, span, PROT_READ | PROT_WRITE);
#else
    mprotect(page, span, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
    auto* p = reinterpret_cast<u8*>(address);
    for (std::size_t i = size; i-- > 1;) {
        __atomic_store_n(p + i, bytes[i], __ATOMIC_SEQ_CST);
    }
    __atomic_store_n(p, bytes[0], __ATOMIC_SEQ_CST);
    mprotect(page, span, Core::GuestCodeProtection());
    if (bbcpu_enabled()) {
        bbcpu_invalidate(address, size);
    }
}

void ApplyHotEffects(bool force, u64 fresh = 0) {
    auto& settings = BbSettings::Get();
    for (auto& code : hot_code) {
        if (!code.ready) {
            continue;
        }
        const int want = settings.effects[code.effect].load(std::memory_order_relaxed) ? 1 : 0;
        if (!force && effects_applied[code.effect].load() == want) {
            continue;
        }
        effects_applied[code.effect].store(want);
        WriteHotCode(image_base + code.offset, want ? code.original.data() : code.patch.data(), code.size);
    }
    const std::size_t count = std::min(effects_object_count.load(std::memory_order_acquire), MaxEffectObjects);
    std::size_t live_objects = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const u64 object = effects_objects[i].load(std::memory_order_relaxed);
        const bool is_fresh = object == fresh; // just built: certainly an options object
        // The game frees these objects and reuses the memory: written only while its switches
        // (+0x2968..+0x2970) still look like switches and the constants the constructor copies
        // from the game's data (32 bytes at +0x2980) are still whole. A block of zeros once
        // passed for switches and a write there damaged the game's heap.
        u8 block[0x40]{};
        if (!runtime_memory_is_mapped(object + 0x2960, sizeof(block))) {
            continue;
        }
        std::memcpy(block, reinterpret_cast<const void*>(object + 0x2960), sizeof(block));
        bool plausible =
            std::memcmp(block + 0x20, reinterpret_cast<const void*>(image_base + 0x5128a40), 32) == 0;
        for (int b = 8; b <= 0x10; ++b) {
            plausible &= block[b] <= 1;
        }
        if (!plausible && !is_fresh) {
            continue;
        }
        ++live_objects;
        for (const auto& flag : hot_flags) {
            int want = settings.effects[flag.effect].load(std::memory_order_relaxed) ? 1 : 0;
            if (flag.effect == 3) {
                // Menus and auxiliary contexts can have no SSAO target: the game's own choice is
                // kept (a forced SSAO there crashed after selecting an item).
                want = BbEffectFlags::SsaoEnabled({reinterpret_cast<const u8*>(object), 0x29a0}, want != 0);
            }
            auto* byte = reinterpret_cast<volatile u8*>(object + flag.offset);
            if (force || *byte != want) {
                __atomic_store_n(const_cast<u8*>(byte), u8(want), __ATOMIC_RELAXED);
            }
        }
    }
    static std::atomic<int> told{0};
    if (force && told.fetch_add(1) < 8) {
        std::printf("Game hooks: effects written to %zu of %zu renderer options objects (the rest "
                    "are gone)\n", live_objects, count);
    }
}

/// The renderer options object is built (rbx at the constructor's tail): its switches follow the
/// settings from the start.
void PS4_SYSV_ABI EffectsConstructed(u64, const Core::GuestRegisterSnapshot* r) {
    const u64 object = r->rbx;
    const std::size_t known = std::min(effects_object_count.load(std::memory_order_acquire), MaxEffectObjects);
    bool seen = false;
    for (std::size_t i = 0; i < known; ++i) {
        seen |= effects_objects[i].load(std::memory_order_relaxed) == object;
    }
    if (!seen) {
        const std::size_t at = effects_object_count.fetch_add(1);
        if (at < MaxEffectObjects) {
            effects_objects[at].store(object, std::memory_order_release);
        } else {
            effects_object_count.store(MaxEffectObjects);
        }
    }
    ApplyHotEffects(true, object);
}

bool InstallHotEffects() {
    if (const char* env = std::getenv("BB_HOT_EFFECTS"); env && env[0] == '0') {
        return false;
    }
    // mov qword [rbx + 0x2977], 0
    static constexpr std::array<u8, 11> tail = {0x48, 0xc7, 0x83, 0x77, 0x29, 0x00,
                                                0x00, 0x00, 0x00, 0x00, 0x00};
    // The code effects: the game's own bytes, taken before anything is written.
    for (auto& code : hot_code) {
        const u8* at = reinterpret_cast<const u8*>(image_base + code.offset);
        if (std::memcmp(at, code.patch.data(), code.size) == 0) {
            continue; // the startup patch is in: left as it is
        }
        std::memcpy(code.original.data(), at, code.size);
        code.ready = true;
    }
    if (!Hook(0x22c254c, tail, 0, EffectsConstructed)) {
        return false;
    }
    for (auto& code : hot_code) {
        if (code.ready) {
            effects_applied[code.effect].store(-1);
#ifdef BB_HAVE_BBCPU
            // Rewritten while the game runs: never taken from the recompiled library.
            bbcpu_recomp_exclude(image_base + code.offset, code.size);
#endif
        }
    }
    ApplyHotEffects(true); // the code effects before the game runs
    hot_effects = true;
    return true;
}
} // namespace

void ApplyGameEffects() {
    if (hot_effects.load(std::memory_order_relaxed)) {
        ApplyHotEffects(false);
    }
}

bool HotEffects() {
    return hot_effects.load(std::memory_order_relaxed);
}

void PatchImage(unsigned char* image, std::uint64_t) {
    // BB_GAME_HOOKS=0 (comparisons): none of bbport 0.6's game hooks.
    if (const char* env = std::getenv("BB_GAME_HOOKS"); env && env[0] == '0') {
        std::printf("Game hooks: off (BB_GAME_HOOKS=0)\n");
        return;
    }
    image_base = reinterpret_cast<u64>(image);
    std::printf("Game hooks: render job slots %s; render resource cache %s; effects %s\n",
                InstallJobSlotFix() ? "given back" : "not installed",
                InstallResourceCacheSize() ? "in the port's memory" : "not installed",
                InstallHotEffects() ? "switched live" : "at start (patches)");
}
} // namespace BbGameHooks
