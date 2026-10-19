#pragma once

#include "RedsandSettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>

namespace RedsandImpactVFX
{
	enum class Category { Hit, Slash, Block };
	enum class Kind { Impact, Explosion, Projectile };
	enum class NativeLayer : unsigned { Hit, Stone, Slash, Block, Wave, Count };
	inline constexpr std::array<std::string_view, 5> nativeLayerNames{ "Hit", "Stone", "Slash", "Block", "Wave" };
	inline constexpr std::array<std::string_view, static_cast<unsigned>(RedsandSettings::BlockStyle::Count)> blockStyleModelNames{
		"NormalBlock.nif", "TimedSparks.nif", "TimedFlare.nif", "PerfectRing.nif", "SimpleTimed.nif", "StarRing.nif"
	};
	using BlockStyleModels = std::array<std::string, blockStyleModelNames.size()>;
	using BlockStyleAvailability = std::array<bool, blockStyleModelNames.size()>;
	struct NativeRoute
	{
		NativeLayer layer = NativeLayer::Hit;
		std::string model;
		float duration = .85f;
	};
	inline Category NativeCategory(NativeLayer layer)
	{
		return layer == NativeLayer::Slash ? Category::Slash : layer == NativeLayer::Block ? Category::Block : Category::Hit;
	}
	struct Route
	{
		std::string plugin;
		std::uint32_t form = 0;
		Kind kind = Kind::Impact;
		Category category = Category::Hit;
		std::string originalModel;
		std::string baseModel;
		std::string offModel;
		std::array<std::string, 64> models;
		float originalDuration = 0.0f;
		float duration = 0.85f;
	};

	inline std::string NormalizePath(std::string_view path)
	{
		std::string result(path);
		for (auto& c : result) {
			if (c == '/') { c = '\\'; }
			else if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c + ('a' - 'A')); }
		}
		return result;
	}

	inline bool ValidModelPath(std::string_view path)
	{
		if (path.empty() || path.size() > 512 || path.find('\0') != path.npos || path.find(':') != path.npos) { return false; }
		const auto normalized = NormalizePath(path);
		if (normalized.front() == '\\' || !normalized.ends_with(".nif") || normalized.starts_with("meshes\\") ||
			normalized.starts_with("data\\")) { return false; }
		std::size_t offset = 0;
		while (offset < normalized.size()) {
			const auto end = normalized.find('\\', offset);
			const auto component = normalized.substr(offset, end == std::string::npos ? end : end - offset);
			if (component.empty() || component == "." || component == "..") { return false; }
			if (end == std::string::npos) { break; }
			offset = end + 1;
		}
		return true;
	}

	inline bool ValidRoute(const Route& route)
	{
		if (NormalizePath(route.plugin) != "skyrim.esm" ||
			route.form == 0 || route.form > 0xFFFFFF || !ValidModelPath(route.baseModel) ||
			(!route.originalModel.empty() && !ValidModelPath(route.originalModel)) ||
			(!route.offModel.empty() && !ValidModelPath(route.offModel)) ||
			(route.originalModel.empty() && route.offModel.empty())) { return false; }
		for (const auto& model : route.models) { if (!ValidModelPath(model)) { return false; } }
		return route.kind != Kind::Impact || (std::isfinite(route.originalDuration) && route.originalDuration >= 0.0f &&
			route.originalDuration <= 30.0f && std::isfinite(route.duration) && route.duration > 0.0f && route.duration <= 30.0f);
	}
	inline bool ValidNativeRoute(const NativeRoute& route)
	{
		return static_cast<unsigned>(route.layer) < nativeLayerNames.size() && ValidModelPath(route.model) &&
			std::isfinite(route.duration) && route.duration > 0.f && route.duration <= 30.f;
	}
	inline BlockStyleModels BlockStyleModelPaths(const NativeRoute& route)
	{
		BlockStyleModels models;
		if (route.layer != NativeLayer::Block || !ValidNativeRoute(route)) { return models; }
		const auto separator = route.model.find_last_of("/\\");
		const auto directory = separator == std::string::npos ? std::string{} : route.model.substr(0, separator + 1);
		for (std::size_t index = 0; index < models.size(); ++index) { models[index] = directory + std::string(blockStyleModelNames[index]); }
		models[0] = route.model;
		models[static_cast<unsigned>(RedsandSettings::BlockStyle::StarRing)] = "StellarImpact/Block/StarRing.nif";
		return models;
	}
	inline unsigned BlockStyleIndex(RedsandSettings::BlockStyle style, const BlockStyleAvailability& available)
	{
		const auto index = static_cast<unsigned>(style);
		return index < available.size() && available[index] ? index : 0;
	}
	inline float BlockStyleDuration(float duration, unsigned selectedStyle)
	{
		return selectedStyle == static_cast<unsigned>(RedsandSettings::BlockStyle::StarRing) ? std::max(duration, 2.f) : duration;
	}

	inline int ModelIndex(const RedsandSettings::Config&)
	{
		return 42;
	}

	inline bool Enabled(Category category, const RedsandSettings::Config& config)
	{
		return config.impactEnabled && (category == Category::Hit ? config.impactHit :
			category == Category::Slash ? config.impactSlash : config.impactBlock);
	}

	struct NativeHitLayers { bool fallbackHit = false; bool slash = false; bool refraction = false; };
	inline NativeHitLayers SelectNativeHitLayers(bool actorTarget, bool physicalMelee, bool blockedOrBash,
		bool blade, bool, bool, bool power, bool critical, const RedsandSettings::Config& config)
	{
		if (!actorTarget || !physicalMelee || blockedOrBash) { return {}; }
		return { Enabled(Category::Hit, config),
			Enabled(Category::Slash, config) && blade,
			Enabled(Category::Hit, config) && (power || critical) };
	}
	inline bool SelectNativeBlock(bool physicalMelee, bool blocked, bool bash, const RedsandSettings::Config& config)
	{
		return physicalMelee && blocked && !bash && Enabled(Category::Block, config);
	}
	inline bool SelectNativeWorld(bool actorTarget, bool physicalMelee, bool bash, const RedsandSettings::Config& config)
	{
		return !actorTarget && physicalMelee && !bash && Enabled(Category::Hit, config);
	}
	inline std::optional<NativeLayer> SelectNativeProjectileLayer(bool arrow, bool blocked, bool actorTarget,
		const RedsandSettings::Config& config)
	{
		if (!arrow) { return {}; }
		const auto layer = blocked ? NativeLayer::Block : actorTarget ? NativeLayer::Hit : NativeLayer::Stone;
		return Enabled(NativeCategory(layer), config) ? std::optional<NativeLayer>{ layer } : std::nullopt;
	}
	inline bool NativeWorldFallbackRequired(std::size_t expected, std::size_t controlled)
	{


		return expected < 26 || controlled != expected;
	}

	inline const std::string& DisabledModel(const Route& route) { return route.offModel.empty() ? route.originalModel : route.offModel; }

	using ModelAvailability = std::unordered_map<std::string, bool>;
	template <class Probe>
	inline std::optional<std::string> FirstUnavailableModel(const Route& route, ModelAvailability& cache, Probe&& probe)
	{
		auto available = [&](const std::string& model) {
			const auto key = NormalizePath(model);
			if (const auto found = cache.find(key); found != cache.end()) { return found->second; }
			const bool exists = probe(model);
			cache.emplace(key, exists);
			return exists;
		};
		if (!available(DisabledModel(route))) { return DisabledModel(route); }
		for (const auto& model : route.models) { if (!available(model)) { return model; } }
		return {};
	}

	inline bool KnownModel(const Route& route, std::string_view current)
	{
		const auto normalized = NormalizePath(current);
		if (normalized == NormalizePath(route.baseModel) || normalized == NormalizePath(DisabledModel(route)) ||
			(!route.originalModel.empty() && normalized == NormalizePath(route.originalModel))) { return true; }
		return std::any_of(route.models.begin(), route.models.end(), [&](const auto& model) { return normalized == NormalizePath(model); });
	}

	struct Ownership
	{
		std::string lastModel;
		float lastDuration = 0.0f;
		bool conflicted = false;
	};
	struct Decision
	{
		bool conflict = false;
		bool write = false;
		std::string model;
		float duration = 0.0f;
	};



	inline Decision Plan(const Route& route, const Ownership& ownership, std::string_view currentModel,
		float currentDuration, const RedsandSettings::Config& config)
	{
		if (NormalizePath(route.plugin) != "skyrim.esm") { return {}; }
		if (ownership.conflicted || NormalizePath(currentModel) != NormalizePath(ownership.lastModel) ||
			(route.kind == Kind::Impact && currentDuration != ownership.lastDuration)) { return { true }; }
		const bool enabled = Enabled(route.category, config);
		Decision result;
		result.model = enabled ? route.models[ModelIndex(config)] : DisabledModel(route);
		result.duration = enabled ? route.duration : route.originalDuration;
		result.write = NormalizePath(currentModel) != NormalizePath(result.model) ||
			(route.kind == Kind::Impact && currentDuration != result.duration);
		return result;
	}
}
