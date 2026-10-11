#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

void check(bool condition, const char* message)
{
	if (!condition) { throw std::runtime_error(message); }
}

struct Manager;
struct UnreadableDependencies
{
	Manager** begin() const { throw std::runtime_error("dependent-manager memory was accessed"); }
	Manager** end() const { return nullptr; }
};

struct Graph {};
struct Lock { bool held = false; };
namespace RE
{
	struct BSSpinLockGuard
	{
		Lock& lock;
		explicit BSSpinLockGuard(Lock& value) : lock(value) { check(!lock.held, "snapshot lock reentered"); lock.held = true; }
		~BSSpinLockGuard() { lock.held = false; }
	};
}

struct GraphArray
{
	Lock& lock;
	std::vector<std::shared_ptr<Graph>> values;

	std::size_t size() const { check(lock.held, "graph count read without lock"); return values.size(); }
	const auto& operator[](std::size_t i) const { check(lock.held, "graph read without lock"); return values.at(i); }
};

struct Manager
{
	struct Runtime { Lock updateLock; std::uint32_t activeGraph = 0; } runtime;
	GraphArray graphs{ runtime.updateLock, {} };
	UnreadableDependencies subManagers;
	Runtime& GetRuntimeData() { return runtime; }
};

void CollectPlayerGraphs(Manager* manager, std::vector<std::shared_ptr<Graph>>& result, Graph*& active)
{
#include "../source/BowGraphSnapshot.inc"
}

int main() try
{
	Manager manager;
	bool caught = false;
	try { for ([[maybe_unused]] auto child : manager.subManagers) {} }
	catch (const std::runtime_error&) { caught = true; }
	check(caught, "negative control did not detect the v4 dependent-manager access");

	std::vector<std::shared_ptr<Graph>> result;
	Graph* active = nullptr;
	CollectPlayerGraphs(nullptr, result, active);
	check(result.empty() && !active, "missing actor manager created a graph");
	CollectPlayerGraphs(&manager, result, active);
	check(result.empty() && !active, "unloaded manager created a graph");

	auto third = std::make_shared<Graph>();
	auto first = std::make_shared<Graph>();
	manager.graphs.values = { third, first, nullptr, third };
	manager.runtime.activeGraph = 1;
	CollectPlayerGraphs(&manager, result, active);
	check(result.size() == 2 && result[0] == third && result[1] == first, "snapshot lost a camera graph or included duplicates/nulls");
	check(active == first.get(), "first-person active graph was not retained");
	check(!manager.runtime.updateLock.held, "snapshot left the manager locked");

	std::weak_ptr<Graph> retiredFirst = first;
	manager.graphs.values = { third };
	first.reset();
	check(!retiredFirst.expired(), "graph destroyed before its listener could be detached");
	result.clear();
	check(retiredFirst.expired(), "retired graph leaked after releasing the snapshot");
	active = nullptr;
	manager.runtime.activeGraph = 99;
	CollectPlayerGraphs(&manager, result, active);
	check(result.size() == 1 && !active, "invalid active index read outside the graph array");
	result.clear();
	manager.runtime.activeGraph = 0;
	CollectPlayerGraphs(&manager, result, active);
	check(active == third.get(), "third-person graph did not survive first-person removal");

	result.clear();
	active = nullptr;
	manager.graphs.values.clear();
	for (int i = 0; i < 32; ++i) { manager.graphs.values.push_back(std::make_shared<Graph>()); }
	manager.runtime.activeGraph = 31;
	CollectPlayerGraphs(&manager, result, active);
	check(result.size() == 16 && !active, "graph budget/active ownership was not bounded");
	std::cout << "PASS: actual graph snapshot, forbidden dependent reads, both cameras, replacement lifetime, nulls, active-index bounds\n";
}
catch (const std::exception& error)
{
	std::cerr << "FAIL: " << error.what() << '\n';
	return 1;
}
