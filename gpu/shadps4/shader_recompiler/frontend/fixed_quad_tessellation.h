// SPDX-FileCopyrightText: Copyright 2026 bloodborne-native contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace Shader {

struct FixedQuadPoint {
    float u;
    float v;
};

// Equal-spacing quad tessellation for the fixed factor pairs used by the one-control-point
// light patches. The outer-3/inner-1 case has a two-segment (degenerate) inner rectangle:
// its single center vertex joins the twelve boundary segments. Other factor pairs must use
// the ordinary tessellator. The triangle order within each patch is implementation-dependent.
inline constexpr std::array<FixedQuadPoint, 6> FixedQuadOneTriangles{{
    {0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f},
    {0.f, 0.f}, {1.f, 1.f}, {0.f, 1.f},
}};

inline constexpr std::array<FixedQuadPoint, 36> FixedQuadThreeTriangles = [] {
    constexpr float third = 1.f / 3.f;
    constexpr std::array<FixedQuadPoint, 12> edge{{
        {0.f, 0.f}, {third, 0.f}, {2.f * third, 0.f}, {1.f, 0.f},
        {1.f, third}, {1.f, 2.f * third}, {1.f, 1.f}, {2.f * third, 1.f},
        {third, 1.f}, {0.f, 1.f}, {0.f, 2.f * third}, {0.f, third},
    }};
    std::array<FixedQuadPoint, 36> vertices{};
    for (std::uint32_t i = 0; i < edge.size(); ++i) {
        vertices[i * 3] = {0.5f, 0.5f};
        vertices[i * 3 + 1] = edge[i];
        vertices[i * 3 + 2] = edge[(i + 1) % edge.size()];
    }
    return vertices;
}();

template <std::size_t N>
constexpr std::array<FixedQuadPoint, N> ReverseFixedQuadWinding(
    std::array<FixedQuadPoint, N> vertices) {
    for (std::size_t i = 0; i < N; i += 3) {
        const auto second = vertices[i + 1];
        vertices[i + 1] = vertices[i + 2];
        vertices[i + 2] = second;
    }
    return vertices;
}

inline constexpr auto FixedQuadOneClockwise = ReverseFixedQuadWinding(FixedQuadOneTriangles);
inline constexpr auto FixedQuadThreeClockwise = ReverseFixedQuadWinding(FixedQuadThreeTriangles);

inline constexpr std::span<const FixedQuadPoint> FixedQuadTriangles(std::uint32_t outer,
                                                                   std::uint32_t inner,
                                                                   bool clockwise = false) {
    if (inner != 1) return {};
    if (outer == 1) return clockwise ? std::span<const FixedQuadPoint>{FixedQuadOneClockwise}
                                    : std::span<const FixedQuadPoint>{FixedQuadOneTriangles};
    if (outer == 3) return clockwise ? std::span<const FixedQuadPoint>{FixedQuadThreeClockwise}
                                    : std::span<const FixedQuadPoint>{FixedQuadThreeTriangles};
    return {};
}

} // namespace Shader
