#pragma once
#include "fluid/Particle.h"
#include <vector>

namespace OStrands::Fluid
{
    constexpr std::uint32_t kShortChain = 0x80000000u;

    // Three independently simulated points, with the last at the outlet for anchoring.
    inline void AppendShortChain(std::vector<Particle>& particles, Particle tip, float spacing, std::uint32_t run)
    {
        tip.run = run | kShortChain;
        const Vec3 forward = Normalize(tip.velocity, {0, 0, -1});
        const Vec3 side = Normalize(Cross(forward, {0, 0, 1}), {1, 0, 0});
        Particle middle = tip;
        middle.position += (forward + side * 0.4f) * spacing;
        Particle head = tip;
        head.position += forward * (2.3f * spacing);
        head.link = Length(head.position - middle.position);
        middle.link = Length(middle.position - tip.position);
        particles.push_back(head);
        particles.push_back(middle);
        particles.push_back(tip);
    }
}
