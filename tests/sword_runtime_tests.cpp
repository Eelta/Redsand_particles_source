#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>
#include "../source/RedsandSettings.h"
#include "../source/RedsandMeleeStyle.h"
#include "../source/RedsandEmissionTuning.h"

namespace RedsandSettings
{
	Config scopeConfig;
	Config Get() { return scopeConfig; }
}

namespace Settings { std::string attackTrailMeshPath; }
namespace RedsandLog
{
	unsigned informationLogs = 0;
	template <class... Args> void info(const char*, Args&&...) { ++informationLogs; }
}

namespace RE
{
	struct NiPoint3
	{
		float x = 0, y = 0, z = 0;
		NiPoint3 operator+(NiPoint3 point) const { return { x + point.x, y + point.y, z + point.z }; }
		NiPoint3 operator-(NiPoint3 point) const { return { x - point.x, y - point.y, z - point.z }; }
		NiPoint3 operator-() const { return { -x, -y, -z }; }
		NiPoint3 operator*(float factor) const { return { x * factor, y * factor, z * factor }; }
		NiPoint3 Cross(NiPoint3 point) const { return { y * point.z - z * point.y, z * point.x - x * point.z, x * point.y - y * point.x }; }
		float Length() const { return std::sqrt(x * x + y * y + z * z); }
		void Unitize() { const float length = Length(); x /= length; y /= length; z /= length; }
	};

	struct NiMatrix3
	{
		float entry[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		NiMatrix3() = default;

		NiMatrix3(float x, float y, float z)
		{
			const float sx = std::sin(x), sy = std::sin(y), sz = std::sin(z);
			const float cx = std::cos(x), cy = std::cos(y), cz = std::cos(z);
			entry[0][0] = cy * cz; entry[0][1] = cy * sz; entry[0][2] = -sy;
			entry[1][0] = sx * sy * cz - cx * sz; entry[1][1] = sx * sy * sz + cx * cz; entry[1][2] = sx * cy;
			entry[2][0] = cx * sy * cz + sx * sz; entry[2][1] = cx * sy * sz - sx * cz; entry[2][2] = cx * cy;
		}
		NiPoint3 operator*(NiPoint3 point) const
		{
			return {
				entry[0][0] * point.x + entry[0][1] * point.y + entry[0][2] * point.z,
				entry[1][0] * point.x + entry[1][1] * point.y + entry[1][2] * point.z,
				entry[2][0] * point.x + entry[2][1] * point.y + entry[2][2] * point.z
			};
		}
		NiMatrix3 operator*(const NiMatrix3& other) const
		{
			NiMatrix3 result;
			for (std::size_t row = 0; row < 3; ++row) {
				for (std::size_t column = 0; column < 3; ++column) {
					result.entry[row][column] = 0;
					for (std::size_t inner = 0; inner < 3; ++inner) {
						result.entry[row][column] += entry[row][inner] * other.entry[inner][column];
					}
				}
			}
			return result;
		}
		NiMatrix3 Transpose() const
		{
			NiMatrix3 result;
			for (std::size_t row = 0; row < 3; ++row) {
				for (std::size_t column = 0; column < 3; ++column) {
					result.entry[row][column] = entry[column][row];
				}
			}
			return result;
		}
	};

	struct NiTransform
	{
		NiPoint3 translate;
		float scale = 1;
		NiMatrix3 rotate;
		NiPoint3 operator*(NiPoint3 point) const { return translate + (rotate * point) * scale; }
		NiTransform operator*(const NiTransform& other) const
		{
			return { *this * other.translate, scale * other.scale, rotate * other.rotate };
		}
		NiTransform Invert() const
		{
			const auto inverseRotation = rotate.Transpose();
			return { (inverseRotation * -translate) * (1.f / scale), 1.f / scale, inverseRotation };
		}
	};
	struct BSGeometry;
	struct NiNode;
	struct NiAVObject
	{
		virtual ~NiAVObject() = default;
		virtual BSGeometry* AsGeometry() { return nullptr; }
		virtual NiNode* AsNode() { return nullptr; }
		std::string_view name;
		NiNode* parent = nullptr;
		NiTransform local, world;
	};
	struct NiNode : NiAVObject
	{
		std::vector<NiAVObject*> children;
		NiNode* AsNode() override { return this; }
	};
	struct BSGeometry : NiAVObject { BSGeometry* AsGeometry() override { return this; } };
	struct NiTriShape : NiAVObject {};
	template <class T> struct NiPointer
	{
		T* value = nullptr;
		T* get() const { return value; }
		T* operator->() const { return value; }
		explicit operator bool() const { return value != nullptr; }
	};
	enum class WEAPON_TYPE { kOneHandSword = 1, kStaff = 8 };
	class TESObjectWEAP
	{
	public:
		WEAPON_TYPE type = WEAPON_TYPE::kOneHandSword;
		WEAPON_TYPE GetWeaponType() const { return type; }
		std::uint32_t GetFormID() const { return 0x123; }
		template <class T> T* As() { return static_cast<T*>(this); }
	};
	struct InventoryEntryData { TESObjectWEAP* object = nullptr; };
	struct NiColorA { float red, green, blue, alpha; };
	class Actor
	{
	public:
		bool player = false;
		bool IsPlayerRef() const { return player; }
		std::uint32_t GetFormID() const { return player ? 0x14 : 0x456; }
	};
	namespace BSVisit
	{
		enum class BSVisitControl { kContinue, kStop };
		template <class Visitor>
		BSVisitControl TraverseScenegraphObjects(NiAVObject* object, Visitor visitor)
		{
			if (visitor(object) == BSVisitControl::kStop) { return BSVisitControl::kStop; }
			if (auto* node = object->AsNode()) {
				for (auto* child : node->children) {
					if (TraverseScenegraphObjects(child, visitor) == BSVisitControl::kStop) { return BSVisitControl::kStop; }
				}
			}
			return BSVisitControl::kContinue;
		}
		template <class Visitor>
		BSVisitControl TraverseScenegraphGeometries(NiAVObject* object, Visitor visitor)
		{
			return TraverseScenegraphObjects(object, [&](NiAVObject* candidate) {
				return candidate->AsGeometry() ? visitor(candidate->AsGeometry()) : BSVisitControl::kContinue;
			});
		}
	}
}

namespace Redsand::Weapons
{
	Profile ForWeapon(RE::TESObjectWEAP* weapon)
	{
		return weapon && weapon->GetWeaponType() != RE::WEAPON_TYPE::kStaff ? Profile::Sword : Profile::Default;
	}
}

namespace Utils
{
	void UpdateNodeTransformLocal(RE::NiAVObject* node, const RE::NiTransform& world)
	{
		node->local = node->parent ? node->parent->world.Invert() * world : world;
	}
}

struct AttackTrail
{
	bool redsandManaged = true;
	bool redsandLegacyTrail = false;
	RedsandSettings::Config redsandConfig;
	Redsand::Weapons::Profile redsandWeaponProfile = Redsand::Weapons::Profile::Default;
	struct Emitter { RE::NiPointer<RE::NiAVObject> geometry; RE::NiTransform originalLocal; };
	std::vector<Emitter> redsandEmitterMeshes;
	RE::NiPointer<RE::NiNode> collisionParentNode;
	RE::NiPoint3 redsandPreviousTip;
	bool redsandTipSample = false;
	float scale = 0.8f;
	RE::NiMatrix3 weaponRotation { 0.f, 1.57079632679f, -1.57079632679f };
	void UpdateRedsandEmission();
	void CacheRedsandEmissionMeshes(RE::NiAVObject* root);
};
#include "../source/SwordRuntime.inc"

namespace
{
	constexpr float meshEnd = 10.042167f;
	using Redsand::Weapons::Profile;
	void Check(bool condition, const char* message)
	{
		if (!condition) { throw std::runtime_error(message); }
	}
	bool Same(float first, float second) { return std::abs(first - second) < 0.001f; }
	bool SamePoint(RE::NiPoint3 first, RE::NiPoint3 second)
	{
		return Same(first.x, second.x) && Same(first.y, second.y) && Same(first.z, second.z);
	}
	bool SameTransform(const RE::NiTransform& first, const RE::NiTransform& second)
	{
		if (!SamePoint(first.translate, second.translate) || !Same(first.scale, second.scale)) { return false; }
		for (std::size_t row = 0; row < 3; ++row) {
			for (std::size_t column = 0; column < 3; ++column) {
				if (!Same(first.rotate.entry[row][column], second.rotate.entry[row][column])) { return false; }
			}
		}
		return true;
	}

	struct Fixture
	{
		RE::NiNode root, weapon, bone, emitterParent;
		RE::NiTriShape firstEmitter;
		RE::BSGeometry secondEmitter, unrelated;
		AttackTrail trail;
		Fixture()
		{
			trail.redsandConfig.swordSpread = 1.f;
			root.children = { &bone, &unrelated };
			bone.parent = &root;
			bone.children = { &emitterParent };
			emitterParent.parent = &bone;
			emitterParent.children = { &firstEmitter, &secondEmitter };
			emitterParent.local.scale = 10.f;
			firstEmitter.parent = secondEmitter.parent = &emitterParent;
			firstEmitter.name = "Trail Mesh";
			secondEmitter.name = "Trailmesh2";
			unrelated.name = "RedsandParticles_Drain_v1";
			firstEmitter.local.translate = { 5, 0, 0 };
			trail.CacheRedsandEmissionMeshes(&root);
			Check(trail.redsandEmitterMeshes.size() == 2, "production discovery must cache both legacy NiTriShape and converted BSGeometry sources");
			Check(trail.redsandEmitterMeshes.front().geometry.get() == &firstEmitter, "native source named Trail Mesh was not cached");
			trail.collisionParentNode.value = &weapon;
			unrelated.world = { { 40, 50, 60 }, 1, RE::NiMatrix3(0.2f, 0.3f, 0.4f) };
			SetPose({ 100, 200, 300 }, RE::NiMatrix3(0.35f, -0.4f, 0.9f), 1.5f, 80.f);
		}
		void SetPose(RE::NiPoint3 position, RE::NiMatrix3 rotation, float actorScale, float reach)
		{
			weapon.world = { position, actorScale, rotation };
			trail.scale = reach * 0.01f;

			const auto collisionRotation = rotation * trail.weaponRotation * RE::NiMatrix3(0.f, 0.f, 0.4f);
			const auto direction = collisionRotation * RE::NiPoint3{ 1, 0, 0 };
			const float cb = std::sqrt(1.f - direction.z * direction.z);
			const float ca = direction.y / cb, sa = -direction.x / cb;
			RE::NiMatrix3 facing;
			facing.entry[0][0] = ca; facing.entry[0][1] = direction.x; facing.entry[0][2] = sa * direction.z;
			facing.entry[1][0] = sa; facing.entry[1][1] = direction.y; facing.entry[1][2] = -ca * direction.z;
			facing.entry[2][0] = 0; facing.entry[2][1] = direction.z; facing.entry[2][2] = cb;
			bone.world = { position + RE::NiPoint3{ 3, -5, 7 }, trail.scale, facing * trail.weaponRotation };
			emitterParent.world = { { 999, 999, 999 }, 3, RE::NiMatrix3(-0.2f, 0.1f, 0.6f) };
		}
		RE::NiPoint3 BladePoint(float fraction) const
		{
			return weapon.world.translate + weapon.world.rotate * RE::NiPoint3{ 0, trail.scale * 100.f * fraction, 0 };
		}
		void CheckPropagation() const
		{
			Check(SameTransform(emitterParent.world, bone.world * emitterParent.local), "intermediate emitter parent kept the previous-frame pose");
			for (const auto& emitter : trail.redsandEmitterMeshes) {
				Check(SameTransform(emitter.geometry->world, emitterParent.world * emitter.geometry->local),
					"scene propagation must preserve emitter position, scale and rotation");
			}
		}
		void CheckOriginal() const
		{
			for (const auto& emitter : trail.redsandEmitterMeshes) {
				Check(SameTransform(emitter.geometry->local, emitter.originalLocal), "default body mode changed an authored emitter transform");
				Check(SameTransform(emitter.geometry->world, bone.world * emitterParent.local * emitter.originalLocal), "default body mode must restore the original emitter pose without drift");
			}
			CheckPropagation();
		}
		void CheckTipPoint() const
		{
			for (const auto& emitter : trail.redsandEmitterMeshes) {
				Check(SamePoint(emitter.geometry->world.translate, BladePoint(1.f)), "tip must follow measured reach without applying scale twice");
				Check(emitter.geometry->world.scale < 0.02f, "tip must collapse the emitter extent");
			}
			CheckPropagation();
		}
		void CheckTipSweep(RE::NiPoint3 previous, RE::NiPoint3 current) const
		{
			for (const auto& emitter : trail.redsandEmitterMeshes) {
				const auto& world = emitter.geometry->world;
				Check(SamePoint(world * RE::NiPoint3{ 0, 0, 0 }, previous), "tip sweep must start at the previous tip, not the weapon body");
				Check(SamePoint(world * RE::NiPoint3{ meshEnd, 0, 0 }, current), "tip sweep must end at the current tip");
				Check(SamePoint(world * RE::NiPoint3{ meshEnd * 0.5f, 0, 0 }, (previous + current) * 0.5f), "tip sweep must fill the sword-shadow path between samples");
				Check(Same(world.scale * meshEnd, (current - previous).Length()), "tip sweep length must be the tip displacement");
				Check(Same((world.rotate * RE::NiPoint3{ 0, 1, 0 }).Length(), 1.f), "tip sweep side axis must remain normalized");
				Check(Same((world.rotate * RE::NiPoint3{ 0, 0, 1 }).Length(), 1.f), "tip sweep up axis must remain normalized");
			}
			CheckPropagation();
		}
		void CheckSpan(float start, float end) const
		{
			for (const auto& emitter : trail.redsandEmitterMeshes) {
				const auto& world = emitter.geometry->world;
				Check(SamePoint(world * RE::NiPoint3{ 0, 0, 0 }, BladePoint(start)), "weapon emission span begins outside the blade/head region");
				Check(SamePoint(world * RE::NiPoint3{ meshEnd, 0, 0 }, BladePoint(end)), "weapon emission endpoint points away from the blade");
				Check(SamePoint(world.rotate * RE::NiPoint3{ 1, 0, 0 }, weapon.world.rotate * RE::NiPoint3{ 0, 1, 0 }), "emitter +X must point along weapon parent +Y");
				Check(Same(world.scale, trail.scale * 100.f * (end - start) / meshEnd), "weapon emission extent double-applied actor scale");
			}
			CheckPropagation();
		}
	};

	void DiscoverySupportsBothEngineGeometryFamilies()
	{
		Fixture fixture;
		Check(fixture.firstEmitter.AsGeometry() == nullptr, "legacy NiTriShape must not be modeled as BSGeometry");
		std::size_t geometryOnlySources = 0;
		RE::BSVisit::TraverseScenegraphGeometries(&fixture.root, [&](RE::BSGeometry* object) {
			if (object->name == "Trail Mesh" || object->name == "Trailmesh2") { ++geometryOnlySources; }
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		Check(geometryOnlySources == 1, "regression fixture must expose the geometric visitor's legacy-node omission");
		fixture.trail.redsandTipSample = true;
		fixture.trail.CacheRedsandEmissionMeshes(&fixture.root);
		Check(fixture.trail.redsandEmitterMeshes.size() == 2, "recaching may not duplicate emitter sources");
		Check(!fixture.trail.redsandTipSample, "recaching a new particle object must reset the previous tip sample");
		fixture.trail.redsandLegacyTrail = true;
		fixture.trail.CacheRedsandEmissionMeshes(&fixture.root);
		Check(fixture.trail.redsandEmitterMeshes.empty(), "Precision legacy trail must never cache particle emission meshes");
		fixture.trail.redsandLegacyTrail = false;
		fixture.trail.CacheRedsandEmissionMeshes(nullptr);
		Check(fixture.trail.redsandEmitterMeshes.empty(), "missing particle object must leave no dangling emitter source");
	}

	void OriginalBodyAndTipSwitchingPreserveSceneTransforms()
	{
		Fixture fixture;
		Check(fixture.trail.redsandConfig.swordTip, "new trails must default to sword-tip attachment");
		fixture.trail.redsandConfig.swordTip = false;
		const auto unrelatedBefore = fixture.unrelated.world;
		fixture.trail.UpdateRedsandEmission(); fixture.CheckOriginal();
		fixture.trail.redsandConfig.swordTip = true;
		fixture.trail.UpdateRedsandEmission(); fixture.CheckTipPoint();
		const auto previousTip = fixture.BladePoint(1.f);
		fixture.SetPose({ -80, 27, 430 }, RE::NiMatrix3(-0.6f, 0.2f, -1.2f), 2.2f, 125.f);
		fixture.trail.UpdateRedsandEmission(); fixture.CheckTipSweep(previousTip, fixture.BladePoint(1.f));
		fixture.trail.redsandConfig.swordTip = false;
		fixture.trail.UpdateRedsandEmission(); fixture.CheckOriginal();
		Check(SameTransform(fixture.unrelated.world, unrelatedBefore), "live world particles must not follow emitter changes");
	}
	void TipSweepUsesConsecutiveTipsAndResetsDiscontinuities()
	{
		Fixture fixture;
		fixture.trail.redsandConfig.swordTip = true;
		fixture.trail.UpdateRedsandEmission(); fixture.CheckTipPoint();
		auto previousTip = fixture.BladePoint(1.f);
		fixture.SetPose({ 103, 195, 307 }, RE::NiMatrix3(0.7f, -0.2f, 1.3f), 1.5f, 80.f);
		fixture.trail.UpdateRedsandEmission(); fixture.CheckTipSweep(previousTip, fixture.BladePoint(1.f));
		previousTip = fixture.BladePoint(1.f);
		fixture.SetPose({ 110, 192, 312 }, RE::NiMatrix3(0.8f, 0.1f, 1.7f), 1.5f, 80.f);
		fixture.trail.UpdateRedsandEmission(); fixture.CheckTipSweep(previousTip, fixture.BladePoint(1.f));
		fixture.trail.UpdateRedsandEmission(); fixture.CheckTipPoint();
		previousTip = fixture.BladePoint(1.f);
		fixture.SetPose(fixture.weapon.world.translate + RE::NiPoint3{ 0, 0, 20 }, fixture.weapon.world.rotate, 1.5f, 80.f);
		fixture.trail.UpdateRedsandEmission(); fixture.CheckTipSweep(previousTip, fixture.BladePoint(1.f));
		fixture.SetPose({ 1000, 2000, 3000 }, RE::NiMatrix3(0.35f, -0.4f, 0.9f), 1.5f, 80.f);
		fixture.trail.UpdateRedsandEmission(); fixture.CheckTipPoint();
		fixture.trail.redsandConfig.swordTip = false;
		fixture.trail.UpdateRedsandEmission(); fixture.CheckOriginal();
		Check(!fixture.trail.redsandTipSample, "body mode must discard old sword-tip history");
		fixture.SetPose({ 1005, 2010, 3020 }, RE::NiMatrix3(-0.4f, 0.1f, -0.9f), 1.5f, 80.f);
		fixture.trail.redsandConfig.swordTip = true;
		fixture.trail.UpdateRedsandEmission(); fixture.CheckTipPoint();
	}
	void CategoryEndpointsFollowTheWeaponInBothPoses()
	{
		struct ExpectedSpan { Profile profile; float start, end; };
		constexpr std::array spans = {
			ExpectedSpan{ Profile::Dagger, 0.12f, 0.94f }, ExpectedSpan{ Profile::Sword, 0.06f, 1.f },
			ExpectedSpan{ Profile::WarAxe, 0.68f, 1.f }, ExpectedSpan{ Profile::Mace, 0.82f, 1.f },
			ExpectedSpan{ Profile::Greatsword, 0.04f, 1.f }, ExpectedSpan{ Profile::Battleaxe, 0.64f, 1.f },
			ExpectedSpan{ Profile::Warhammer, 0.80f, 1.f }
		};
		for (const auto& span : spans) {
			Fixture fixture;
			fixture.trail.redsandConfig.swordTip = false;
			fixture.trail.redsandWeaponProfile = span.profile;
			fixture.trail.UpdateRedsandEmission(); fixture.CheckSpan(span.start, span.end);
			fixture.SetPose({ -80, 27, 430 }, RE::NiMatrix3(-0.6f, 0.2f, -1.2f), 2.2f, 125.f);
			fixture.trail.UpdateRedsandEmission(); fixture.CheckSpan(span.start, span.end);
			fixture.trail.redsandConfig.swordTip = true;
			fixture.trail.UpdateRedsandEmission(); fixture.CheckTipPoint();
			fixture.trail.redsandConfig.swordTip = false;
			fixture.trail.UpdateRedsandEmission(); fixture.CheckSpan(span.start, span.end);
			fixture.trail.redsandWeaponProfile = Profile::Default;
			fixture.trail.UpdateRedsandEmission(); fixture.CheckOriginal();
		}
	}
	void LegacyRibbonsRemainUntouched()
	{
		Fixture fixture;
		fixture.trail.redsandManaged = false;
		fixture.trail.redsandWeaponProfile = Profile::Warhammer;
		fixture.trail.redsandConfig.swordTip = true;
		fixture.firstEmitter.world = { { 33, 44, 55 }, 7, RE::NiMatrix3(0.2f, -0.3f, 0.1f) };
		const auto previousWorld = fixture.firstEmitter.world, previousLocal = fixture.firstEmitter.local;
		fixture.trail.UpdateRedsandEmission();
		Check(SameTransform(fixture.firstEmitter.world, previousWorld) && SameTransform(fixture.firstEmitter.local, previousLocal), "legacy ribbon meshes must remain untouched");
	}
	void LiveSpreadNarrowsWidthWithoutMovingTheSweep()
	{
		Fixture fixture;
		fixture.trail.redsandConfig.swordTip = true;
		fixture.trail.UpdateRedsandEmission();
		const auto start = fixture.BladePoint(1.f);
		fixture.SetPose({ 150, 220, 300 }, RE::NiMatrix3(0.7f, 0.2f, 1.3f), 1.5f, 80.f);
		const auto end = fixture.BladePoint(1.f);
		fixture.trail.UpdateRedsandEmission();
		const auto baseline = fixture.firstEmitter.world;
		for (const float spread : { 0.25f, 2.f, 0.f, 1.f, 0.25f }) {
			fixture.trail.redsandConfig.swordSpread = spread;

			fixture.trail.redsandPreviousTip = start;
			fixture.trail.UpdateRedsandEmission();
			const float width = std::max(spread, 0.001f);
			for (const auto& emitter : fixture.trail.redsandEmitterMeshes) {
				const auto& world = emitter.geometry->world;
				Check(SamePoint(world * RE::NiPoint3{}, start), "spread moved the sweep origin");
				Check(SamePoint(world * RE::NiPoint3{ meshEnd, 0, 0 }, end), "spread shortened or moved the tip endpoint");
				Check(Same((world.rotate * RE::NiPoint3{ 0, 1, 0 }).Length(), width), "source width did not follow live spread");
				Check(Same((world.rotate * RE::NiPoint3{ 0, 0, 1 }).Length(), width), "source depth did not follow live spread");
				Check(Same(world.scale, baseline.scale), "spread may not change sweep length");
			}
			fixture.CheckPropagation();
		}
		fixture.trail.redsandConfig.swordTip = false;
		fixture.trail.redsandWeaponProfile = Profile::WarAxe;
		fixture.trail.UpdateRedsandEmission();
		const auto world = fixture.firstEmitter.world;
		Check(SamePoint(world * RE::NiPoint3{}, fixture.BladePoint(0.68f)), "body spread moved axe-head start");
		Check(SamePoint(world * RE::NiPoint3{ meshEnd, 0, 0 }, fixture.BladePoint(1.f)), "body spread changed axe-head endpoint");
		Check(Same((world.rotate * RE::NiPoint3{ 0, 1, 0 }).Length(), 0.25f), "body mode ignored spread");
		fixture.CheckPropagation();
	}
	void LiveScalarValuesAlwaysUseAuthoredConstants()
	{
		using namespace Redsand::Emission;
		RedsandSettings::Config config;
		for (const float spread : { 0.25f, 2.f, 0.f, 1.f, 0.25f }) {
			config.swordSpread = spread;
			config.swordSize = 0.5f;
			config.swordDensity = 1.5f;
			Check(Same(Value(-13.f, Scalar::Speed, config), -13.f * spread), "speed sign or authored baseline lost");
			Check(Same(Value(9.f, Scalar::Radius, config), 4.5f), "size compounded across live changes");
			Check(Same(Value(1600.f, Scalar::BirthRate, config), 2400.f), "spread or size modified density");
		}
	}
	struct ScopeFixture
	{
		RedsandSettings::Config redsandConfig;
		Profile redsandWeaponProfile = Profile::Default;
		bool redsandLegacyTrail = false, redsandActorIsPlayer = false, redsandScopeAllowed = false;
		std::optional<RE::NiColorA> baseColorOverride;
		std::string Select(const RedsandSettings::Config& config, RE::Actor* actor, RE::TESObjectWEAP* weapon,
			std::string_view requested, std::string_view installedRibbon)
		{
			using namespace std::literals;
			RedsandSettings::scopeConfig = config;
			Settings::attackTrailMeshPath = installedRibbon;
			struct Handle { RE::Actor* value; RE::Actor* get() const { return value; } };
			const Handle a_actorHandle{ actor };
			RE::InventoryEntryData entry{ weapon };
			auto* a_weaponItem = &entry;
			baseColorOverride.reset();
#include "../source/TrailActorStyle.inc"
			std::string trailMeshPath = redsandLegacyTrail ? Settings::attackTrailMeshPath : "Effects/WeaponTrails/ElementalDesert/DefaultYellow.nif";
			if (!requested.empty()) { trailMeshPath = requested; }
#include "../source/TrailDensity.inc"
			return trailMeshPath;
		}
	};

	void ConstructorScopeCannotReenterParticlesThroughMeshOverrides()
	{
		using Redsand::Weapons::IsParticleMesh;
		using Redsand::Weapons::ResolveMeleeMesh;
		constexpr auto own = "Effects/WeaponTrails/ElementalDesert/DefaultYellow.nif";
		constexpr auto customRibbon = "Effects/WeaponTrails/CustomRibbon.nif";
		constexpr auto hardRibbon = "Effects/WeaponTrails/AttackTrail.nif";
		constexpr std::array<std::string_view, 6> ownMeshes{
			own, "effects\\weapontrails\\elementaldesert\\WeaponStyles\\Sword\\Fire.nif",
			"Effects/WeaponTrails/ElementalDesert/Density5/Blue.nif", "Effects/WeaponTrails/ElementalDesert/UnknownColor.nif",
			"MESHES\\EFFECTS\\WeaponTrails\\ElementalDesert\\Red.nif", "Data/Meshes/Effects/WeaponTrails/ElementalDesert/Frost.nif"
		};
		RE::TESObjectWEAP weapon;
		for (const bool player : { false, true }) {
			RE::Actor actor;
			actor.player = player;
			for (const bool enabled : { false, true }) {
				for (const bool players : { false, true }) {
					for (const bool npcs : { false, true }) {
						auto config = RedsandSettings::Config{};
						config.swordEnabled = enabled;
						config.swordPlayer = players;
						config.swordNPC = npcs;
						const bool allowed = enabled && (player ? players : npcs);
						for (const auto requested : ownMeshes) {
							ScopeFixture fixture;
							const auto selected = fixture.Select(config, &actor, &weapon, requested, customRibbon);
							Check(fixture.redsandScopeAllowed == allowed && fixture.redsandLegacyTrail != allowed && fixture.redsandActorIsPlayer == player,
								"constructor did not snapshot the player/NPC scope correctly");
							Check(IsParticleMesh(selected) == allowed, "direct/TOML particle override bypassed constructor scope before Spawn");
							if (!allowed) {
								Check(selected == customRibbon, "excluded scope did not retain the installed custom ribbon");
								Check(fixture.Select(config, &actor, &weapon, requested, own) == hardRibbon, "own installed fallback reintroduced excluded particles");
								Check(fixture.Select(config, &actor, &weapon, requested, "") == hardRibbon, "empty installed fallback did not use the safe ribbon");
							}
						}
						ScopeFixture fixture;
						Check(fixture.Select(config, &actor, &weapon, customRibbon, own) == customRibbon,
							"scope selection changed another mod's explicit ribbon");
						Check(IsParticleMesh(fixture.Select(config, &actor, &weapon, "", own)) == allowed,
							"the configured default particle mesh bypassed scope without a direct override");
					}
				}
			}
			weapon.type = RE::WEAPON_TYPE::kStaff;
			ScopeFixture staff;
			Check(staff.Select({}, &actor, &weapon, own, customRibbon) == customRibbon && !staff.redsandScopeAllowed,
				"staff melee regained particles through a direct override");
			weapon.type = RE::WEAPON_TYPE::kOneHandSword;
		}
		ScopeFixture missingActor;
		Check(missingActor.Select({}, nullptr, &weapon, own, own) == hardRibbon && !missingActor.redsandScopeAllowed,
			"an unresolved actor emitted particles before its scope could be checked");
		constexpr auto unrelated = "Effects/WeaponTrails/ElementalDesertOther/Fire.nif";
		Check(ResolveMeleeMesh(false, unrelated, own) == unrelated, "scope matching consumed another mod's similarly named folder");
		Check(ResolveMeleeMesh(false, own, "mEsHeS\\eFfEcTs\\wEaPoNtRaIlS\\eLeMeNtAlDeSeRt\\Blue.nif") == hardRibbon,
			"installed own fallback did not honor Windows case/slash variants");
		Check(RedsandLog::informationLogs == 0, "disabled diagnostics emitted constructor logs");
		auto diagnostics = RedsandSettings::Config{};
		diagnostics.diagnostics = true;
		RE::Actor npc;
		ScopeFixture traced;
		traced.Select(diagnostics, &npc, &weapon, own, customRibbon);
		Check(RedsandLog::informationLogs == 2, "disabled diagnostics consumed the first profile log or constructor scope did not log");
		traced.Select(diagnostics, &npc, &weapon, own, customRibbon);
		Check(RedsandLog::informationLogs == 3, "scope diagnostics did not log once per construction or repeated the profile report");
	}
}

int main()
{
	try {
		DiscoverySupportsBothEngineGeometryFamilies();
		OriginalBodyAndTipSwitchingPreserveSceneTransforms();
		TipSweepUsesConsecutiveTipsAndResetsDiscontinuities();
		CategoryEndpointsFollowTheWeaponInBothPoses();
		LegacyRibbonsRemainUntouched();
		LiveSpreadNarrowsWidthWithoutMovingTheSweep();
		LiveScalarValuesAlwaysUseAuthoredConstants();
		ConstructorScopeCannotReenterParticlesThroughMeshOverrides();
		std::cout << "PASS: legacy/converted source discovery, consecutive tip sweep, discontinuity resets, category endpoints, scene propagation and legacy isolation\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
