#pragma once

#include "RedsandImpactRouting.h"
#include <toml++/toml.h>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <vector>

namespace RedsandImpactVFX::Manifest
{
	inline void Require(bool condition, const char* message)
	{
		if (!condition) { throw std::runtime_error(message); }
	}

	inline std::string String(const toml::table& table, std::string_view key, bool required = true)
	{
		auto value = table[key].value<std::string>();
		Require(value.has_value() || !required, "Missing/string field in impact route");
		return value.value_or("");
	}

	inline std::vector<Route> ReadRoutes(const std::filesystem::path& path)
	{
		Require(std::filesystem::file_size(path) <= 2 * 1024 * 1024, "Impact route file exceeds limit");
		const auto document = toml::parse_file(path.string());
		Require(document["schemaVersion"].value<int64_t>().value_or(0) == 1, "Unsupported impact route schema");
		const auto* array = document["Route"].as_array();
		Require(array && array->size() <= 512, "Invalid impact route count");
		const auto* native = document["Native"].as_array();
		Require(!array->empty() || (native && !native->empty()), "Manifest has no impact routes or native effects");
		std::vector<Route> definitions;
		std::set<std::pair<std::string, std::uint32_t>> identities;
		for (const auto& item : *array) {
			const auto* table = item.as_table();
			Require(table != nullptr, "Impact route must be a table");
			Route route;
			route.plugin = String(*table, "plugin");
			if (NormalizePath(route.plugin) != "skyrim.esm") { continue; }
			const auto id = (*table)["form"].value<int64_t>();
			Require(id && *id > 0 && *id <= 0xFFFFFF, "Invalid local FormID in impact route");
			route.form = static_cast<std::uint32_t>(*id);
			const auto type = String(*table, "type");
			Require(type == "IPCT" || type == "EXPL" || type == "PROJ", "Unsupported impact route form type");
			route.kind = type == "IPCT" ? Kind::Impact : type == "EXPL" ? Kind::Explosion : Kind::Projectile;
			const auto category = String(*table, "category");
			Require(category == "Hit" || category == "Slash" || category == "Block", "Unsupported impact route category");
			route.category = category == "Hit" ? Category::Hit : category == "Slash" ? Category::Slash : Category::Block;
			route.originalModel = String(*table, "originalModel");
			route.baseModel = String(*table, "baseModel");
			route.offModel = String(*table, "offModel", false);
			const auto* models = (*table)["models"].as_array();
			Require(models && models->size() == route.models.size(), "Impact route requires 64 models");
			for (std::size_t index = 0; index < route.models.size(); ++index) {
				const auto value = (*models)[index].value<std::string>();
				Require(value.has_value(), "Non-string model in impact route");
				route.models[index] = *value;
			}
			if (route.kind == Kind::Impact) {
				const auto original = (*table)["originalDuration"].value<double>();
				const auto duration = (*table)["duration"].value<double>();
				Require(original && duration, "IPCT route requires visual durations");
				route.originalDuration = static_cast<float>(*original);
				route.duration = static_cast<float>(*duration);
			} else {
				Require(!table->contains("originalDuration") && !table->contains("duration"), "Only IPCT routes may own visual duration");
			}
			Require(ValidRoute(route), "Invalid model path/duration in impact route");
			Require(identities.emplace(NormalizePath(route.plugin), route.form).second, "Duplicate impact route FormID");
			definitions.push_back(std::move(route));
		}
		return definitions;
	}

	inline std::vector<NativeRoute> ReadNativeRoutes(const std::filesystem::path& path)
	{
		Require(std::filesystem::file_size(path) <= 2 * 1024 * 1024, "Impact route file exceeds limit");
		const auto document = toml::parse_file(path.string());
		Require(document["schemaVersion"].value<int64_t>().value_or(0) == 1, "Unsupported impact route schema");
		const auto* array = document["Native"].as_array();
		if (!document.contains("Native")) { return {}; }
		Require(array != nullptr, "Native impact layers must be an array of tables");
		Require(!array->empty() && array->size() <= nativeLayerNames.size(), "Invalid native impact layer count");
		std::vector<NativeRoute> definitions;
		std::set<NativeLayer> identities;
		for (const auto& item : *array) {
			const auto* table = item.as_table();
			Require(table != nullptr, "Native impact layer must be a table");
			Require(!table->contains("plugin") && !table->contains("form"), "Native impact layers must not reference plugin forms");
			const auto id = String(*table, "id");
			const auto found = std::find(nativeLayerNames.begin(), nativeLayerNames.end(), id);
			Require(found != nativeLayerNames.end(), "Unknown native impact layer");
			NativeRoute route;
			route.layer = static_cast<NativeLayer>(std::distance(nativeLayerNames.begin(), found));
			route.model = String(*table, "model");
			const auto duration = (*table)["duration"].value<double>();
			Require(duration.has_value(), "Native impact layer requires a duration");
			route.duration = static_cast<float>(*duration);
			Require(ValidNativeRoute(route), "Invalid native impact model/duration");
			Require(identities.emplace(route.layer).second, "Duplicate native impact layer");
			definitions.push_back(std::move(route));
		}
		return definitions;
	}
}
