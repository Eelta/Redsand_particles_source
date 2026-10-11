#pragma once

#include "RedsandBowSimulation.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Redsand::Bow
{

	template <class Streams>
	std::size_t StreamToRetire(const Streams& streams, std::size_t capacity)
	{
		const auto none = streams.size();
		if (streams.size() < capacity) { return none; }
		std::size_t stopped = none, projectile = none;
		for (std::size_t i = 0; i < streams.size(); ++i) {
			const auto& stream = *streams[i];
			if (!stream.emitting) {
				if (stopped == none || stream.age > streams[stopped]->age) { stopped = i; }
			} else if (!stream.charging) {
				if (projectile == none || stream.age > streams[projectile]->age) { projectile = i; }
			}
		}
		return stopped != none ? stopped : projectile;
	}

	template <class Streams, class Reusable>
	std::size_t StreamToReuse(const Streams& streams, std::size_t capacity, Reusable reusable)
	{
		if (streams.size() < capacity) { return streams.size(); }
		std::size_t victim = streams.size();
		for (std::size_t i = 0; i < streams.size(); ++i) {
			const auto& stream = *streams[i];
			if ((stream.charging && stream.emitting) || !reusable(stream)) { continue; }
			if (victim == streams.size() || (!stream.emitting && streams[victim]->emitting) ||
				(stream.emitting == streams[victim]->emitting && stream.age > streams[victim]->age)) { victim = i; }
		}
		return victim;
	}

	template <class Streams>
	void NewestLeaseAtBack(Streams& streams, std::size_t lease)
	{

		std::rotate(streams.begin() + lease, streams.begin() + lease + 1, streams.end());
	}

	template <class Stream>
	void ResetStreamLease(Stream& stream, std::uint32_t seed)
	{
		stream.simulation.Reset(seed);

		stream.rendererStartAge -= stream.age;
		stream.age = stream.chargeTime = stream.silence = 0.f;
		stream.chargeSequence = 0;
		stream.charging = stream.seen = false;
		stream.emitting = true;
		stream.rendererBrightness = -1.f;
		stream.projectile = {};
	}

	template <class Shots, class Stream>
	void ForgetRendererLease(Shots& shots, const Stream& stream)
	{
		if (!stream.charging) {
			if (auto shot = shots.find(stream.projectile); shot != shots.end()) { shot->second.rendererResident = false; }
		}
	}

	template <class Sample>
	class SampleQueue
	{
		using Handle = decltype(std::declval<Sample>().handle);
		using Priority = std::multimap<std::pair<float, std::uint64_t>, Handle>;
		struct Entry { Sample sample; typename Priority::iterator priority; };
		std::unordered_map<Handle, Entry> entries;
		Priority priority;
		std::size_t capacity;
		std::uint64_t dropped = 0, merged = 0;

	public:
		explicit SampleQueue(std::size_t limit) : capacity(limit) { entries.reserve(limit); }
		std::size_t Size() const { return entries.size(); }
		bool HasBirth(const Handle& handle) const
		{
			const auto found = entries.find(handle);
			return found != entries.end() && found->second.sample.livingTime <= 0.25f;
		}
		std::uint64_t Dropped() const { return dropped; }
		std::uint64_t Merged() const { return merged; }
		void Clear() { entries.clear(); priority.clear(); dropped = merged = 0; }

		bool Push(Sample sample)
		{
			if (!capacity || !std::isfinite(sample.livingTime) || sample.livingTime < 0.f ||
				!std::isfinite(sample.delta) || sample.delta <= 0.f) { ++dropped; return false; }
			if (!sample.lastSequence) { sample.lastSequence = sample.sequence; }
			if (sample.latestLivingTime < 0.f) { sample.latestLivingTime = sample.livingTime; }
			if (!std::isfinite(sample.latestLivingTime) || sample.latestLivingTime < sample.livingTime || sample.lastSequence < sample.sequence) {
				++dropped; return false;
			}
			if constexpr (requires { sample.path; }) {
				if (!sample.path.Size()) { sample.path.Add({ sample.from, sample.to, sample.delta, sample.continuous, sample.sequence, sample.lastSequence }); }
			}
			if (auto found = entries.find(sample.handle); found != entries.end()) {
				auto& queued = found->second.sample;
				if (sample.sequence < queued.sequence && sample.livingTime > queued.livingTime + 0.001f) { ++dropped; return false; }
				priority.erase(found->second.priority);
				if (sample.sequence > queued.lastSequence && sample.latestLivingTime + 0.001f < queued.latestLivingTime && sample.livingTime <= 0.25f) {
					queued = std::move(sample);
					found->second.priority = priority.emplace(std::pair{ queued.livingTime, ~std::uint64_t{ 0 } - queued.sequence }, queued.handle);
					return true;
				}

				const float delta = static_cast<float>(std::min(static_cast<double>(queued.delta) + sample.delta,
					static_cast<double>(std::numeric_limits<float>::max())));
				if constexpr (requires { queued.path; sample.path; }) { queued.path.Merge(sample.path); }
				queued.continuous = queued.continuous && sample.continuous;
				if (sample.sequence < queued.sequence) {
					queued.continuous = queued.continuous && (sample.to - queued.from).Length() <= 128.f;
					queued.from = sample.from;
					queued.direction = sample.direction;
					queued.sequence = sample.sequence;
					queued.livingTime = sample.livingTime;
					queued.weaponID = sample.weaponID;
					queued.power = sample.power;
				}
				if (sample.lastSequence > queued.lastSequence) {
					queued.continuous = queued.continuous && (sample.from - queued.to).Length() <= 128.f;
					queued.to = sample.to;
					queued.flying = sample.flying;
					queued.lastSequence = sample.lastSequence;
					queued.latestLivingTime = sample.latestLivingTime;
				}
				queued.delta = delta;
				found->second.priority = priority.emplace(std::pair{ queued.livingTime, ~std::uint64_t{ 0 } - queued.sequence }, queued.handle);
				++merged;
				return true;
			}
			const auto rank = std::pair{ sample.livingTime, ~std::uint64_t{ 0 } - sample.sequence };
			if (entries.size() >= capacity) {
				auto victim = std::prev(priority.end());

				if (sample.livingTime > victim->first.first) { ++dropped; return false; }
				entries.erase(victim->second);
				priority.erase(victim);
				++dropped;
			}
			auto order = priority.emplace(rank, sample.handle);
			entries.emplace(sample.handle, Entry{ std::move(sample), order });
			return true;
		}

		void Drain(std::vector<Sample>& result)
		{
			result.reserve(result.size() + entries.size());
			for (auto& [handle, entry] : entries) { result.push_back(std::move(entry.sample)); }
			entries.clear();
			priority.clear();
		}
	};

	template <class Handle>
	class BirthReservations
	{
		std::unordered_map<Handle, std::size_t> active;
		std::size_t capacity, outstanding = 0;

	public:
		explicit BirthReservations(std::size_t limit) : capacity(limit) {}
		bool Begin(const Handle& handle)
		{
			if (outstanding >= capacity) { return false; }
			++active[handle];
			++outstanding;
			return true;
		}
		void End(const Handle& handle)
		{
			const auto found = active.find(handle);
			if (found == active.end()) { return; }
			if (!--found->second) { active.erase(found); }
			--outstanding;
		}
		bool Contains(const Handle& handle) const { return active.contains(handle); }
		std::size_t Count() const { return outstanding; }
		void Clear() { active.clear(); outstanding = 0; }
	};
}
