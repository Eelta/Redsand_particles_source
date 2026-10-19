#include "PCH.h"
#include "RedsandAppearancePolicy.h"
#include "RedsandLog.h"
#include "RedsandSlashRandom.h"
#include "RedsandImpactVFX.h"
#include "RedsandMaterialColor.h"
#include "RedsandUpdateScope.h"
#include <RE/N/NiFloatExtraData.h>
#include <RE/N/NiFloatInterpolator.h>
#include <RE/N/NiFloatData.h>
#include <RE/N/NiInterpController.h>
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <mutex>

namespace RedsandAppearance
{
	namespace
	{
		using UpdateFunction = void (*)(RE::NiNode*, RE::NiUpdateData&, std::uint32_t);
		REL::Relocation<UpdateFunction> downward, selected, rigid;
		using FloatUpdate = bool (*)(RE::NiFloatInterpolator*, float, RE::NiObjectNET*, float&);
		REL::Relocation<FloatUpdate> floatUpdate;
		using RenderFunction = RE::BSShaderProperty::RenderPassArray* (*)(RE::BSEffectShaderProperty*, RE::BSGeometry*, std::uint32_t, RE::BSShaderAccumulator*);
		REL::Relocation<RenderFunction> renderPasses;
		const RE::BSFixedString& RateName()
		{
			static const RE::BSFixedString name{ "RSAttackRate_v1" };
			return name;
		}

		bool UpdateFloat(RE::NiFloatInterpolator* self, float time, RE::NiObjectNET* target, float& value)
		{
			const bool result = floatUpdate(self, time, target, value);
			const auto* data = self->floatData.get();
			if (result && data && data->keys && data->numKeys >= 5 && data->numKeys <= 128 && data->keySize == sizeof(RE::NiFloatKey) && data->type == RE::NiFloatKey::KeyType::kLink) {
				auto* k0 = data->keys;
				auto* k1 = k0->GetKeyAt(1, data->keySize);
				auto* k2 = k0->GetKeyAt(2, data->keySize);
				if (OwnedSlashRateCurve(k0->GetTime(), k0->GetValue(), k1->GetTime(), k1->GetValue(), k2->GetTime(), k2->GetValue())) {
					const auto* rate = target ? netimmerse_cast<RE::NiFloatExtraData*>(target->GetExtraData(RateName())) : nullptr;
					value *= rate ? rate->value : SlashDensity(RedsandSlashRandom::appearance ? *RedsandSlashRandom::appearance : RedsandSettings::Get());
				} else if (OwnedRateCurve(k0->GetTime(), k0->GetValue(), k1->GetTime(), k1->GetValue(), k2->GetTime(), k2->GetValue())) {
					const auto* rate = target ? netimmerse_cast<RE::NiFloatExtraData*>(target->GetExtraData(RateName())) : nullptr;
					const auto config = RedsandSlashRandom::appearance ? *RedsandSlashRandom::appearance : RedsandSettings::Get();
					value *= rate ? rate->value : config.impactDensity * RedsandSettings::impactDensityBaseline;
				}
			}
			return result;
		}

		bool Owned(RE::NiNode* root)
		{
			const auto* name = root ? root->name.c_str() : nullptr;
			return name && std::strcmp(name, rootTag.data()) == 0;
		}

		void StartBlockSequence(RE::NiNode* root)
		{
			static const RE::BSFixedString name{ "RSBlockSequence_v1" };
			auto* marker = netimmerse_cast<RE::NiFloatExtraData*>(root->GetExtraData(name));
			if (!marker || marker->value != 0.f) { return; }
			auto* manager = root->GetController<RE::NiControllerManager>();
			if (!manager) { return; }
			StartBlockSequenceOnce(marker->value, manager->GetSequenceByName("SpecialIdle_AreaEffect"), [](RE::NiControllerSequence* sequence) {
				return sequence->Activate(0, true, 1.f, 0.f, nullptr, false);
			});
		}

#include "SlashEmitterControls.inc"

		RedsandSettings::Config ForRoot(RE::NiNode* root)
		{
			const auto base = RedsandSettings::Get();
			if (!RedsandSlashRandom::Enabled(base)) { return base; }
			static const RE::BSFixedString seedName{ "RSRandomSlashSeed_v1" };
			RedsandImpactVFX::BindRandomSlashRoot(root);
			auto* seed = netimmerse_cast<RE::NiFloatExtraData*>(root->GetExtraData(seedName));
			if (!seed) {



				static const RE::BSFixedString cutName{ lengthTag.data() };
				if (!root->GetObjectByName(cutName)) { return base; }
				const auto value = RedsandSlashRandom::attackSeed ? RedsandSlashRandom::attackSeed : RedsandSlashRandom::NextSeed();
				RE::NiPointer<RE::NiFloatExtraData> extra{ RE::NiFloatExtraData::Create(seedName, static_cast<float>(value)) };
				if (!extra || !root->AddExtraData(extra.get())) { return base; }
				seed = extra.get();
			}
			const auto value = RedsandSlashRandom::ReadSceneSeed(seed->value);
			return value ? RedsandSlashRandom::Sample(base, value->value, value->contact) : base;
		}

		void Tint(RE::BSGeometry* geometry, const RedsandSettings::Config& config, bool prepareRates = true);
		void Scale(RE::NiNode* root, const RedsandSettings::Config& config)
		{
			if (!Owned(root)) { return; }
			static const RE::BSFixedString name{ "RSScale_v2" };
			auto* marker = netimmerse_cast<RE::NiFloatExtraData*>(root->GetExtraData(name));
			if (!marker) {
				RE::NiPointer<RE::NiFloatExtraData> extra{ RE::NiFloatExtraData::Create(name, root->local.scale) };
				if (!extra || !root->AddExtraData(extra.get())) { return; }
				marker = extra.get();
			}
			root->local.scale = marker->value * config.impactSize;
			std::array<RE::NiAVObject*, 128> pending{}, visited{};
			unsigned count = 1, done = 0;
			pending[0] = root;
			while (count && done < visited.size()) {
				auto* object = pending[--count];
				if (!object || std::find(visited.begin(), visited.begin() + done, object) != visited.begin() + done) { continue; }
				visited[done++] = object;
				if (auto* node = object->AsNode()) {
					for (const auto& child : node->children) { if (child && count < pending.size()) { pending[count++] = child.get(); } }
				}
				if (auto* geometry = netimmerse_cast<RE::BSGeometry*>(object)) {
					UpdateSlashSpeed(geometry, config);
					Tint(geometry, config);
				}
			}
		}

		void Tint(RE::BSGeometry* geometry, const RedsandSettings::Config& config, bool prepareRates)
		{
			auto* shader = netimmerse_cast<RE::BSEffectShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
			if (!shader) { return; }
			const auto* shaderName = shader->name.c_str();
			if (!shaderName) { return; }
			const auto group = Group(shaderName);
			if (!group) { return; }


			auto* rate = prepareRates ? netimmerse_cast<RE::NiFloatExtraData*>(geometry->GetExtraData(RateName())) : nullptr;
			if (prepareRates && (rate || config.slashRandomValues)) {
				const auto factor = *group == RedsandSettings::ColorTarget::SlashSparks ? SlashDensity(config) : config.impactDensity * RedsandSettings::impactDensityBaseline;
				if (rate) { rate->value = factor; }
				else {
					RE::NiPointer<RE::NiFloatExtraData> extra{ RE::NiFloatExtraData::Create(RateName(), factor) };
					if (extra) { geometry->AddExtraData(extra.get()); }
				}
			}
			auto* material = shader->GetMaterial();
			if (!material) { return; }
			static const RE::BSFixedString markerName{ "RSRGBPeak_v1" };
			auto* marker = netimmerse_cast<RE::NiFloatExtraData*>(geometry->GetExtraData(markerName));
			if (!marker) {
				const auto peak = std::max({ material->baseColor.red, material->baseColor.green, material->baseColor.blue, 0.f });
				RE::NiPointer<RE::NiFloatExtraData> owned{ RE::NiFloatExtraData::Create(markerName, -std::max(peak, .0001f)) };
				if (!owned || !geometry->AddExtraData(owned.get())) { return; }
				marker = owned.get();
			}
			const auto& rgb = Color(config, *group);
			const float peak = std::abs(marker->value);
			std::optional<float> wantedGlow;
			static const RE::BSFixedString glowName{ "RSGlow_v2" }, animatedName{ "RSAnimatedGlow_v2" }, originalGlowName{ "RSOriginalGlow_v1" };
			if (!shader->GetExtraData(animatedName)) {
				auto* glow = netimmerse_cast<RE::NiFloatExtraData*>(geometry->GetExtraData(glowName));
				if (!glow) {
					RE::NiPointer<RE::NiFloatExtraData> extra{ RE::NiFloatExtraData::Create(glowName, material->baseColorScale) };
					if (extra && geometry->AddExtraData(extra.get())) { glow = extra.get(); }
				}
				if (glow) { wantedGlow = GlowStrength(glow->value, shader->GetExtraData(originalGlowName) != nullptr, config); }
			}
			if (!RedsandMaterialColor::Commit(shader, material, { rgb[0] * peak, rgb[1] * peak, rgb[2] * peak },
				wantedGlow, marker->value < 0.f, [](auto* copy) { copy->~BSEffectShaderMaterial(); RE::free(copy); })) { return; }
			marker->value = peak;
			static std::array<std::atomic<unsigned>, 7> appliedLogs{};
			auto& groupLogs = appliedLogs[static_cast<unsigned>(*group)];
			if (groupLogs.load(std::memory_order_relaxed) < 6 && groupLogs.fetch_add(1, std::memory_order_relaxed) < 6) {
				logger::info("Redsand rendered owned VFX: node={} group={} rgb={}/{}/{} glow={} alpha={} scale={}",
					geometry->name.c_str(), static_cast<unsigned>(*group), material->baseColor.red, material->baseColor.green,
					material->baseColor.blue, material->baseColorScale, material->baseColor.alpha, geometry->world.scale);
			}
		}

		RE::BSShaderProperty::RenderPassArray* RenderPasses(RE::BSEffectShaderProperty* shader, RE::BSGeometry* geometry,
			std::uint32_t mode, RE::BSShaderAccumulator* accumulator)
		{



			if (geometry && shader->name == "RSRGBv1_2"sv && geometry->GetGeometryRuntimeData().shaderProperty.get() == shader) {
				const auto base = RedsandSettings::Get();
				if (base.slashRandomColor) {
					auto* parent = geometry->parent;
					for (unsigned depth = 0; parent && depth < 32; ++depth, parent = parent->parent) {
						if (!Owned(parent)) { continue; }
						RedsandImpactVFX::BindRandomSlashRoot(parent);
						static const RE::BSFixedString seedName{ "RSRandomSlashSeed_v1" };
						const auto* extra = netimmerse_cast<RE::NiFloatExtraData*>(parent->GetExtraData(seedName));
						const auto seed = extra ? RedsandSlashRandom::ReadSceneSeed(extra->value) : std::nullopt;
						if (seed && seed->contact) {
							Tint(geometry, RedsandSlashRandom::Sample(base, seed->value, true), false);
							static std::atomic<unsigned> logs{ 0 };
							if (logs.load(std::memory_order_relaxed) < 6 && logs.fetch_add(1, std::memory_order_relaxed) < 6) {
								const auto* material = shader->GetMaterial();
								if (material) { logger::info("Redsand paired-hit draw: seed={} rgb={}/{}/{}", seed->value,
									material->baseColor.red, material->baseColor.green, material->baseColor.blue); }
							}
						}
						break;
					}
				}
			}
			return renderPasses(shader, geometry, mode, accumulator);
		}

		void Apply(RE::NiNode* root, const RedsandSettings::Config& config)
		{
			if (!Owned(root)) { return; }


			std::array<RE::NiAVObject*, 128> pending{}, visited{};
			unsigned count = 1, done = 0;
			pending[0] = root;
			while (count && done < visited.size()) {
				auto* object = pending[--count];
				if (!object || std::find(visited.begin(), visited.begin() + done, object) != visited.begin() + done) { continue; }
				visited[done++] = object;
				if (auto* node = object->AsNode()) {
					const auto* nodeName = node->name.c_str();
					if (nodeName && std::strcmp(nodeName, lengthTag.data()) == 0) {


						node->local.rotate = RE::NiMatrix3{};
						node->local.rotate.entry[0][0] = config.cutLength * RedsandSettings::cutLengthBaseline;
					}
					for (const auto& child : node->children) { if (child && count < pending.size()) { pending[count++] = child.get(); } }
				}
				if (auto* geometry = netimmerse_cast<RE::BSGeometry*>(object)) { Tint(geometry, config); }
			}
		}
		void UpdateOwned(UpdateFunction original, RE::NiNode* root, RE::NiUpdateData& data, std::uint32_t arg)
		{
			if (!Owned(root)) { original(root, data, arg); return; }
            UpdateScope scope(root);
            if (scope.Nested()) { original(root, data, arg); return; }
			StartBlockSequence(root);
			const auto config = ForRoot(root);
			RedsandSlashRandom::AppearanceScope appearance(config);



			Scale(root, config);
			original(root, data, arg);
			Apply(root, config);
		}
		void Downward(RE::NiNode* root, RE::NiUpdateData& data, std::uint32_t arg) { UpdateOwned(downward.get(), root, data, arg); }
		void Selected(RE::NiNode* root, RE::NiUpdateData& data, std::uint32_t arg) { UpdateOwned(selected.get(), root, data, arg); }
		void Rigid(RE::NiNode* root, RE::NiUpdateData& data, std::uint32_t arg) { UpdateOwned(rigid.get(), root, data, arg); }
	}
	void Install()
	{
		static std::once_flag once;
		std::call_once(once, [] {
			REL::Relocation<std::uintptr_t> table{ RE::VTABLE_NiNode[0] };


			downward = table.write_vfunc(0x2C, Downward);
			selected = table.write_vfunc(0x2D, Selected);
			rigid = table.write_vfunc(0x2E, Rigid);
			REL::Relocation<std::uintptr_t> floats{ RE::VTABLE_NiFloatInterpolator[0] };
			floatUpdate = floats.write_vfunc(0x29, UpdateFloat);
			REL::Relocation<std::uintptr_t> effects{ RE::VTABLE_BSEffectShaderProperty[0] };
			renderPasses = effects.write_vfunc(0x2A, RenderPasses);
			logger::info("Redsand continuous VFX controls installed: brightness=0..20 density=0..10 size=.05..10; tagged curve outputs only");
		});
	}
}
