#pragma once

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace RedsandSwingTrail
{
	struct Vec3
	{
		float x = 0.f, y = 0.f, z = 0.f;
		Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
		Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
		Vec3 operator*(float n) const { return {x*n,y*n,z*n}; }
		float Dot(Vec3 b) const { return x*b.x+y*b.y+z*b.z; }
		float Length() const { return std::sqrt(Dot(*this)); }
		bool Finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
	};

	inline bool IsBladePart(std::string_view name)
	{
		const auto lower = [](char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch; };
		for (const std::string_view marker : { "scb", "scab", "sheath", "blood" }) {
			if (std::search(name.begin(), name.end(), marker.begin(), marker.end(),
				[&](char a, char b) { return lower(a) == b; }) != name.end()) { return false; }
		}
		return true;
	}

	struct BladeTip
	{
		Vec3 point{};
		float reachSquared = 0.f;

		void Consider(Vec3 candidate)
		{
			if (!candidate.Finite()) { return; }
			const float distance = candidate.Dot(candidate);
			if (distance > reachSquared && distance >= 16.f && distance <= 250000.f) {
				point = candidate;
				reachSquared = distance;
			}
		}
	};

	struct Parameters
	{
		float ribbonLife = .2f, particleLife = .3f, width = .5f, momentum = .1f;
		float density = 1.f, size = 1.f, spread = 1.f;
		float ribbonFade = .25f, particleFade = .4f, gravity = .3f;
	};

	inline Parameters CompactStroke(Parameters value)
	{
		value.ribbonLife = std::min(value.ribbonLife, .105f);
		value.ribbonFade = std::min(value.ribbonFade, .215f);
		value.particleLife = std::min(value.particleLife, .11f);
		value.particleFade = std::min(value.particleFade, .28f);
		return value;
	}

	struct Particle
	{
		Vec3 position, velocity;
		float age = 0.f, lifetime = .1f, radius = 1.f, intensity = 1.f;
		bool ribbon = false;
		float fadeStart = 0.f, gravity = 0.f;
		std::uint32_t segment = 0;
		float distanceFraction = 0.f;
		float bladeFraction = 1.f;
		float FadeProgress() const { return std::clamp((age - fadeStart) / std::max(.001f, lifetime - fadeStart), 0.f, 1.f); }
		float Fade() const { const float u = FadeProgress(); return u * u * (3.f - 2.f * u); }
		float Alpha() const { return 1.f-Fade(); }
		float Size() const { return ribbon ? radius*(1.f-.45f*Fade()) : radius; }
	};
	struct Pose { Vec3 base, tip; };
	struct Tangent { Vec3 base, tip;float length=0.f; };
	struct Curve
	{
		Pose start, end;
		Tangent first, last;
		float duration;
		float startLength,endLength;
		Curve(Pose a,Pose b,Tangent va,Tangent vb,float dt) : start(a),end(b),first(va),last(vb),duration(dt),
			startLength((a.tip-a.base).Length()),endLength((b.tip-b.base).Length()) {}
		static Vec3 Hermite(Vec3 a,Vec3 b,Vec3 va,Vec3 vb,float t,float dt)
		{
			return a*(2*t*t*t-3*t*t+1)+va*((t*t*t-2*t*t+t)*dt)+b*(-2*t*t*t+3*t*t)+vb*((t*t*t-t*t)*dt);
		}
		Pose At(float t) const
		{
			const auto base=Hermite(start.base,end.base,first.base,last.base,t,duration);
			const auto tip=Hermite(start.tip,end.tip,first.tip,last.tip,t,duration);
			const auto axis=tip-base;const float rawLength=axis.Length();
			const float length=Hermite({startLength,0,0},{endLength,0,0},{first.length,0,0},{last.length,0,0},t,duration).x;
			return {base,base+axis*(std::max(.1f,length)/std::max(.0001f,rawLength))};
		}
		Vec3 Position(float t,float along) const { const auto p=At(t);return p.base+(p.tip-p.base)*along; }
		Vec3 Velocity(float t,float along) const
		{
			const float lo=std::max(0.f,t-.001f),hi=std::min(1.f,t+.001f);
			return (Position(hi,along)-Position(lo,along))*(1.f/((hi-lo)*duration));
		}
		struct Evaluation
		{
			Pose at,before,after;
			float inverseTime;
			static Vec3 Along(Pose p,float along) { return p.base+(p.tip-p.base)*along; }
			Vec3 Position(float along) const { return Along(at,along); }
			Vec3 Velocity(float along) const { return (Along(after,along)-Along(before,along))*inverseTime; }
		};
		Evaluation Evaluate(float t) const
		{
			const float lo=std::max(0.f,t-.001f),hi=std::min(1.f,t+.001f);
			return {At(t),At(lo),At(hi),1.f/((hi-lo)*duration)};
		}
	};
	class Lease
	{
	public:
		static constexpr unsigned maximum = 32;
		Lease() = default;
		Lease(const Lease&) = delete;
		Lease& operator=(const Lease&) = delete;
		~Lease() { if (held) { count.fetch_sub(1); } }
		bool Acquire()
		{
			if (held) { return true; }
			auto current = count.load();
			while (current < maximum) { if (count.compare_exchange_weak(current,current+1)) { held = true; return true; } }
			return false;
		}
	private:
		bool held = false;
		inline static std::atomic<unsigned> count{0};
	};
	class Simulation
	{
	public:
		static constexpr std::size_t capacity = 4096;
		static constexpr unsigned maxSamplesPerFrame = 384;
		const std::vector<Particle>& Particles() const { return particles; }
		bool Empty() const { return particles.empty(); }
		void DiscardParticles()
		{
			particles.clear();
			needsFirstSpark = true;
			remainder = 0.f;
		}
		void SetRenderCapacity(std::size_t count) { renderCapacity = std::min(count, capacity); }
		void Break()
		{
			previousCurve.reset();
			sampled = false;
			needsFirstSpark = true;
			remainder = idle = 0.f;
		}
		bool EndAttack()
		{
			if (ended) { return false; }
			ended = true;
			Break();
			return true;
		}
		void Advance(float delta)
		{
			if (!std::isfinite(delta) || delta <= 0.f) { return; }
			const auto arc=Motion::For(delta,9.f),grain=Motion::For(delta,3.f);
			for (auto& p : particles) { Move(p,delta,p.ribbon ? arc : grain); }
			std::erase_if(particles,[](const auto& p) { return p.age >= p.lifetime; });
		}
		void AdvanceFrame(float delta)
		{
			Advance(std::min(delta, .04f));
		}
		void Seed(Pose pose)
		{
			const float length = (pose.tip - pose.base).Length();
			if (pose.base.Finite() && pose.tip.Finite() && length >= .1f && length <= 500.f) {
				current = pose;
				sampled = true;
			}
		}
		void Sample(Vec3 base, Vec3 tip, float delta, const Parameters& parameters)
		{
			if (ended) { return; }
			if (!base.Finite() || !tip.Finite() || !std::isfinite(delta) || delta <= 0.f) { Break(); return; }
			const Pose next{base,tip};const float reach=(tip-base).Length();
			if (reach<.1f || reach>500.f) { Break();return; }
			if (delta > .1f) { Break(); }
			if (!sampled) { Seed(next); return; }
			const auto oldAxis=current.tip-current.base,newAxis=tip-base;
			const float distance=(tip-current.tip).Length();
			if ((base-current.base).Length()>120.f || oldAxis.Dot(newAxis)<-.9999f*oldAxis.Length()*reach) {
				Break();
				Seed(next);
				return;
			}
			if (distance <= .001f) {
				idle += delta;
				if (idle >= .04f) {
					Break();
					Seed(next);
				}
				return;
			}
			const float elapsed=delta+idle;idle=0.f;
			auto [first, last] = ArcTangents(current, next, elapsed);
			if (previousCurve) {
				const auto incoming = previousCurve->last;
				first = { JoinVelocity(incoming.base, first.base), JoinVelocity(incoming.tip, first.tip),
					JoinRate(incoming.length, first.length) };
				const auto original = *previousCurve;
				previousCurve->last = first;
				RefineSegment(original, *previousCurve);
			}
			previousCurve.emplace(current, next, first, last, elapsed);
			++segment;
			Emit(*previousCurve, parameters);
			current = next;
		}
	private:
		static float JoinRate(float incoming, float outgoing)
		{
			if (incoming * outgoing <= 0.f) { return 0.f; }
			return 2.f * incoming * outgoing / (incoming + outgoing);
		}

		static Vec3 JoinVelocity(Vec3 incoming, Vec3 outgoing)
		{
			const float before = incoming.Length();
			const float after = outgoing.Length();
			if (before < .001f || after < .001f) { return {}; }
			const auto direction = incoming * (1.f / before) + outgoing * (1.f / after);
			const float length = direction.Length();
			if (length < .2f) { return {}; }
			return direction * (JoinRate(before, after) / length);
		}

		static std::pair<Tangent, Tangent> ArcTangents(Pose start, Pose end, float delta)
		{
			const auto a = start.tip - start.base, b = end.tip - end.base;
			const float lengthA = a.Length(), lengthB = b.Length();
			const auto dirA = a * (1.f / lengthA), dirB = b * (1.f / lengthB);
			const float cosine = std::clamp(dirA.Dot(dirB), -1.f, 1.f);
			const float angle = std::acos(cosine);
			const float angularRate = (angle > .001f ? angle / std::sin(angle) : 1.f) / delta;
			const auto baseVelocity = (end.base - start.base) * (1.f / delta);
			const float lengthRate = (lengthB - lengthA) / delta;
			return {
				{baseVelocity, baseVelocity + dirA * lengthRate + (dirB - dirA * cosine) * (angularRate * lengthA), lengthRate},
				{baseVelocity, baseVelocity + dirB * lengthRate + (dirB * cosine - dirA) * (angularRate * lengthB), lengthRate}
			};
		}
		struct ArcDistances
		{
			static constexpr unsigned subdivisions = 24;
			std::array<float, subdivisions + 1> lengths{};
			unsigned interval = 1;
			explicit ArcDistances(const Curve& curve)
			{
				auto prior = curve.start.tip;
				for (unsigned i = 1; i <= subdivisions; ++i) {
					const auto point = curve.Position(static_cast<float>(i) / subdivisions, 1.f);
					lengths[i] = lengths[i - 1] + (point - prior).Length();
					prior = point;
				}
			}

			float TimeAt(float distanceFraction)
			{
				if (distanceFraction >= 1.f) { return 1.f; }
				const float target = lengths.back() * distanceFraction;
				while (interval < subdivisions && lengths[interval] < target) { ++interval; }
				const float span = lengths[interval] - lengths[interval - 1];
				const float fraction = span > .0001f ? std::clamp((target - lengths[interval - 1]) / span, 0.f, 1.f) : 1.f;
				return (interval - 1.f + fraction) / subdivisions;
			}
		};

		void RefineSegment(const Curve& original, const Curve& refined)
		{
			auto first = std::partition_point(particles.begin(), particles.end(), [&](const auto& p) { return p.segment != segment; });
			if (first == particles.end()) { return; }
			ArcDistances before(original);
			ArcDistances after(refined);
			Pose oldPose, newPose;
			float lastFraction = -1.f;
			for (auto it = first; it != particles.end(); ++it) {
				if (it->distanceFraction != lastFraction) {
					lastFraction = it->distanceFraction;
					oldPose = original.At(before.TimeAt(lastFraction));
					newPose = refined.At(after.TimeAt(lastFraction));
				}
				const auto oldOrigin = Curve::Evaluation::Along(oldPose, it->bladeFraction);
				const auto newOrigin = Curve::Evaluation::Along(newPose, it->bladeFraction);
				it->position = it->ribbon ? newOrigin : it->position + (newOrigin - oldOrigin);
			}
		}

		void Emit(const Curve& curve, const Parameters& parameters)
		{
			if (particles.size() >= renderCapacity) { return; }
			if (!particles.capacity()) { particles.reserve(1024); }
			ArcDistances distances(curve);
			const float distance = distances.lengths.back();
			const float spacing = std::clamp(parameters.width * .65f, .1f, .6f);
			const unsigned available = static_cast<unsigned>(renderCapacity - particles.size());
			const float arcLife = parameters.ribbonLife + parameters.ribbonFade;
			const float maxGrainHold = std::max(parameters.ribbonLife + .02f, parameters.particleLife * 1.15f);
			const float retention = parameters.density > 0.f ? std::max(arcLife + .04f, maxGrainHold + parameters.particleFade) : arcLife;
			const float frameShare = std::clamp(curve.duration * .90f / std::max(.001f, retention), 0.f, 1.f);
			const unsigned frameBudget = std::min(available, std::max(2u, static_cast<unsigned>(renderCapacity * frameShare)));
			const float grainRatio = .6f * parameters.density * spacing;
			const unsigned grainReserve = parameters.density > 0.f && frameBudget > 1 ?
				std::clamp(static_cast<unsigned>(frameBudget * grainRatio / (1.f + grainRatio)), 1u, frameBudget / 2) : 0u;
			const unsigned samples = std::min({frameBudget - grainReserve, maxSamplesPerFrame,
				std::max(1u, static_cast<unsigned>(std::ceil(distance / spacing)))});
			unsigned grainSlots = frameBudget - samples;
			for (unsigned i=0;i<samples;++i) {
				const float fraction = (i + 1.f) / samples;
				const float t = distances.TimeAt(fraction);
				const float birthAge = curve.duration * (1.f - t);
				if (birthAge >= parameters.ribbonLife + parameters.ribbonFade) { continue; }
				Particle arc{curve.Position(t, 1.f), {}, 0.f, arcLife,
					1.15f, .85f, true, parameters.ribbonLife, 0.f, segment, fraction};
				Move(arc, birthAge, {});
				Add(arc);
				remainder += distance/samples*.6f*parameters.density;
				const unsigned budget = static_cast<unsigned>(remainder);
				remainder -= budget;
				const unsigned grains = std::min({grainSlots, 12u, grainReserve && needsFirstSpark ? std::max(1u, budget) : budget});
				if (!grains) { continue; }
				grainSlots -= grains;
				const auto evaluated = curve.Evaluate(t);
				const auto velocityAt=[&](float along) {
					Vec3 velocity=evaluated.Velocity(along)*parameters.momentum;
					const float speed=velocity.Length();return speed>80.f ? velocity*(80.f/speed) : velocity;
				};
				const auto grainMotion = Motion::For(birthAge, 3.f);
				for (unsigned grain=0; grain<grains; ++grain) {
					const float along = needsFirstSpark ? 1.f : .94f + Random() * .06f;
					const float jitter=3.f*parameters.spread;
					const Vec3 offset{(Random()-.5f)*jitter,(Random()-.5f)*jitter,(Random()-.5f)*jitter};
					const auto velocity=velocityAt(along)+offset*2.f;
					const float retained=std::max(parameters.ribbonLife+.02f,parameters.particleLife*(.85f+Random()*.3f));
					const float fade=std::max(parameters.particleFade,parameters.ribbonLife+parameters.ribbonFade+.04f-retained);
					Particle p{evaluated.Position(along)+offset,velocity,0.f,retained+fade,
						(.45f+Random()*.3f)*parameters.size,.8f+Random()*.2f,false,retained,-240.f*parameters.gravity,segment,fraction,along};
					Move(p,birthAge,grainMotion); Add(p);
					needsFirstSpark = false;
				}
			}
		}
		std::vector<Particle> particles;
		std::size_t renderCapacity = capacity;
		Pose current;
		std::optional<Curve> previousCurve;
		std::uint32_t segment = 0;
		bool sampled = false, ended = false, needsFirstSpark = true;
		float remainder = 0.f,idle=0.f;
		std::uint32_t random = 0x912e5c3bu;
		float Random() { random^=random<<13;random^=random>>17;random^=random<<5;return (random&0xffffff)/16777216.f; }
		struct Motion
		{
			float decay=1.f,travel=0.f,fall=0.f;
			static Motion For(float delta,float drag) { const float decay=std::exp(-drag*delta),travel=(1.f-decay)/drag;return {decay,travel,(delta-travel)/drag}; }
		};
		static void Move(Particle& p,float delta,Motion motion)
		{
			p.position=p.position+p.velocity*motion.travel;p.velocity=p.velocity*motion.decay;
			p.position.z+=p.gravity*motion.fall;p.velocity.z+=p.gravity*motion.travel;
			p.age += delta;
		}
		void Add(const Particle& p) { if (p.age<p.lifetime && particles.size()<renderCapacity) { particles.push_back(p); } }
	};
}
