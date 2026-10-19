#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
namespace RE { class NiNode; }

namespace RedsandImpactVFX
{
	enum class Availability { Waiting, NotInstalled, Ready, InvalidRoutes };
	struct Status
	{
		Availability availability = Availability::Waiting;
		std::size_t resolved = 0;
		std::size_t missing = 0;
		std::size_t conflicts = 0;
		std::size_t definitions = 0;
		std::size_t resourceFailures = 0;
		std::size_t wrongTypes = 0;
		std::size_t unknownModels = 0;
		int appliedIndex = -1;
		bool appliedEnabled = false;
		bool appliedHit = false;
		bool appliedSlash = false;
		bool appliedBlock = false;
		std::uint64_t applyPasses = 0;
		std::uint64_t modelsChanged = 0;
		std::uint64_t slashHits = 0;
		std::uint64_t slashSpawned = 0;
		std::uint64_t slashFailed = 0;
		std::uint64_t slashFallbacks = 0;
		bool slashCallback = false;
		bool worldCallback = false;
		bool arrowCallback = false;
		std::size_t nativeResolved = 0;
		bool valhallaRingControlled = false;
		std::uint64_t nativeBlocks = 0;
		std::uint64_t nativeWorldHits = 0;
		std::uint64_t nativeArrowHits = 0;
		std::string detail;
	};
	void Initialize();
	void Update();
	[[nodiscard]] Status GetStatus();
	bool BindRandomSlashRoot(RE::NiNode* root);
}
