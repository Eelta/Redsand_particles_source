#include "../source/RedsandBowStreamBudget.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
	constexpr std::size_t capacity = 32;
	struct Stream
	{
		bool charging = false, emitting = true;
		float age = 0.f;
		unsigned shot = 0;
	};
	using Streams = std::vector<std::unique_ptr<Stream>>;

	void Check(bool condition, const char* message)
	{
		if (!condition) { throw std::runtime_error(message); }
	}

	void Add(Streams& streams, unsigned shot, float age = 0.f, bool charging = false, bool emitting = true)
	{
		streams.push_back(std::make_unique<Stream>(Stream{ charging, emitting, age, shot }));
	}

	void RapidFireCanAlwaysAdmitTheNextShot()
	{
		Streams streams;
		for (unsigned shot = 0; shot < capacity; ++shot) {
			Add(streams, shot, static_cast<float>(capacity - shot) * 0.1f);
		}

		Check(!(streams.size() < capacity), "fixture did not reach the old stream rejection limit");
		for (unsigned shot = capacity; shot < 320; ++shot) {
			const auto victim = Redsand::Bow::StreamToRetire(streams, capacity);
			Check(victim < streams.size(), "a full rapid-fire burst blocked its next shot");
			Check(streams[victim]->shot == shot - capacity, "replacement did not retire the oldest flying arrow");
			streams.erase(streams.begin() + victim);
			Add(streams, shot);
			Check(streams.size() == capacity && streams.back()->shot == shot, "the next shot was not admitted within the budget");
			for (auto& stream : streams) { stream->age += 0.1f; }
		}
	}

	void StoppedStreamsYieldBeforeLiveProjectiles()
	{
		Streams streams;
		Add(streams, 0, 20.f);
		Add(streams, 1, 2.f, false, false);
		Add(streams, 2, 4.f, true, false);
		Check(Redsand::Bow::StreamToRetire(streams, 3) == 2, "oldest stopped stream did not yield first");
		streams[2]->emitting = true;
		Check(Redsand::Bow::StreamToRetire(streams, 3) == 1, "live projectile displaced a stopped stream");
		streams[1]->emitting = true;
		Check(Redsand::Bow::StreamToRetire(streams, 3) == 0, "oldest projectile did not yield after stopped streams were exhausted");
	}

	void HeldChargesKeepTheirSlots()
	{
		Streams streams;
		Add(streams, 0, 100.f, true);
		Add(streams, 1, 1.f);
		Add(streams, 2, 2.f);
		Check(Redsand::Bow::StreamToRetire(streams, 3) == 2, "an active charge was selected over a flying arrow");
		streams[1]->charging = streams[2]->charging = true;
		Check(Redsand::Bow::StreamToRetire(streams, 3) == streams.size(), "an active charge was evicted from an all-charge budget");

		Streams full;
		for (unsigned shot = 0; shot < capacity; ++shot) { Add(full, shot, static_cast<float>(shot), true); }
		Check(Redsand::Bow::StreamToRetire(full, capacity) == full.size(), "32 active charges failed to reject a replacement");
	}

	void SpareCapacityNeverEvicts()
	{
		Streams streams;
		Check(Redsand::Bow::StreamToRetire(streams, capacity) == streams.size(), "empty budget selected a victim");
		Add(streams, 0, 100.f, false, false);
		Check(Redsand::Bow::StreamToRetire(streams, capacity) == streams.size(), "a stopped stream was evicted despite spare capacity");
		for (unsigned shot = 1; shot < capacity - 1; ++shot) { Add(streams, shot); }
		Check(Redsand::Bow::StreamToRetire(streams, capacity) == streams.size(), "the last free slot triggered an eviction");
	}
}

int main() try
{
	RapidFireCanAlwaysAdmitTheNextShot();
	StoppedStreamsYieldBeforeLiveProjectiles();
	HeldChargesKeepTheirSlots();
	SpareCapacityNeverEvicts();
	std::cout << "PASS: rapid-fire stream admission, retirement priority and active-charge protection\n";
}
catch (const std::exception& error)
{
	std::cerr << "FAIL: " << error.what() << '\n';
	return 1;
}
