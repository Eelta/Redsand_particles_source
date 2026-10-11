#include "../source/RedsandMotionTrace.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

void check(bool value, const char* message)
{
	if (!value) { throw std::runtime_error(message); }
}

int main() try
{
	Redsand::MotionTrace trace;
	std::atomic_uint64_t ticks = 0;
	auto clock = [&] { return ++ticks; };
	trace.Capture(true, 1, 1, 1, "disabled", "", clock);
	check(ticks == 0 && trace.Drain().count == 0, "default-off trace did work");
	trace.Enable(true);
	trace.Capture(false, 1, 1, 1, "disabled", "", clock);
	trace.Capture(true, 2, 1, 1, "inactive graph", "", clock);
	trace.Capture(true, 0, 0, 1, "missing source", "", clock);
	trace.Capture(true, 1, 1, 1, nullptr, "", clock);
	check(ticks == 0, "excluded events consulted the clock");
	for (int i = 0; i < 1000; ++i) { trace.Capture(true, 1, 1, 7, std::to_string(i).c_str(), "PIE.$Idle_Stop", clock); }
	auto batch = trace.Drain();
	check(batch.count == 64 && batch.dropped == 936, "event flood was not bounded/reported");
	for (std::size_t i = 0; i < batch.count; ++i) {
		const auto& event = batch.events[i];
		check(event.sequence == i + 937 && event.time == i + 937 && event.source == 1 && event.epoch == 7, "latest event order or identity lost");
		check(std::string(event.tag.data()) == std::to_string(i + 936), "ring retained wrong events");
	}
	check(trace.Drain().count == 0, "drain replayed events");
	trace.Capture(true, 2, 2, 7, "InputManager_ActionTriggered", "Power Attack", clock, 6.f, true);
	batch = trace.Drain();
	check(batch.count == 1 && batch.events[0].hasNumber && batch.events[0].number == 6.f && batch.events[0].source == 2, "input action ID or independent source lost");
	std::string unicode;
	for (int i = 0; i < 100; ++i) { unicode += "\xE5\xBC\x93"; }
	trace.Capture(true, 1, 1, 8, unicode.c_str(), "one\nline\r\tvalue", clock);
	batch = trace.Drain();
	check(batch.count == 1 && batch.events[0].truncated && std::string(batch.events[0].tag.data()).size() == 78, "UTF-8 boundary broken");
	check(std::string(batch.events[0].payload.data()) == "one line  value", "log line injection was retained");
	trace.Capture(true, 1, 1, 8, "discard on disable", nullptr, clock);
	trace.Enable(false);
	trace.Capture(true, 1, 1, 8, "late callback", nullptr, clock);
	check(trace.Drain().count == 0, "disabled queue kept old or late events");
	trace.Enable(true);
	std::vector<std::thread> writers;
	for (int n = 0; n < 8; ++n) {
		writers.emplace_back([&, n] {
			const auto text = std::string(50, static_cast<char>('A' + n));
			for (int i = 0; i < 1000; ++i) { trace.Capture(true, 9, 9, 10, text.c_str(), text.c_str(), clock); }
		});
	}
	for (auto& writer : writers) { writer.join(); }
	batch = trace.Drain();
	check(batch.count == 64 && batch.dropped == 7936, "concurrent flood accounting wrong");
	for (std::size_t i = 0; i < batch.count; ++i) {
		const auto& event = batch.events[i];
		check(std::string(event.tag.data()) == std::string(event.payload.data()) && !event.truncated, "concurrent event data torn");
		if (i) { check(event.sequence == batch.events[i - 1].sequence + 1, "concurrent event ordering lost"); }
	}
	std::cout << "PASS: disabled trace, camera isolation, overflow, order, UTF-8, reset and concurrent event capture\n";
	return 0;
}
catch (const std::exception& error)
{
	std::cerr << error.what() << '\n';
	return 1;
}
