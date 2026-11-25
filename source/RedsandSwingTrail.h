#pragma once

#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numeric>
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
		Vec3 Cross(Vec3 b) const { return {y*b.z-z*b.y, z*b.x-x*b.z, x*b.y-y*b.x}; }
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
		float ribbonLife = .2f, particleLife = .3f, width = .5f, momentum = 1.f;
		float density = 1.f, size = 1.f;
		float ribbonFade = .25f, particleFade = .4f;
		float particleWidth = 0.f;
	};

	struct Particle
	{
		Vec3 position, velocity;
		float age = 0.f, lifetime = .1f, radius = 1.f, intensity = 1.f;
		bool ribbon = false;
		float fadeStart = 0.f;
		float distanceFraction = 0.f;
		std::uint32_t stroke = 0;
		float strokeDistance = 0.f;
		Vec3 swingDirection, emissionCenter;
		float sideOffset = 0.f;
		Vec3 settlingOffset, flutterOffset;
		float FadeProgress() const { return std::clamp((age - fadeStart) / std::max(.001f, lifetime - fadeStart), 0.f, 1.f); }
		float Fade() const { const float u = FadeProgress(); return u * u * (3.f - 2.f * u); }
		float Alpha() const { return 1.f-Fade(); }
		float Size() const { return ribbon ? radius*(1.f-.45f*Fade()) : radius; }
	};
	struct Pose { Vec3 base, tip, actor{}; };
	struct Curve
	{
		Pose start, end;
		float duration;
		float startLength, endLength;

		Curve(Pose a, Pose b, float delta) : start(a), end(b), duration(delta),
			startLength((a.tip - a.base).Length()), endLength((b.tip - b.base).Length())
		{
			startAxis = (start.tip - start.base) * (1.f / startLength);
			endBlade = end.tip - end.base;
			const auto endAxis = endBlade * (1.f / endLength);
			const float cosine = std::clamp(startAxis.Dot(endAxis), -1.f, 1.f);
			const auto turn = endAxis - startAxis * cosine;
			const float sine = turn.Length();
			if (sine <= .0001f) { arcWeight = 0.f; return; }
			turnAxis = turn * (1.f / sine);
			angle = std::atan2(sine, cosine);

			// Hilt translation can cancel the rotation at the tip. Constrain the
			// reconstructed path in that case, even though both poses are valid.
			const auto chord = end.tip - start.tip;
			const float length = chord.Length();
			if (length <= .001f) { arcWeight = 0.f; return; }
			const auto direction = chord * (1.f / length);
			constexpr unsigned checks = 24;
			float previous = 0.f;
			for (unsigned i = 1; i <= checks; ++i) {
				const float t = static_cast<float>(i) / checks;
				const auto offset = i == checks ? chord : chord * t + TipDeviation(t);
				const float progress = offset.Dot(direction);
				const float step = (progress - previous) / (length / checks);
				if (step < .04f) { arcWeight = std::min(arcWeight, .96f / (1.f - step)); }
				const float lateral = (offset - direction * progress).Length();
				if (lateral > .45f * length) { arcWeight = std::min(arcWeight, .45f * length / lateral); }
				previous = progress;
			}
		}

		Pose At(float t) const
		{
			if (t <= 0.f) { return start; }
			if (t >= 1.f) { return end; }
			const auto base = start.base + (end.base - start.base) * t;
			const auto linearTip = start.tip + (end.tip - start.tip) * t;
			const auto tip = arcWeight > 0.f ? linearTip + TipDeviation(t) * arcWeight : linearTip;
			return {base, tip, start.actor + (end.actor - start.actor) * t};
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

	private:
		Vec3 TipDeviation(float t) const
		{
			const auto axis = startAxis * std::cos(angle * t) + turnAxis * std::sin(angle * t);
			const auto linearBlade = (start.tip - start.base) * (1.f - t) + endBlade * t;
			return axis * std::lerp(startLength, endLength, t) - linearBlade;
		}

		Vec3 startAxis, turnAxis, endBlade;
		float angle = 0.f, arcWeight = 1.f;
	};
	class Lease
	{
	public:
		static constexpr unsigned maximum = 32;
		Lease() = default;
		Lease(const Lease&) = delete;
		Lease& operator=(const Lease&) = delete;
		~Lease() { if (slot < maximum) { occupied.fetch_and(~(1u << slot)); } }
		bool Acquire()
		{
			if (slot < maximum) { return true; }
			auto mask = occupied.load();
			while (~mask) {
				const auto available = static_cast<unsigned>(std::countr_zero(~mask));
				if (occupied.compare_exchange_weak(mask, mask | (1u << available))) { slot = available; return true; }
			}
			return false;
		}
		unsigned Index() const { return slot; }
	private:
		unsigned slot = maximum;
		inline static std::atomic<std::uint32_t> occupied{0};
	};
	struct RibbonRow { Vec3 left, center, right; float alpha, coreStrength = 1.f; Vec3 side; };
	struct RibbonEnd { float distance; Vec3 position; };
	struct RibbonEnds
	{
		std::uint32_t stroke;
		RibbonEnd tail, head;
		unsigned rounded = 0, detours = 0;
		std::size_t firstRow = 0, rowCount = 0;
		float meshBegin = 0.f, meshEnd = 0.f;
		Vec3 exitDirection, exitSide;
	};
	struct RibbonTracePoint { float distance; Vec3 position; float born = 0.f, hold = 0.f, lifetime = 0.f, speed = 0.f; };

	inline Vec3 RibbonTracePosition(const std::vector<RibbonTracePoint>& trace, float distance)
	{
		const auto next = std::lower_bound(trace.begin(), trace.end(), distance,
			[](const auto& point, float at) { return point.distance < at; });
		if (next == trace.begin()) { return next->position; }
		if (next == trace.end()) { return trace.back().position; }
		const auto& prior = *(next - 1);
		const float t = (distance - prior.distance) / (next->distance - prior.distance);
		return prior.position + (next->position - prior.position) * t;
	}

	inline RibbonEnds RibbonTraceEnds(std::uint32_t stroke, const std::vector<RibbonTracePoint>& trace)
	{
		const auto& tail = trace.front();
		const auto& head = trace.back();
		RibbonEnds ends{stroke, {tail.distance, tail.position}, {head.distance, head.position}};
		const auto offset = head.position - RibbonTracePosition(trace, std::max(tail.distance, head.distance - 6.f));
		ends.exitDirection = offset * (1.f / std::max(.001f, offset.Length()));
		return ends;
	}

	class RibbonSpline
	{
	public:
		struct Point { Vec3 position, tangent; };

		void Set(const std::vector<RibbonTracePoint>& trace, float begin, float finish, float spacing)
		{
			knots.clear();
			const auto add = [&](float distance) { knots.push_back({distance, RibbonTracePosition(trace, distance)}); };
			add(begin);
			for (float distance = (std::floor(begin / spacing) + 1.f) * spacing; distance < finish - .001f; distance += spacing) {
				if (distance > begin + .001f) { add(distance); }
			}
			add(finish);
			detours = BypassDetours(spacing);
			detours += BypassSidewaysKinks(spacing);
			SmoothKnots(spacing);
			FitSpans();
			filterRadius = std::min(spacing, .20f * (finish - begin));
			tail = RoundEnd(begin, 1.f, finish - begin, spacing);
			head = RoundEnd(finish, -1.f, finish - begin, spacing);
		}

		Point At(float distance) const
		{
			if (tail && distance < tail->join) { return tail->At(distance); }
			if (head && distance > head->join) { return head->At(distance); }
			return FilteredAt(distance);
		}

		unsigned RoundedEnds() const { return static_cast<unsigned>(tail.has_value()) + static_cast<unsigned>(head.has_value()); }
		unsigned BypassedDetours() const { return detours; }

	private:
		unsigned BypassDetours(float spacing)
		{
			unsigned count = 0;
			for (std::size_t start = 0; start + 3 < knots.size(); ++start) {
				const auto& a = knots[start];
				std::size_t best = start;
				float saved = 0.f;
				for (std::size_t finish = start + 3; finish < knots.size(); ++finish) {
					const auto& b = knots[finish];
					const float travel = b.distance - a.distance;
					if (travel > 7.f * spacing) { break; }
					const auto chord = b.position - a.position;
					const float length = chord.Length();
					if (length < .45f * spacing || length > .55f * travel || travel - length <= saved) { continue; }
					const auto direction = chord * (1.f / length);
					float prior = 0.f, backwards = 0.f;
					bool local = true;
					for (std::size_t i = start + 1; i < finish; ++i) {
						const auto offset = knots[i].position - a.position;
						const float progress = offset.Dot(direction);
						if ((offset - direction * std::clamp(progress, 0.f, length)).Length() > 1.7f * spacing) {
							local = false;
							break;
						}
						backwards += std::max(0.f, prior - progress);
						prior = progress;
					}
					if (local && backwards + std::max(0.f, prior - length) >= .45f * spacing) {
						best = finish;
						saved = travel - length;
					}
				}
				if (best != start) {
					// Preserve both endpoints and the original distance used for aging and particle placement.
					knots.erase(knots.begin() + start + 1, knots.begin() + best);
					++count;
				}
			}
			return count;
		}

		unsigned BypassSidewaysKinks(float spacing)
		{
			unsigned count = 0;
			for (std::size_t i = 2; i + 2 < knots.size();) {
				const auto& a = knots[i - 1];
				const auto& b = knots[i];
				const auto approach = a.position - knots[i - 2].position;
				bool removed = false;
				for (std::size_t skip : {1u, 2u}) {
					const std::size_t end = i + skip;
					if (end + 1 >= knots.size()) { break; }
					const auto& c = knots[end];
					const auto chord = c.position - a.position;
					const float length = chord.Length();
					const float span = skip == 1 ? 3.f : 3.05f;
					const float minimumChord = skip == 1 ? .5f : 1.f;
					if (c.distance - a.distance > span * spacing || length < minimumChord * spacing) { continue; }
					const auto direction = chord * (1.f / length);
					const auto incoming = b.position - a.position;
					const auto outgoing = c.position - knots[end - 1].position;
					const auto departure = knots[end + 1].position - c.position;
					const float agreement = skip == 1 ? .85f : .94f;
					const float turn = skip == 1 ? .65f : .88f;
					if (approach.Dot(direction) <= agreement * approach.Length() ||
						departure.Dot(direction) <= agreement * departure.Length() ||
						incoming.Dot(outgoing) >= turn * incoming.Length() * outgoing.Length()) { continue; }
					float prior = 0.f, lateral = 0.f;
					bool forward = true;
					for (auto mid = i; mid < end; ++mid) {
						const auto offset = knots[mid].position - a.position;
						const float progress = offset.Dot(direction);
						forward &= progress > prior && progress < length;
						prior = progress;
						lateral = std::max(lateral, (offset - direction * progress).Length());
					}
					// Preserve real turns and remove only a short excursion between aligned segments.
					if (forward && lateral >= .2f * spacing && lateral <= .65f * spacing) {
						knots.erase(knots.begin() + i, knots.begin() + end);
						++count;
						removed = true;
						break;
					}
				}
				if (!removed) { ++i; }
			}
			return count;
		}

		void SmoothKnots(float spacing)
		{
			if (knots.size() < 3) { return; }
			const auto measured = knots;
			for (std::size_t i = 1; i + 1 < knots.size(); ++i) {
				const auto& a = measured[i - 1];
				const auto& b = measured[i];
				const auto& c = measured[i + 1];
				const float before = (b.position - a.position).Length();
				const float after = (c.position - b.position).Length();
				if (before + after <= .001f) { continue; }
				const auto offset = a.position + (c.position - a.position) * (before / (before + after)) - b.position;
				knots[i].position = b.position + offset * std::min(.30f, .18f * spacing / std::max(.001f, offset.Length()));
			}
		}

		struct EndCurve
		{
			float end, join;
			Vec3 endpoint, control, shoulder;

			Point At(float distance) const
			{
				const float t = (distance - end) / (join - end);
				const auto a = control - endpoint;
				const auto b = shoulder - control;
				const auto tangent = a * (1.f - t) + b * t;
				return {endpoint + (a * (2.f - t) + b * t) * t, tangent * (join > end ? 1.f : -1.f)};
			}
		};

		std::optional<EndCurve> RoundEnd(float end, float sign, float span, float spacing) const
		{
			if (span < spacing) { return std::nullopt; }
			const auto endpoint = FilteredAt(end);
			for (float scale : {1.6f, 2.f, 2.5f}) {
				const float extent = std::min(spacing * scale, span * .28f);
				const float join = end + sign * extent;
				const auto shoulder = FilteredAt(join);
				const auto chord = shoulder.position - endpoint.position;
				const float length = chord.Length();
				const float tangentLength = shoulder.tangent.Length();
				if (length < .35f * extent || tangentLength <= .001f) { continue; }
				const auto axis = chord * (1.f / length);
				const auto tangent = shoulder.tangent * (sign / tangentLength);
				const float alignment = axis.Dot(tangent);
				if (alignment < .55f) { continue; }
				const auto expected = axis * (2.f * alignment) - tangent;
				if (expected.Dot(endpoint.tangent * sign) > .91f * endpoint.tangent.Length() && length > .86f * extent) {
					return std::nullopt;
				}
				// An end correction uses the measured tip and enters the middle spline along its existing tangent.
				const EndCurve candidate{end, join, endpoint.position, shoulder.position - tangent * (.5f * length / alignment), shoulder.position};
				const float maxOffset = std::min(5.f, spacing * .75f);
				bool local = true;
				for (float t : {.25f, .5f, .75f}) {
					const float at = std::lerp(end, join, t);
					local &= (candidate.At(at).position - FilteredAt(at).position).Length() <= maxOffset;
				}
				if (local) { return candidate; }
			}
			return std::nullopt;
		}

		Point FilteredAt(float distance) const
		{
			const auto center = InteriorAt(distance);
			const float fromStart = distance - knots.front().distance;
			const float fromEnd = knots.back().distance - distance;
			const float nearest = std::min(fromStart, fromEnd);
			if (nearest <= .001f || filterRadius <= .001f) { return center; }
			// Narrow the averaging window smoothly at the measured tips.
			const float ramp = std::min(1.f, nearest / (2.f * filterRadius));
			const float radius = filterRadius * ramp * (2.f - ramp);
			const float radiusSlope = (fromStart < fromEnd ? 1.f : -1.f) * (1.f - ramp);
			const auto left = InteriorAt(distance - radius);
			const auto right = InteriorAt(distance + radius);
			constexpr float sideWeight = .30f, maxShift = .15f;
			const auto offset = ((left.position - center.position) + (right.position - center.position)) * sideWeight;
			const auto offsetSlope = (left.tangent * (1.f - radiusSlope) + right.tangent * (1.f + radiusSlope) -
				center.tangent * 2.f) * sideWeight;
			const float scale = 1.f / std::sqrt(1.f + offset.Dot(offset) / (maxShift * maxShift));
			const float scaleSlope = -scale * scale * scale * offset.Dot(offsetSlope) / (maxShift * maxShift);
			return {
				center.position + offset * scale,
				center.tangent + offsetSlope * scale + offset * scaleSlope
			};
		}

		Point InteriorAt(float distance) const
		{
			const auto span = std::upper_bound(spans.begin(), spans.end() - 1, distance,
				[](float at, const auto& span) { return at < span.end; });
			const float inverseLength = 1.f / (span->end - span->begin);
			const float t = std::clamp((distance - span->begin) * inverseLength, 0.f, 1.f);
			const auto a = span->control - span->start;
			const auto b = span->finish - span->control;
			return {distance < span->end ? span->start + (a * (2.f - t) + b * t) * t : span->finish,
				(a * (1.f - t) + b * t) * (2.f * inverseLength)};
		}

		void FitSpans()
		{
			spans.resize(knots.size());
			float begin = knots.front().distance;
			for (std::size_t i = 0; i < spans.size(); ++i) {
				const float end = i + 1 == spans.size() ? knots.back().distance :
					std::midpoint(knots[i].distance, knots[i + 1].distance);
				spans[i] = {begin, end, {}, knots[i].position, {}};
				begin = end;
			}
			spans.front().start = knots.front().position;
			spans.back().finish = knots.back().position;
			for (std::size_t i = 1; i < spans.size(); ++i) {
				auto& left = spans[i - 1];
				auto& right = spans[i];
				const float leftLength = (left.end - left.begin) * (i == 1 ? 2.f : 1.f);
				const float rightLength = (right.end - right.begin) * (i + 1 == spans.size() ? 2.f : 1.f);
				// Match the derivative across uneven segments and the linear tips.
				const float blend = leftLength / (leftLength + rightLength);
				left.finish = right.start = left.control + (right.control - left.control) * blend;
			}
			spans.front().control = (spans.front().start + spans.front().finish) * .5f;
			spans.back().control = (spans.back().start + spans.back().finish) * .5f;
		}

		struct SplineSpan { float begin, end; Vec3 start, control, finish; };
		std::vector<RibbonTracePoint> knots;
		std::vector<SplineSpan> spans;
		std::optional<EndCurve> tail, head;
		float filterRadius = 0.f;
		unsigned detours = 0;
	};

	struct ScatterPlacement { Vec3 position; float opacity; };

	struct PointLighting { float materialBrightness, grainOpacity, fallbackOpacity; };

	inline PointLighting ResolvePointLighting(float particleBrightness, float arcBrightness, bool drawFallback)
	{
		const float grains = particleBrightness * std::sqrt(std::min(1.f, particleBrightness / 10.f));
		const float fallback = drawFallback ? arcBrightness : 0.f;
		const float material = std::max({1.f, grains, fallback});
		return {material, grains / material, fallback / material};
	}

	inline float RibbonEnvelope(float progress)
	{
		return 4.f * progress * (1.f - progress);
	}

	template <class Submit>
	unsigned SampleFallbackRibbon(const std::vector<RibbonRow>& rows, unsigned capacity, Submit submit)
	{
		if (!capacity || rows.size() < 2) { return 0; }
		const auto radiusAt = [](const RibbonRow& row) { return (row.right - row.center).Length(); };
		float bodyRadius = 0.f;
		for (const auto& row : rows) { bodyRadius = std::max(bodyRadius, radiusAt(row)); }
		if (bodyRadius <= .001f) { return 0; }
		const auto sampleWeight = [&](float first, float second) {
			return 1.f + 2.f * (1.f - .5f * (first + second) / bodyRadius);
		};
		float length = 0.f;
		for (std::size_t i = 1; i < rows.size(); ++i) {
			const auto& a = rows[i - 1];
			const auto& b = rows[i];
			if (a.alpha > 0.f || b.alpha > 0.f) { length += (b.center - a.center).Length() * sampleWeight(radiusAt(a), radiusAt(b)); }
		}
		if (length <= .001f) { return 0; }
		const float spacing = std::max(.14f, length / capacity);
		float next = std::min(.5f * spacing, .5f * length);
		unsigned count = 0;
		for (std::size_t i = 1; i < rows.size() && count < capacity; ++i) {
			const auto& a = rows[i - 1];
			const auto& b = rows[i];
			if (a.alpha <= 0.f && b.alpha <= 0.f) { continue; }
			const auto offset = b.center - a.center;
			const float aRadius = radiusAt(a), bRadius = radiusAt(b);
			const float weight = sampleWeight(aRadius, bRadius);
			const float span = offset.Length() * weight;
			if (span <= .001f) { continue; }
			for (; next <= span && count < capacity; next += spacing) {
				const float t = next / span;
				const float radius = std::lerp(aRadius, bRadius, t);
				const float alpha = std::lerp(a.alpha, b.alpha, t);
				if (radius <= .001f || alpha <= .001f) { continue; }
				submit(a.center + offset * t, radius, alpha * std::min(1.f, spacing / (weight * radius)));
				++count;
			}
			next -= span;
		}
		return count;
	}
	class Simulation
	{
	public:
		static constexpr std::size_t capacity = 4096;
		static constexpr unsigned maxSamplesPerFrame = 384;
		static constexpr unsigned maxRibbonRows = 512;
		const std::vector<Particle>& Particles() const { return particles; }
		bool Empty() const
		{
			return particles.empty() && std::none_of(strokeTraces.begin(), strokeTraces.end(), [&](const auto& trace) {
				return std::ranges::any_of(trace.points, [&](const auto& point) { return LinePointAlive(point); });
			});
		}
		void DiscardParticles()
		{
			particles.clear();
			strokeTraces.clear();
			strokeDistance = 0.f;
			needsFirstSpark = true;
			remainder = .5f;
		}
		void SetRenderCapacity(std::size_t count) { renderCapacity = std::min(count, capacity); }
		void Break()
		{
			if (!strokeTraces.empty() && strokeTraces.back().stroke == stroke) {
				strokeTraces.back().releasedAt = lineTime;
			}
			sampled = false;
			needsFirstSpark = true;
			remainder = .5f;
			idle = 0.f;
			strokeDistance = 0.f;
			++stroke;
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
			lineTime += delta;
			const auto motion = Motion::For(delta);
			for (auto& p : particles) { Move(p,delta,motion); }
			std::erase_if(particles,[](const auto& p) { return p.age >= p.lifetime; });
			const auto firstLiveStroke = particles.empty() ? stroke : particles.front().stroke;
			std::erase_if(strokeTraces, [&](const auto& entry) {
				return entry.stroke < firstLiveStroke && std::ranges::none_of(entry.points, [&](const auto& point) { return LinePointAlive(point); });
			});
		}
		void AdvanceFrame(float delta)
		{
			Advance(std::min(delta, .04f));
		}
		void BuildRibbon(Vec3 camera, float width, std::vector<RibbonRow>& rows, std::vector<RibbonEnds>& ends)
		{
			viewpoint = camera;
			rows.clear();
			ends.clear();
			struct DrawSpan { std::size_t trace; const RibbonTracePoint* first; const RibbonTracePoint* last; unsigned desired; };
			std::vector<DrawSpan> spans;
			unsigned desiredRows = 0;
			const auto alive = [&](const auto& point) { return LinePointAlive(point); };
			for (const auto& trace : strokeTraces) {
				ends.push_back(RibbonTraceEnds(trace.stroke, trace.points));
				if (trace.exitDirection.Dot(trace.exitDirection) > .5f) {
					ends.back().exitDirection = trace.exitDirection;
					ends.back().exitSide = trace.exitSide;
				}
				const auto first = std::ranges::find_if(trace.points, alive);
				if (first == trace.points.end()) { continue; }
				const auto last = std::find_if(trace.points.rbegin(), trace.points.rend(), alive);
				const float length = last->distance - first->distance;
				if (length <= .001f) { continue; }
				const unsigned desired = std::clamp(static_cast<unsigned>(std::ceil(length / 1.5f)) + 1, 3u, maxRibbonRows);
				spans.push_back({ends.size() - 1, &*first, &*last, desired});
				desiredRows += desired;
			}
			RibbonSpline spline;
			for (const auto& span : spans) {
				auto& trace = strokeTraces[span.trace];
				const float begin = span.first->distance;
				const float finish = span.last->distance;
				const float length = finish - begin;
				const unsigned count = std::max(3u, span.desired * maxRibbonRows / std::max(desiredRows, maxRibbonRows));
				if (rows.size() + count > maxRibbonRows) { continue; }
				const auto& lastPoint = trace.points.back();
				const float release = trace.releasedAt ?
					(lineTime - *trace.releasedAt) / std::max(.045f, .8f * lastPoint.lifetime) : 0.f;
				const float strokeLength = lastPoint.distance - trace.points.front().distance;
				const auto appearance = [&](const RibbonTracePoint& point) {
					const float age = lineTime - point.born;
					const float fade = std::clamp((age - point.hold) / std::max(.001f, point.lifetime - point.hold), 0.f, 1.f);
					const float faded = fade * fade * (3.f - 2.f * fade);
					const float speed = std::clamp((point.speed / std::max(1.f, trace.peakSpeed) - .1f) / .5f, 0.f, 1.f);
					const float sweep = speed * speed * (3.f - 2.f * speed);
					const float progress = (point.distance - trace.points.front().distance) / strokeLength;
					const float releaseFade = std::clamp((release - .58f * progress) / .42f, 0.f, 1.f);
					const float feather = releaseFade * releaseFade * (3.f - 2.f * releaseFade);
					const float remaining = (1.f - faded) * (1.f - feather);
					const float emission = std::clamp((remaining - .13f) / .87f, 0.f, 1.f);
					const float glow = emission * emission;
					return std::array{
						1.15f * (1.f - .30f * faded) * (.65f + .35f * sweep) * (1.f - .45f * feather),
						glow * (3.f - 2.f * emission) * (.33f + .67f * sweep),
						glow
					};
				};
				const auto sample = [&](float distance) {
					const auto next = std::upper_bound(span.first, span.last, distance,
						[](float at, const RibbonTracePoint& point) { return at < point.distance; });
					const auto& a = *(next - 1);
					const auto& b = *next;
					const float t = std::clamp((distance - a.distance) / std::max(.0001f, b.distance - a.distance), 0.f, 1.f);
					const auto first = appearance(a), last = appearance(b);
					return std::array{std::lerp(first[0], last[0], t), std::lerp(first[1], last[1], t), std::lerp(first[2], last[2], t)};
				};
				spline.Set(trace.points, begin, finish, std::clamp(.115f * trace.reach, 7.f, 12.f));
				auto& strokeBounds = ends[span.trace];
				strokeBounds.rounded = spline.RoundedEnds();
				strokeBounds.detours = spline.BypassedDetours();
				strokeBounds.firstRow = rows.size();
				strokeBounds.rowCount = count;
				strokeBounds.meshBegin = begin;
				strokeBounds.meshEnd = finish;
				const float bodyRadius = std::min(width * std::clamp(length / 30.f, 3.f, 6.f), .09f * length);
				Vec3 lastSide{};
				for (std::size_t i = 0; i < count; ++i) {
					const float progress = static_cast<float>(i) / static_cast<float>(count - 1);
					const float distance = std::lerp(begin, finish, progress);
					const auto point = spline.At(distance);
					const float envelope = RibbonEnvelope(progress);
					const auto appearance = sample(distance);
					// Keep the thin leading point visible on the first moving frame.
					const float alpha = appearance[1] * std::sqrt(envelope * (2.f - envelope));
					const auto view = camera - point.position;
					auto side = point.tangent.Cross(view);
					const float facing = side.Length() / std::max(.001f, point.tangent.Length() * view.Length());
					if (side.Length() <= .001f) { side = ScatterAxis(lastSide, point.tangent); }
					if (side.Length() <= .001f) { side = ScatterAxis(point.tangent, point.tangent); }
					side = side * (1.f / std::max(.001f, side.Length()));
					if (lastSide.Dot(side) < 0.f) { side = side * -1.f; }
					if (facing < .20f && lastSide.Length() > .001f) {
						const float blend = std::clamp(facing / .20f, 0.f, 1.f);
						side = ScatterAxis(ScatterAxis(lastSide, point.tangent) * (1.f - blend) + side * blend, point.tangent);
					}
					lastSide = side;
					const auto edge = side * (bodyRadius * appearance[0] * envelope * envelope);
					rows.push_back({point.position - edge, point.position, point.position + edge, alpha, appearance[2], side});
				}
				if (finish == strokeBounds.head.distance) {
					const auto lastSegment = rows.back().center - rows[rows.size() - 2].center;
					const float segmentLength = lastSegment.Length();
					if (segmentLength > .001f) {
						strokeBounds.exitDirection = trace.exitDirection = lastSegment * (1.f / segmentLength);
						strokeBounds.exitSide = trace.exitSide = lastSide;
					}
				}
			}
		}
		ScatterPlacement PlaceScatterOnRibbon(const Particle& particle, const std::vector<RibbonRow>& rows,
			const std::vector<RibbonEnds>& strokes) const
		{
			const auto ends = std::ranges::find(strokes, particle.stroke, &RibbonEnds::stroke);
			if (ends == strokes.end()) { return {particle.position, 0.f}; }
			const float drift = std::max(0.f, (particle.position - particle.emissionCenter).Dot(particle.swingDirection));
			const float distance = std::max(ends->tail.distance, particle.strokeDistance + drift);
			const float settleStart = std::min(.045f, .25f * particle.fadeStart);
			const float settle = std::clamp((particle.age - settleStart) / std::max(.001f, particle.lifetime - settleStart), 0.f, 1.f);
			const float sway = settle * settle * (3.f - 2.f * settle);
			const float flutterPhase = 4.f * settle * (1.f - settle);
			const Vec3 settling{particle.settlingOffset.x * sway, particle.settlingOffset.y * sway,
				particle.settlingOffset.z * settle * settle};
			const auto place = [&](Vec3 center, Vec3 tangent, Vec3 side = {}) {
				if (particle.sideOffset != 0.f) {
					if (side.Length() <= .001f) { side = tangent.Cross(viewpoint - center); }
					center = center + side * (particle.sideOffset / std::max(.001f, side.Length()));
				}
				return ScatterPlacement{center + settling + particle.flutterOffset * (flutterPhase * flutterPhase), 1.f};
			};
			if (distance >= ends->head.distance) {
				const auto tangent = ends->exitDirection.Dot(ends->exitDirection) > .5f ? ends->exitDirection : particle.swingDirection;
				return place(ends->head.position + tangent * (distance - ends->head.distance), tangent, ends->exitSide);
			}
			if (ends->rowCount >= 2 && distance >= ends->meshBegin && distance <= ends->meshEnd) {
				const float index = (distance - ends->meshBegin) / (ends->meshEnd - ends->meshBegin) * (ends->rowCount - 1);
				const auto first = std::min(static_cast<std::size_t>(index), ends->rowCount - 2);
				const auto& a = rows[ends->firstRow + first];
				const auto& b = rows[ends->firstRow + first + 1];
				const auto tangent = b.center - a.center;
				const float t = std::clamp(index - first, 0.f, 1.f);
				return place(a.center + tangent * t, tangent, a.side * (1.f - t) + b.side * t);
			}
			const auto trace = std::ranges::find(strokeTraces, particle.stroke, &StrokeTrace::stroke);
			if (trace == strokeTraces.end()) { return {particle.position, 0.f}; }
			// Residual grains still have a path after the ribbon has faded.
			return place(RibbonTracePosition(trace->points, distance),
				RibbonTracePosition(trace->points, distance + 1.f) - RibbonTracePosition(trace->points, distance - 1.f));
		}
		void Seed(Pose pose)
		{
			const float length = (pose.tip - pose.base).Length();
			if (pose.base.Finite() && pose.tip.Finite() && length >= .1f && length <= 500.f) {
				current = pose;
				sampled = true;
			}
		}
		void Sample(Vec3 base, Vec3 tip, float delta, const Parameters& parameters, Vec3 actor = {})
		{
			if (ended) { return; }
			if (!base.Finite() || !tip.Finite() || !std::isfinite(delta) || delta <= 0.f) { Break(); return; }
			const Pose next{base,tip,actor};const float reach=(tip-base).Length();
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
				current = next;
				if (idle >= .04f) {
					Break();
					Seed(next);
				}
				return;
			}
			idle = 0.f;
			Emit(Curve(current, next, delta), parameters);
			current = next;
		}
	private:
		bool LinePointAlive(const RibbonTracePoint& point) const { return lineTime - point.born < point.lifetime; }

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
				while (interval > 1 && lengths[interval - 1] > target) { --interval; }
				while (interval < subdivisions && lengths[interval] < target) { ++interval; }
				const float span = lengths[interval] - lengths[interval - 1];
				const float fraction = span > .0001f ? std::clamp((target - lengths[interval - 1]) / span, 0.f, 1.f) : 1.f;
				return (interval - 1.f + fraction) / subdivisions;
			}
		};

		static Vec3 ScatterAxis(Vec3 blade, Vec3 travel)
		{
			const float speedSquared = travel.Dot(travel);
			if (speedSquared > .0001f) {
				blade = blade - travel * (blade.Dot(travel) / speedSquared);
				if (blade.Dot(blade) < .0001f) {
					const Vec3 reference = travel.z * travel.z < .9f * speedSquared ? Vec3{0.f, 0.f, 1.f} : Vec3{1.f, 0.f, 0.f};
					blade = travel.Cross(reference);
				}
			}
			return blade * (1.f / std::max(.001f, blade.Length()));
		}

		void Emit(const Curve& curve, const Parameters& parameters)
		{
			ArcDistances distances(curve);
			const float distance = distances.lengths.back();
			const float arcLife = parameters.ribbonLife + parameters.ribbonFade;
			const auto relative = (curve.end.tip - curve.end.actor) - (curve.start.tip - curve.start.actor);
			const float speed = relative.Length() / curve.duration;
			const auto linePoint = [&](float at, float t) {
				return RibbonTracePoint{at, curve.Position(t, 1.f), lineTime - curve.duration * (1.f - t), parameters.ribbonLife, arcLife, speed};
			};
			const bool beginsStroke = strokeTraces.empty() || strokeTraces.back().stroke != stroke;
			if (beginsStroke) {
				strokeTraces.push_back({stroke, 2.f, curve.startLength, speed, {linePoint(0.f, 0.f)}});
			}
			auto& trace = strokeTraces.back();
			trace.exitDirection = trace.exitSide = {};
			trace.peakSpeed = std::max(trace.peakSpeed, speed);
			const float finish = strokeDistance + distance;
			for (; trace.nextDistance < finish - .001f; trace.nextDistance += 2.f) {
				if (trace.nextDistance <= strokeDistance + .001f) { continue; }
				const float t = distances.TimeAt((trace.nextDistance - strokeDistance) / distance);
				trace.points.push_back(linePoint(trace.nextDistance, t));
			}
			trace.points.push_back(linePoint(finish, 1.f));
			if (trace.points.size() > maxRibbonRows) {
				// Keep the full path and reduce the sampling frequency of its older portion.
				const auto middle = trace.points.begin() + trace.points.size() / 2;
				auto output = trace.points.begin() + 1;
				for (auto point = output + 1; point < middle; point += 2) { *output++ = *point; }
				trace.points.erase(output, middle);
			}
			distances.interval = 1;
			// Space births in world distance; carry the fractional interval to the next pose.
			const float grainsPerUnit = 1.3f * parameters.density;
			const float phase = remainder;
			const float intervals = phase + distance * grainsPerUnit;
			const unsigned desiredGrains = static_cast<unsigned>(intervals);
			remainder = intervals - desiredGrains;
			if (particles.size() >= renderCapacity) { strokeDistance += distance; return; }
			if (!particles.capacity()) { particles.reserve(1024); }
			constexpr float spacing = 1.4f;
			const unsigned available = static_cast<unsigned>(renderCapacity - particles.size());
			const float retention = parameters.density > 0.f ? std::max(arcLife, parameters.particleLife * 1.15f + parameters.particleFade) : arcLife;
			const float frameShare = std::clamp(curve.duration * .90f / std::max(.001f, retention), 0.f, 1.f);
			const unsigned steadyBudget = std::max(2u, static_cast<unsigned>(renderCapacity * frameShare));
			const unsigned headroom = static_cast<unsigned>(renderCapacity / 2 - std::min(particles.size(), renderCapacity / 2));
			const unsigned frameBudget = std::min(available, steadyBudget + std::min(steadyBudget * 3 / 4, headroom / 4));
			const unsigned firstSparkSlot = parameters.density > 0.f && frameBudget > 1 ? 1u : 0u;
			const float grainRatio = grainsPerUnit * spacing;
			const unsigned grainReserve = firstSparkSlot ?
				std::clamp(static_cast<unsigned>(frameBudget * grainRatio / (1.f + grainRatio)), 1u, frameBudget / 2) : 0u;
			const unsigned desiredSamples = std::max(1u, static_cast<unsigned>(std::ceil(distance / spacing)));
			const unsigned samples = std::min({frameBudget - grainReserve, maxSamplesPerFrame, desiredSamples});
			const unsigned grainCount = std::min({frameBudget - samples, samples * 12,
				std::max(desiredGrains, needsFirstSpark ? firstSparkSlot : 0u)});
			for (unsigned i=0;i<samples;++i) {
				const float fraction = (i + 1.f) / samples;
				const float arcFraction = beginsStroke && samples > 1 ? static_cast<float>(i) / (samples - 1) : fraction;
				const float t = distances.TimeAt(arcFraction);
				const float birthAge = curve.duration * (1.f - t);
				if (birthAge < arcLife) {
					Particle arc{curve.Position(t, 1.f), {}, 0.f, arcLife,
						1.15f, .85f, true, parameters.ribbonLife, arcFraction, stroke, strokeDistance + distance * arcFraction};
					Move(arc, birthAge, {});
					Add(arc);
				}
			}
			for (unsigned grain = 0; grain < grainCount; ++grain) {
				const auto interval = 1u + static_cast<unsigned>((grain + .5f) * desiredGrains / grainCount);
				const bool firstSpark = needsFirstSpark && grainCount == 1 && (desiredGrains == 0 || (beginsStroke && distance < 16.f));
				const float fraction = firstSpark ? .5f : std::clamp((interval - phase) / (distance * grainsPerUnit), 0.f, 1.f);
				if (firstSpark && desiredGrains == 0) { remainder = .5f * distance * grainsPerUnit; }
				const float t = distances.TimeAt(fraction);
				const float grainAge = curve.duration * (1.f - t);
				const auto evaluated = curve.Evaluate(t);
				const auto travel = evaluated.Velocity(1.f);
				const float speed = travel.Length();
				const auto direction = travel * (1.f / std::max(.001f, speed));
				const auto velocity = direction * (std::min(135.f, speed) * parameters.momentum);
				const float retained = parameters.particleLife * (.85f + Random() * .3f);
				Particle p{evaluated.at.tip, velocity, 0.f, retained + parameters.particleFade,
					(.45f + Random() * .3f) * parameters.size, .8f + Random() * .2f, false, retained,
					fraction, stroke, strokeDistance + distance * fraction, direction, evaluated.at.tip};
				const float sideRoll = Random() - .5f;
				p.sideOffset = sideRoll * parameters.particleWidth;
				const float settleStrength = parameters.momentum * std::clamp(p.lifetime / .45f, .55f, 1.6f);
				const Vec3 up{0.f, 0.f, 1.f};
				const auto side = ScatterAxis(direction.Cross(up), direction);
				const float flutterRoll = (p.intensity - .8f) * 5.f - .5f;
				p.settlingOffset = (side * (12.f * sideRoll) - up * (6.f + 20.f * (p.intensity - .8f))) * settleStrength;
				p.flutterOffset = (side * (7.f * flutterRoll) + direction * (3.f * sideRoll)) * settleStrength;
				Move(p, grainAge, Motion::For(grainAge));
				Add(p);
				needsFirstSpark = false;
			}
			strokeDistance += distance;
		}
		std::vector<Particle> particles;
		struct StrokeTrace
		{
			std::uint32_t stroke;
			float nextDistance, reach, peakSpeed;
			std::vector<RibbonTracePoint> points;
			Vec3 exitDirection, exitSide;
			std::optional<float> releasedAt;
		};
		std::vector<StrokeTrace> strokeTraces;
		std::size_t renderCapacity = capacity;
		Pose current;
		Vec3 viewpoint;
		std::uint32_t stroke = 0;
		bool sampled = false, ended = false, needsFirstSpark = true;
		float remainder = .5f, idle = 0.f, strokeDistance = 0.f, lineTime = 0.f;
		std::uint32_t random = 0x912e5c3bu;
		float Random() { random^=random<<13;random^=random>>17;random^=random<<5;return (random&0xffffff)/16777216.f; }
		struct Motion
		{
			float decay=1.f,travel=0.f;
			static Motion For(float delta) { const float decay=std::exp(-3.f*delta);return {decay,(1.f-decay)/3.f}; }
		};
		static void Move(Particle& p,float delta,Motion motion)
		{
			p.position = p.position + p.velocity * motion.travel;
			p.velocity = p.velocity * motion.decay;
			p.age += delta;
		}
		void Add(const Particle& p) { if (p.age<p.lifetime && particles.size()<renderCapacity) { particles.push_back(p); } }
	};
}
