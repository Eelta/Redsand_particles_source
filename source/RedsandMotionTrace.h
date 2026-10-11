#pragma once

#include <array>
#include <cstdint>
#include <mutex>

namespace Redsand
{
	class MotionTrace
	{
	public:
		struct Event
		{
			std::uint64_t sequence = 0, time = 0, epoch = 0;
			std::uintptr_t source = 0;
			std::array<char, 80> tag{};
			std::array<char, 112> payload{};
			bool truncated = false;
			bool hasNumber = false;
			float number = 0.f;
		};
		struct Batch
		{
			std::array<Event, 64> events{};
			std::size_t count = 0;
			std::uint64_t dropped = 0;
		};

		void Enable(bool value)
		{
			std::lock_guard lock(mutex);
			if (active == value) { return; }
			active = value;
			head = count = 0;
			dropped = 0;
		}

		template<class Clock>
		void Capture(bool enabled, std::uintptr_t source, std::uintptr_t expectedSource,
			std::uint64_t epoch, const char* tag, const char* payload, Clock&& clock, float number = 0.f, bool hasNumber = false)
		{
			if (!enabled || !source || source != expectedSource || !tag || !*tag) { return; }
			std::lock_guard lock(mutex);
			if (!active) { return; }
			if (count == events.size()) { head = (head + 1) % events.size(); --count; ++dropped; }
			auto& event = events[(head + count++) % events.size()];
			event = {};
			event.sequence = ++sequence;
			event.time = clock();
			event.source = source;
			event.epoch = epoch;
			event.number = number;
			event.hasNumber = hasNumber;
			event.truncated = Copy(event.tag, tag);
			event.truncated = Copy(event.payload, payload) || event.truncated;
		}

		Batch Drain()
		{
			std::lock_guard lock(mutex);
			Batch batch;
			batch.count = count;
			batch.dropped = dropped;
			for (std::size_t i = 0; i < count; ++i) { batch.events[i] = events[(head + i) % events.size()]; }
			head = count = 0;
			dropped = 0;
			return batch;
		}

	private:
		template<std::size_t N>
		static bool Copy(std::array<char, N>& destination, const char* value)
		{
			if (!value) { return false; }
			std::size_t size = 0;
			while (size + 1 < N && value[size]) { ++size; }
			const bool truncated = value[size] != '\0';
			if (truncated) { while (size && (static_cast<unsigned char>(value[size]) & 0xC0) == 0x80) { --size; } }
			for (std::size_t i = 0; i < size; ++i) { destination[i] = static_cast<unsigned char>(value[i]) < 0x20 ? ' ' : value[i]; }
			destination[size] = '\0';
			return truncated;
		}

		std::mutex mutex;
		std::array<Event, 64> events{};
		std::size_t head = 0, count = 0;
		std::uint64_t sequence = 0, dropped = 0;
		bool active = false;
	};
}
