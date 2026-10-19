#include "../source/RedsandSlashFrame.h"

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace RedsandSlash;

void Check(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
bool Near(float a, float b) { return std::abs(a - b) < 1e-5f; }
void Orthonormal(const Frame& f)
{
	Check(Finite(f.x) && Finite(f.y) && Finite(f.z), "non-finite basis");
	Check(Near(Dot(f.x, f.x), 1) && Near(Dot(f.y, f.y), 1) && Near(Dot(f.z, f.z), 1), "basis changes asset scale");
	Check(Near(Dot(f.x, f.y), 0) && Near(Dot(f.x, f.z), 0) && Near(Dot(f.y, f.z), 0), "basis skews asset plane");
	Check(Near(Dot(Cross(f.x, f.y), f.z), 1), "basis mirrors the blade");
}

int main()
{
	try {
		const auto horizontal = BuildFrame({ 250, 0, 0 }, { 0, 0, 5 }, { 0, -1, 0 });
		Check(horizontal && !horizontal->fallbackAxis, "measured blade axis not used");
		Orthonormal(*horizontal);
		Check(Near(horizontal->x.x, 1) && Near(horizontal->y.z, 1) && Near(horizontal->z.y, -1), "local XY does not follow motion/blade axes");
		const auto reverse = BuildFrame({ -250, 0, 0 }, { 0, 0, 5 }, { 0, -1, 0 });
		Check(reverse && Near(reverse->x.x, -1) && Near(reverse->y.z, 1) && Near(reverse->z.y, 1), "reverse swing lost its handedness");
		Orthonormal(*reverse);
		const auto diagonal = BuildFrame({ 50, 70, -20 }, { 4, 3, 6 }, { 1, 0, 0 });
		Check(diagonal && !diagonal->fallbackAxis, "oblique blade incorrectly treated as degenerate");
		Orthonormal(*diagonal);
		Check(Dot(diagonal->x, *Unit({ 50, 70, -20 })) > .99999f, "swing direction changed");
		Check(Dot(diagonal->y, *Unit({ 4, 3, 6 })) > 0, "blade projection flipped");
		for (const auto axis : { Vec3{}, Vec3{ 2, 0, 0 }, Vec3{ std::numeric_limits<float>::quiet_NaN(), 0, 0 } }) {
			for (const auto normal : { Vec3{ 0, 0, 1 }, Vec3{ 1, 0, 0 }, Vec3{} }) {
				const auto fallback = BuildFrame({ 10, 0, 0 }, axis, normal);
				Check(fallback && fallback->fallbackAxis, "degenerate capsule did not use a stable fallback");
				Orthonormal(*fallback);
			}
		}
		const auto normalFallback = BuildFrame({ 1, 0, 0 }, {}, { 0, 0, -1 });
		Check(normalFallback && Near(normalFallback->z.z, -1), "contact-plane fallback ignored normal");
		Check(!BuildFrame({}, { 0, 1, 0 }, { 0, 0, 1 }), "zero-velocity hit invented a swing");
		Check(!BuildFrame({ std::numeric_limits<float>::infinity(), 0, 0 }, {}, {}), "invalid velocity accepted");
		const auto large = BuildFrame({ 1e30f, 1e30f, 0 }, { 0, 0, 1 }, {});
		Check(large.has_value(), "finite large vector overflowed"); Orthonormal(*large);
		std::cout << "Blade slash: measured axes, reversal, oblique projection, normal fallback and finite right-handed bases passed\n";
	} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
