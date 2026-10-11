// SPDX-FileCopyrightText: Copyright 2026 bloodborne-native contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <cmath>
#include "shader_recompiler/frontend/fixed_quad_tessellation.h"

static float SignedArea(std::span<const Shader::FixedQuadPoint> vertices, bool clockwise) {
    float area = 0;
    for (size_t i = 0; i < vertices.size(); i += 3) {
        const auto a = vertices[i], b = vertices[i + 1], c = vertices[i + 2];
        const float twice = (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
        assert(clockwise ? twice < 0 : twice > 0);
        area += twice * 0.5f;
    }
    return area;
}

int main() {
    const auto one = Shader::FixedQuadTriangles(1, 1);
    const auto three = Shader::FixedQuadTriangles(3, 1);
    assert(one.size() == 6);
    assert(three.size() == 36);
    assert(std::abs(SignedArea(one, false) - 1.f) < 1e-6f);
    assert(std::abs(SignedArea(three, false) - 1.f) < 1e-6f);
    assert(std::abs(SignedArea(Shader::FixedQuadTriangles(1, 1, true), true) + 1.f) < 1e-6f);
    assert(std::abs(SignedArea(Shader::FixedQuadTriangles(3, 1, true), true) + 1.f) < 1e-6f);
    assert(Shader::FixedQuadTriangles(2, 1).empty());
    assert(Shader::FixedQuadTriangles(3, 2).empty());
}
