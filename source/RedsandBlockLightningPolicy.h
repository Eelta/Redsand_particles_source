#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace RedsandBlockLightning
{
    inline constexpr unsigned branchCount = 60;
    inline constexpr unsigned pieceCount = branchCount;
    inline constexpr float duration = 1.55f;
    inline float Smooth(float t) { t = std::clamp(t, 0.f, 1.f); return t*t*(3.f-2.f*t); }
    inline float Variation(unsigned key) { return ((key*73u+19u)%101u)/100.f; }
    struct Timing { float birth, travel, tailDelay, tailTravel; };
    inline Timing Schedule(unsigned branch)
    {
        const auto part = branch % 5;
        const auto group = branch/5;
        const Timing primary{((group*7u)%12u)*.023f+Variation(group+3)*.018f,
            .24f+.09f*Variation(group+7), .38f+.16f*Variation(group+11), .38f+.12f*Variation(group+17)};
        if (part == 0) return primary;
        const auto side = part < 3 ? 1u : 3u;

        const float attach = side == 1 ? .395833333f : .677083333f;
        const float reach = .5f-std::sin(std::asin(1.f-2.f*attach/1.08f)/3.f);
        const Timing secondary{primary.birth+primary.travel*reach+.012f,
            .16f+.07f*Variation(group*5+side), .29f+.14f*Variation(group*5+side+23), .27f+.12f*Variation(group*5+side+29)};
        if (part == side) return secondary;
        const float twigReach = .5f-std::sin(std::asin(1.f-2.f*.55f/1.08f)/3.f);
        return {secondary.birth+secondary.travel*twigReach+.012f,
            .12f+.05f*Variation(branch+31), .22f+.12f*Variation(branch+37), .23f+.09f*Variation(branch+41)};
    }
    struct Piece { unsigned branch, level, index, count; float begin, end; };
    inline const std::array<Timing, branchCount>& Timings()
    {
        static const auto values = [] { std::array<Timing, branchCount> result{};
            for (unsigned i = 0; i < branchCount; ++i) result[i] = Schedule(i); return result; }();
        return values;
    }
    inline Piece Describe(unsigned piece)
    {
        const auto part = piece%5;
        const auto level = part == 0 ? 0u : part == 1 || part == 3 ? 1u : 2u;
        return {piece, level, 0, 1, 0.f, 1.f};
    }
    struct Pose { float head, tail, alpha, x, y, z; };
    inline Pose Animate(unsigned piece, float elapsed)
    {
        const auto info = Describe(piece);
        const auto& timing = Timings()[info.branch];
        const float age = elapsed-timing.birth;
        const float head = 1.08f*Smooth(age/timing.travel);
        const float tail = -.08f+1.20f*Smooth((age-timing.tailDelay)/timing.tailTravel);
        const float alpha = Smooth(age/.018f)*(1.f-Smooth((tail-.92f)/.20f));

        return {head, tail, alpha, 0.f, 0.f, 0.f};
    }
    inline bool Visible(unsigned piece, const Pose& pose)
    {
        const auto info = Describe(piece);
        return pose.alpha > .0001f && pose.head > info.begin && pose.tail < info.end && pose.head > pose.tail;
    }
    inline float MaskScale(const Pose& pose) { return 1.f/std::max(.025f, pose.head-pose.tail); }
    inline float MaskOffset(const Pose& pose) { return -pose.tail*MaskScale(pose); }
    inline float Profile(float u)
    {
        return Smooth(u/.065f)*(1.f-Smooth((u-.86f)/.14f));
    }
    inline float Opacity(const Pose& pose, float pathU)
    {
        return pose.alpha*Profile(pathU*MaskScale(pose)+MaskOffset(pose));
    }
    inline bool Selected(bool enabled, bool probabilistic, float chance, float roll)
    {
        return enabled && (!probabilistic || (std::isfinite(roll) && roll >= 0.f && roll < std::clamp(chance, 0.f, 1.f)));
    }


    struct Gate
    {
        struct Entry { std::uint32_t cause = 0; double time = -100.; };
        std::array<Entry, 16> recent{};
        unsigned next = 0;
        bool Accept(std::uint32_t cause, double now)
        {
            for (const auto& hit : recent) { if (hit.cause == cause && now-hit.time >= 0 && now-hit.time < .09) return false; }
            recent[next++ % recent.size()] = {cause, now}; return true;
        }
        void Reset() { *this = Gate{}; }
    };
}
