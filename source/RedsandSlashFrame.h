#pragma once

#include <cmath>
#include <optional>

namespace RedsandSlash
{
	struct Vec3
	{
		float x = 0, y = 0, z = 0;
		Vec3 operator+(Vec3 other) const { return { x + other.x, y + other.y, z + other.z }; }
		Vec3 operator-(Vec3 other) const { return { x - other.x, y - other.y, z - other.z }; }
		Vec3 operator*(float scalar) const { return { x * scalar, y * scalar, z * scalar }; }
	};
	inline bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
	inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	inline std::optional<Vec3> Unit(Vec3 v)
	{
		if (!Finite(v)) { return {}; }
		const double squared = double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z;
		if (squared < 1e-10) { return {}; }
		return v * static_cast<float>(1.0 / std::sqrt(squared));
	}
	struct Frame
	{

		Vec3 x, y, z;
		bool fallbackAxis = false;
	};
	inline std::optional<Frame> BuildFrame(Vec3 velocity, Vec3 bladeAxis, Vec3 contactNormal)
	{
		const auto motion = Unit(velocity);
		if (!motion) { return {}; }
		const auto x = *motion;
		const auto blade = Unit(bladeAxis);
		const auto projected = blade ? Unit(*blade - x * Dot(*blade, x)) : std::optional<Vec3>{};
		auto y = projected;
		if (!y) {


			if (const auto normal = Unit(contactNormal)) { y = Unit(Cross(*normal, x)); }
			if (!y) {
				const auto ax = std::abs(x.x), ay = std::abs(x.y), az = std::abs(x.z);
				const Vec3 leastParallel = ax <= ay && ax <= az ? Vec3{ 1, 0, 0 } : ay <= az ? Vec3{ 0, 1, 0 } : Vec3{ 0, 0, 1 };
				y = Unit(Cross(leastParallel, x));
			}
		}
		if (!y) { return {}; }
		const auto z = Unit(Cross(x, *y));
		if (!z) { return {}; }
		return Frame{ x, Cross(*z, x), *z, !projected.has_value() };
	}
}
