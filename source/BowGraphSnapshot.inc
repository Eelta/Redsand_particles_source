if (!manager) { return; }

			RE::BSSpinLockGuard lock(manager->GetRuntimeData().updateLock);
			const auto activeIndex = manager->GetRuntimeData().activeGraph;
			for (std::size_t i = 0; i < manager->graphs.size() && result.size() < 16; ++i) {
				const auto& graph = manager->graphs[i];
				if (!graph) { continue; }
				if (std::none_of(result.begin(), result.end(), [&](const auto& old) { return old.get() == graph.get(); })) { result.push_back(graph); }
				if (i == activeIndex) { active = graph.get(); }
			}
