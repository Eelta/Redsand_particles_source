#pragma once

#include "util.hpp"

template <typename T, typename U>
concept easing = std::is_arithmetic_v<U> && requires(float a, float b, U from, U to) {
    T::interpolate(a, b, from, to);
};

namespace easings
{
    template <arithmetic T> struct linear
    {
        static T interpolate(float time, float duration, T from, T to)
        {
            const float x = time / duration;
            return std::lerp(from, to, x);
        }
    };

    template <arithmetic T> struct in_back
    {
        static T interpolate(float time, float duration, T from, T to)
        {
            const float a = 1.70158f;
            const float b = 1 + a;
            const float x = time / duration;

            return std::lerp(from, to, b * std::powf(x, 3) - a * std::powf(x, 2));
        }
    };

    template <arithmetic T> struct testing
    {
        static T interpolate(float time, float duration, T from, T to)
        {
            const float x = time / duration;
            return x < 0.5f ? std::lerp(from, to, std::powf(1 - (x / 0.5f), 3))
                            : std::lerp(from, to, std::powf(1 - (x / 0.5f), 2));



        }
    };

    using linear_f = linear<float>;
    using in_back_f = in_back<float>;
}
