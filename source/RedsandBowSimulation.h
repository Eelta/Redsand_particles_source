#pragma once

#include "RedsandSettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>


namespace Redsand::Bow
{
	struct Vec3
	{
		float x = 0.f, y = 0.f, z = 0.f;
		Vec3 operator+(Vec3 b) const { return { x + b.x, y + b.y, z + b.z }; }
		Vec3 operator-(Vec3 b) const { return { x - b.x, y - b.y, z - b.z }; }
		Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
		float Dot(Vec3 b) const { return x * b.x + y * b.y + z * b.z; }
		float Length() const { return std::sqrt(Dot(*this)); }
		bool Finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
		Vec3 Cross(Vec3 b) const { return { y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x }; }
		Vec3 Unit() const { const float n = Length(); return n > 0.0001f ? *this * (1.f / n) : Vec3{ 0.f, 1.f, 0.f }; }
	};

	enum class Kind { Gather, Ring, Trail };
	enum class TrailStyle { Default, VanillaBow, VanillaCrossbow };

	struct FlightSegment
	{
		Vec3 from, to;
		float delta = 0.f;
		bool continuous = false;
		std::uint64_t sequence = 0, lastSequence = 0;
	};

	class FlightPath
	{
	public:
		static constexpr std::size_t capacity = 16;
		std::size_t Size() const { return count; }
		const FlightSegment* begin() const { return segments.data(); }
		const FlightSegment* end() const { return segments.data() + count; }

		void Add(FlightSegment segment)
		{
			if (!segment.from.Finite() || !segment.to.Finite() || !std::isfinite(segment.delta) || segment.delta <= 0.f) { return; }
			if (!segment.lastSequence) { segment.lastSequence = segment.sequence; }
			for (std::size_t i = 0; i < count; ++i) {
				if (segment.sequence >= segments[i].sequence && segment.lastSequence <= segments[i].lastSequence) { return; }
			}
			std::array<FlightSegment, capacity + 1> ordered;
			std::copy_n(segments.begin(), count, ordered.begin());
			ordered[count] = segment;
			auto size = count + 1;
			std::sort(ordered.begin(), ordered.begin() + size, [](const auto& left, const auto& right) { return left.sequence < right.sequence; });
			if (size > capacity) {
				std::size_t merge = size;
				float error = std::numeric_limits<float>::max();
				for (std::size_t i = 0; i + 1 < size; ++i) {
					const auto& first = ordered[i];
					const auto& second = ordered[i + 1];
					if ((first.to - second.from).Length() > 0.01f || !first.continuous || !second.continuous) { continue; }
					const Vec3 chord = second.to - first.from;
					const float lengthSquared = chord.Dot(chord);
					const float t = lengthSquared > 0.f ? std::clamp((first.to - first.from).Dot(chord) / lengthSquared, 0.f, 1.f) : 0.f;
					const float deviation = (first.to - (first.from + chord * t)).Length();
					if (deviation < error) { merge = i; error = deviation; }
				}
				if (merge < size) {
					ordered[merge].to = ordered[merge + 1].to;
					ordered[merge].delta = static_cast<float>(std::min(static_cast<double>(ordered[merge].delta) + ordered[merge + 1].delta,
						static_cast<double>(std::numeric_limits<float>::max())));
					ordered[merge].lastSequence = ordered[merge + 1].lastSequence;
					std::move(ordered.begin() + merge + 2, ordered.begin() + size, ordered.begin() + merge + 1);
				} else {
					std::move(ordered.begin() + 1, ordered.begin() + size, ordered.begin());
				}
				--size;
			}
			count = size;
			std::copy_n(ordered.begin(), count, segments.begin());
		}

		void Merge(const FlightPath& other)
		{
			for (const auto& segment : other) { Add(segment); }
		}

	private:
		std::array<FlightSegment, capacity> segments{};
		std::size_t count = 0;
	};

	struct Particle
	{
		Vec3 position, velocity, offset, tangent;
		float age = 0.f, lifetime = 1.f, radius = 1.f;
		float stopAlpha = 1.f;
		float floatTime = 0.f, phase = 0.f;
		bool stopped = false;
		Kind kind = Kind::Trail;

		float Alpha() const
		{
			const float u = std::clamp(age / lifetime, 0.f, 1.f);
			if (stopped) { return stopAlpha * std::pow(1.f - u, 1.35f); }
			const float fade = kind == Kind::Gather ? std::clamp((lifetime - age) / 0.22f, 0.f, 1.f) : std::pow(1.f - u, 1.35f);
			return std::min(age / 0.035f, 1.f) * fade;
		}
		float Size() const { return radius * (0.35f + 0.65f * std::clamp(1.f - age / lifetime, 0.f, 1.f)); }
	};

	class Simulation
	{
	public:
		static constexpr std::size_t capacity = 8192;
		static constexpr float maxLifetime = 3.3f;
		explicit Simulation(std::uint32_t seed = 1) : randomState(seed ? seed : 1) { particles.reserve(1024); }

		const std::vector<Particle>& Particles() const { return particles; }
		bool Empty() const { return particles.empty(); }
		void SetStyle(TrailStyle value) { style = value; }
		TrailStyle Style() const { return style; }
		void Reset(std::uint32_t seed)
		{
			particles.clear();
			randomState = seed ? seed : 1;
			gatherCarry = trailCarry = 0.f;
		}

		void SetEnabledKinds(bool gather, bool rings, bool trails)
		{
			std::erase_if(particles, [&](const Particle& p) {
				return (p.kind == Kind::Gather && !gather) || (p.kind == Kind::Ring && !rings) || (p.kind == Kind::Trail && !trails);
			});
			if (!gather) { gatherCarry = 0.f; }
			if (!trails) { trailCarry = 0.f; }
		}

		void BeginGather(Vec3 tip, float density)
		{
			if (!tip.Finite() || !ValidDensity(density)) { return; }

			const auto count = static_cast<unsigned>(std::round(180.f * density));
			for (unsigned i = 0; i < count && particles.size() < capacity; ++i) {
				AddGather(tip, Range(0.04f, 0.35f));
			}
		}

		void Advance(float dt, Vec3 tip)
		{
			if (!std::isfinite(dt) || dt <= 0.f || !tip.Finite()) { return; }
			for (auto& p : particles) { Step(p, dt, tip); }
			std::erase_if(particles, [](const Particle& p) { return p.age >= p.lifetime; });
		}

		void Gather(float dt, Vec3 tip, float density, float charge)
		{
			if (!std::isfinite(dt) || dt <= 0.f || !tip.Finite() || !ValidDensity(density)) { return; }

			const float window = std::min(dt, 0.1f);
			if (!std::isfinite(charge)) { charge = 0.f; }
			const float rate = (650.f + 390.f * std::clamp(charge, 0.f, 1.f)) * density;
			const float total = gatherCarry + window * rate;
			const auto count = static_cast<unsigned>(total);
			for (unsigned i = 0; i < count && particles.size() < capacity; ++i) {
				AddGather(tip, std::max(0.f, window - (i + 1.f - gatherCarry) / rate));
			}
			gatherCarry = total - count;
		}

		void StopGather()
		{
			gatherCarry = 0.f;
			for (auto& p : particles) {
				if (p.kind == Kind::Gather) {

					p.stopAlpha = p.Alpha();
					p.radius = p.Size();
					p.lifetime = std::max(p.lifetime - p.age, 0.001f);
					p.age = 0.f;
					p.stopped = true;
					p.kind = Kind::Trail;
					p.velocity = {};
				}
			}
		}

		void Ring(Vec3 origin, Vec3 direction, float density, float power)
		{
			if (!origin.Finite() || !direction.Finite() || !ValidDensity(density)) { return; }
			const Vec3 forward = direction.Unit();
			const Vec3 right = forward.Cross(std::abs(forward.z) < 0.9f ? Vec3{ 0.f, 0.f, 1.f } : Vec3{ 1.f, 0.f, 0.f }).Unit();
			const Vec3 up = right.Cross(forward).Unit();
			const unsigned count = std::max(32u, static_cast<unsigned>(std::round(384.f * density)));
			if (!std::isfinite(power)) { power = 0.f; }
			const float strength = 0.65f + 0.35f * std::clamp(power, 0.f, 1.f);
			for (unsigned i = 0; i < count && particles.size() < capacity; ++i) {
				const float angle = 6.283185307f * (i + Range(-0.2f, 0.2f)) / count;
				const Vec3 radial = right * std::cos(angle) + up * std::sin(angle);
				Particle p;
				p.kind = Kind::Ring;
				p.position = origin + radial * Range(4.55f, 8.19f);

				p.velocity = (radial * Range(177.45f, 213.85f) + forward * Range(100.f, 140.f)) * strength;
				p.lifetime = Range(1.1f, 1.5f);
				p.radius = Range(1.1f, 1.95f);
				particles.push_back(p);
			}
		}

		unsigned Trail(Vec3 from, Vec3 to, float dt, float density, bool continuous = false,
			float elapsedAfter = 0.f, float minimumSpacing = 0.f, unsigned budget = 1024)
		{
			if (!from.Finite() || !to.Finite() || !std::isfinite(dt) || dt <= 0.f || !ValidDensity(density) ||
				!std::isfinite(elapsedAfter) || elapsedAfter < 0.f || !std::isfinite(minimumSpacing) || minimumSpacing < 0.f) { return 0; }
			const Vec3 delta = to - from;
			const float distance = delta.Length();
			if (!std::isfinite(distance) || (distance > 2048.f && !continuous)) { trailCarry = 0.f; return 0; }
			if (distance < 0.001f) { return 0; }
			const float spacing = std::max({ (style == TrailStyle::VanillaBow ? 3.f : style == TrailStyle::VanillaCrossbow ? 2.5f : 3.75f) / density,
				continuous ? distance / 1024.f : 0.f, minimumSpacing });
			const float carry = std::fmod(trailCarry, spacing);
			unsigned emitted = 0;
			for (float along = spacing - carry; along <= distance && emitted < budget && particles.size() < capacity; along += spacing, ++emitted) {
				const float t = along / distance;
				Particle p;
				if (style == TrailStyle::VanillaBow) {

					p.position = from + delta * t + Sphere() * Range(0.f, 0.25f);
					p.velocity = Sphere() * Range(0.3f, 1.1f) + Vec3{ 0.f, 0.f, 0.5f };
					p.lifetime = Range(2.3f, maxLifetime);
					p.radius = Range(0.48f, 1.f);
				} else if (style == TrailStyle::VanillaCrossbow) {

					p.position = from + delta * t + Sphere() * Range(0.f, 0.4f);
					p.velocity = Sphere() * Range(0.25f, 0.8f);
					p.lifetime = Range(1.1f, 1.7f);
					p.radius = Range(0.4f, 0.78f);
				} else {
					p.position = from + delta * t + Sphere() * Range(0.f, 1.5f);
					p.velocity = Sphere() * Range(1.5f, 6.f) + Vec3{ 0.f, 0.f, 2.f };
					p.lifetime = Range(2.3f, maxLifetime);
					p.radius = Range(0.65f, 1.25f);
				}
				Step(p, static_cast<float>(std::min(static_cast<double>(dt) * (1.f - t) + elapsedAfter,
					static_cast<double>(std::numeric_limits<float>::max()))), {});
				if (p.age < p.lifetime) { particles.push_back(p); }
			}
			trailCarry = std::fmod(carry + distance, spacing);
			return emitted;
		}

		void Trail(const FlightPath& path, float density)
		{
			double distance = 0., elapsedAfter = 0.;
			for (const auto& segment : path) {
				const float length = (segment.to - segment.from).Length();
				if (std::isfinite(length) && (length <= 2048.f || segment.continuous)) { distance += length; }
				elapsedAfter += segment.delta;
			}
			const float spacing = static_cast<float>(std::min(distance / 1024., static_cast<double>(std::numeric_limits<float>::max())));
			unsigned budget = 1024;
			for (const auto& segment : path) {
				elapsedAfter = std::max(elapsedAfter - segment.delta, 0.);
				budget -= Trail(segment.from, segment.to, segment.delta, density, segment.continuous,
					static_cast<float>(std::min(elapsedAfter, static_cast<double>(std::numeric_limits<float>::max()))), spacing, budget);
			}
		}

	private:
		std::vector<Particle> particles;
		std::uint32_t randomState;
		float gatherCarry = 0.f, trailCarry = 0.f;
		TrailStyle style = TrailStyle::Default;

		void AddGather(Vec3 tip, float age)
		{
			Particle p;
			p.kind = Kind::Gather;
			p.offset = Sphere() * Range(27.f, 65.f);
			p.tangent = p.offset.Unit().Cross(Sphere()).Unit() * Range(5.2f, 13.f);
			p.position = tip + p.offset;
			p.lifetime = Range(1.35f, 2.1f);
			p.floatTime = Range(0.35f, 0.65f);
			p.phase = Range(0.f, 6.283185307f);
			p.radius = Range(0.72f, 1.43f);
			Step(p, age, tip);
			particles.push_back(p);
		}

		static bool ValidDensity(float& density)
		{
			if (!std::isfinite(density) || density <= 0.f) { return false; }
			density = std::clamp(density, 0.05f, RedsandSettings::maxDensity);
			return true;
		}
		float Range(float low, float high)
		{
			randomState ^= randomState << 13;
			randomState ^= randomState >> 17;
			randomState ^= randomState << 5;
			return low + (high - low) * static_cast<float>(randomState >> 8) / 16777216.f;
		}
		Vec3 Sphere()
		{
			const float z = Range(-1.f, 1.f), angle = Range(0.f, 6.283185307f);
			const float r = std::sqrt(std::max(0.f, 1.f - z * z));
			return { r * std::cos(angle), r * std::sin(angle), z };
		}
		static void Step(Particle& p, float dt, Vec3 tip)
		{
			p.age += dt;
			if (p.kind == Kind::Gather) {

				const float u = std::clamp((p.age - p.floatTime) / (p.lifetime - p.floatTime - 0.18f), 0.f, 1.f);
				const float gather = u * u * (3.f - 2.f * u);
				const float remaining = 1.f - gather;
				const float wobble = std::sin(p.phase + p.age * 4.5f) - std::sin(p.phase);
				p.position = tip + p.offset * remaining + p.tangent * (wobble * remaining * 0.6f);
			} else {
				const float drag = p.kind == Kind::Ring ? 2.5f : 1.2f;
				const float damping = std::exp(-drag * dt);
				p.position = p.position + p.velocity * ((1.f - damping) / drag);
				p.velocity = p.velocity * damping;
			}
		}
	};
}
