#include "../source/RedsandSettings.h"
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	std::mutex previousLock;
	RedsandSettings::Config previousConfig;
	RedsandSettings::Config PreviousRead()
	{
		std::lock_guard lock(previousLock);
		return previousConfig;
	}
	using Reader = RedsandSettings::Config (*)();
	std::atomic<double> sink{ 0. };
	double Measure(Reader read, unsigned threads)
	{
		constexpr unsigned iterations = 250000;
		std::barrier start(static_cast<std::ptrdiff_t>(threads + 1));
		std::vector<std::thread> workers;
		for (unsigned i = 0; i < threads; ++i) {
			workers.emplace_back([&] {
				for (unsigned j = 0; j < 100; ++j) { sink.fetch_add(read().impactDensity, std::memory_order_relaxed); }
				start.arrive_and_wait();
				double sum = 0.;
				for (unsigned j = 0; j < iterations; ++j) { const auto config = read(); sum += config.impactDensity + config.colors[0].rgb[0]; }
				sink.fetch_add(sum, std::memory_order_relaxed);
			});
		}
		const auto begin = std::chrono::steady_clock::now();
		start.arrive_and_wait();
		for (auto& worker : workers) { worker.join(); }
		return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / (iterations * threads);
	}
}

int main()
{


	std::cout << "{\"settings_read_microbenchmark\":true,\"game_fps_benchmark\":false,\"results\":[";
	for (const unsigned threads : { 1u, 4u }) {
		std::vector<double> before, after;
		for (unsigned round = 0; round < 7; ++round) {
			if (round % 2 == 0) { before.push_back(Measure(PreviousRead, threads)); after.push_back(Measure(RedsandSettings::Get, threads)); }
			else { after.push_back(Measure(RedsandSettings::Get, threads)); before.push_back(Measure(PreviousRead, threads)); }
		}
		std::sort(before.begin(), before.end()); std::sort(after.begin(), after.end());
		if (threads != 1) { std::cout << ','; }
		std::cout << "{\"threads\":" << threads << ",\"previous_equivalent_lock_ns_per_read\":" << before[3]
			<< ",\"cached_production_ns_per_read\":" << after[3] << '}';
	}
	std::cout << "]}\n";
}
