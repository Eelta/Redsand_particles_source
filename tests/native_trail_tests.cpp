#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "../source/RedsandSettings.h"
#include "../source/RedsandMeleeStyle.h"

using namespace std::literals;
namespace RedsandSettings { Config current; Config Get() { return current; } }
namespace Settings { bool bDisplayTrails = true, bTrailUseAttackCollisionLength = false; }

struct Node
{
	bool culled = false, validRoot = true, isNode = true;
	std::vector<int> children{ 1 };
	void SetAppCulled(bool value) { culled = value; }
	Node* GetObjectByName(std::string_view) { return validRoot ? this : nullptr; }
	Node* AsNode() { return isNode ? this : nullptr; }
};
struct Effect { std::shared_ptr<Node> particleObject = std::make_shared<Node>(); float age = 0.f, lifetime = 10.f; };
struct Handle { Node* value; Node* get() const { return value; } };
struct TrailOverride { std::optional<std::string> meshOverride; std::optional<float> lifetimeMult; std::optional<float> baseColorOverride; std::optional<float> baseColorScaleMult; };
bool selectedParticle = true, scopeAllowed = true;

struct AttackTrail
{
	AttackTrail(Node* node, Handle actor, int cell, int* item, bool left, bool length,
		std::optional<TrailOverride> supplied, bool native = false) :
		redsandNativeOnly(native), actorHandle(actor), collisionNode(node), collisionParentNode(node),
		passedCell(cell), passedItem(item), passedLeft(left), passedLength(length), passedOverride(supplied)
	{
		redsandConfig = RedsandSettings::Get();
        redsandReleasedSelected = !native && redsandConfig.swordReleasedTrail;
		redsandParticleSelected = selectedParticle;
		redsandScopeAllowed = scopeAllowed;
	}
	~AttackTrail();
	bool ShouldAddPrecisionTrail() const;
	void RetireRedsandNativeTrail();
	bool Update(float a_deltaTime)
	{
#include "../source/NativeTrailUpdate.inc"
		++visualUpdates;
		return bActive;
	}
	bool redsandReleasedSelected = false;
    bool redsandNativeOnly, redsandParticleSelected = false, redsandScopeAllowed = false, bActive = true;
	float redsandNativePendingTime = 0.f;
	RedsandSettings::Config redsandConfig;
    std::string redsandEnchantmentKey = "none";
	Handle actorHandle;
	Node* collisionNode;
	Node* collisionParentNode;
	std::shared_ptr<Effect> trailParticle = std::make_shared<Effect>();
	int passedCell;
	int* passedItem;
	bool passedLeft, passedLength;
	std::optional<TrailOverride> passedOverride;
	unsigned visualUpdates = 0;
};
#include "../source/NativeTrailLifecycle.inc"

struct PrecisionHandler
{
	std::vector<std::shared_ptr<AttackTrail>> _attackTrails;
	static PrecisionHandler* GetSingleton() { static PrecisionHandler handler; return &handler; }
};

void Check(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }

void Register(const std::shared_ptr<Node>& attackCollisionNode, Handle actorHandle, int cell, int* weaponItem, bool bIsLeftHand)
{
	struct Definition { bool bTrailUseTrueLength = true; std::optional<TrailOverride> trailOverride; } a_collisionDefinition;
	a_collisionDefinition.trailOverride = TrailOverride{ "RedsandParticles/Sword.nif", 1.5f };
#include "../source/NativeTrailRegistration.inc"
}

void RegistrationKeepsTwoVisualsWithoutRecursion()
{
	auto& trails = PrecisionHandler::GetSingleton()->_attackTrails;
	auto node = std::make_shared<Node>();
	int item = 9;
	for (const bool keep : { false, true }) {
		for (const bool selected : { false, true }) {
			for (const bool allowed : { false, true }) {
				trails.clear();
				RedsandSettings::current = {};
				RedsandSettings::current.swordReleasedTrail = false;
				RedsandSettings::current.swordKeepPrecisionTrail = keep;
				selectedParticle = selected;
				scopeAllowed = allowed;
				Register(node, { node.get() }, 77, &item, true);
				const bool two = keep && selected && allowed;
				Check(trails.size() == (two ? 2 : 1), "registration changed default, excluded-scope or explicit external-mesh behavior");
				Check(!trails[0]->redsandNativeOnly, "primary particle renderer was replaced");
				if (!two) { continue; }
				Check(trails[1]->redsandNativeOnly && !trails[1]->ShouldAddPrecisionTrail(), "second trail can recursively add more renderers");
				for (const auto& trail : trails) {
					Check(trail->collisionNode == node.get() && trail->actorHandle.get() == node.get() &&
						trail->passedCell == 77 && trail->passedItem == &item && trail->passedLeft && trail->passedLength,
						"two trails did not share the same attack's visual attachment and handedness");
					Check(trail->Update(.016f) && trail->visualUpdates == 1, "a registered visual did not receive one update");
				}
				const auto native = trails[1]->trailParticle;
				trails.erase(trails.begin());
				Check(trails[0]->Update(.016f) && !native->particleObject->culled, "particle removal destroyed the independent ribbon");
				trails.clear();
				Check(native->age > native->lifetime && native->particleObject->culled, "manager clear left the native visual alive");
			}
		}
	}
}

void NativeOnlyLifecycleIsBoundedAndSwitchable()
{
	Node node;
	auto make = [&] { return std::make_unique<AttackTrail>(&node, Handle{ &node }, 1, nullptr, false, false, std::nullopt, true); };
	RedsandSettings::current = {};
	RedsandSettings::current.swordReleasedTrail = false;
	RedsandSettings::current.swordKeepPrecisionTrail = true;
	for (unsigned i = 0; i < 1000; ++i) {
		auto trail = make();
		auto effect = trail->trailParticle;
		Check(trail->Update(.016f), "valid native trail was rejected");
		trail.reset();
		Check(effect->age > effect->lifetime && effect->particleObject->culled, "native visual leaked on destruction");
	}
	for (unsigned reason = 0; reason < 8; ++reason) {
		auto trail = make();
		auto effect = trail->trailParticle;
		if (reason == 0) { RedsandSettings::current.swordKeepPrecisionTrail = false; }
		if (reason == 1) { Settings::bDisplayTrails = false; }
		if (reason == 2) { trail->actorHandle.value = nullptr; }
		if (reason == 3) { trail->collisionNode = nullptr; }
		if (reason == 4) { effect->age = effect->lifetime + .1f; }
		if (reason == 5) { effect->particleObject->validRoot = false; }
		if (reason == 6) { effect->particleObject->isNode = false; }
		if (reason == 7) { effect->particleObject->children.clear(); }
		Check(!trail->Update(reason >= 5 ? 2.f : .016f) && !trail->bActive && effect->particleObject->culled,
			"disabled, invalid or expired native trail survived");
		RedsandSettings::current.swordKeepPrecisionTrail = true;
		Settings::bDisplayTrails = true;
	}
	auto pending = make();
	pending->trailParticle->particleObject.reset();
	Check(pending->Update(.5f) && pending->Update(.5f) && pending->Update(.5f), "async native visual was retired too early");
	Check(!pending->Update(.5f), "missing native asset retained the renderer indefinitely");
	auto inactive = make();
	inactive->bActive = false;
	inactive->trailParticle->particleObject.reset();
	Check(!inactive->Update(.1f), "inactive native visual reentered asynchronous loading");
	auto recovered = make();
	recovered->trailParticle->particleObject.reset();
	Check(recovered->Update(.5f), "loading native visual was rejected");
	recovered->trailParticle->particleObject = std::make_shared<Node>();
	Check(recovered->Update(.5f), "late-ready native visual did not recover");
	auto pendingRoot = make();
	pendingRoot->trailParticle->particleObject->children.clear();
	Check(pendingRoot->Update(.5f) && pendingRoot->visualUpdates == 0, "partial async root reached unsafe native update");
	pendingRoot->trailParticle->particleObject->children.push_back(1);
	Check(pendingRoot->Update(.5f) && pendingRoot->visualUpdates == 1, "late-ready native skeleton did not recover");
	RedsandSettings::current.swordEnabled = false;
	RedsandSettings::current.swordPlayer = false;
	RedsandSettings::current.swordNPC = false;
	Check(recovered->Update(.016f), "disabling particles incorrectly disabled Precision's own ribbon");
	auto primary = std::make_unique<AttackTrail>(&node, Handle{ &node }, 1, nullptr, false, false, std::nullopt);
	auto effect = primary->trailParticle;
	primary.reset();
	Check(!effect->particleObject->culled && effect->age == 0.f, "default primary destructor behavior changed");
}

void NativeOverrideDoesNotInheritParticleOnlyDefinition()
{
	for (const bool redsandNativeOnly : { false, true }) {
		for (const auto path : { "RedsandParticles/Sword.nif", "Data/Meshes/Effects/WeaponTrails/ElementalDesert/Blue.nif", "Custom/Ribbon.nif" }) {
			std::optional<TrailOverride> a_trailOverride = TrailOverride{ path, 3.f, .42f };
#include "../source/NativeTrailOverride.inc"
			const bool rejected = redsandNativeOnly && Redsand::Weapons::IsParticleMesh(path);
			Check(a_trailOverride.has_value(), "companion discarded the player's API settings");
			Check(a_trailOverride->meshOverride.has_value() != rejected, "companion accepted a particle-only mesh");
			if (!rejected) { Check(*a_trailOverride->meshOverride == path, "custom ribbon mesh changed"); }
			Check(a_trailOverride->baseColorOverride == .42f && a_trailOverride->lifetimeMult == 3.f,
				"particle mesh isolation changed the player's API color or lifetime");
		}
	}
	const bool redsandNativeOnly = true;
	std::optional<TrailOverride> a_trailOverride = TrailOverride{ "RedsandParticles/Sword.nif" };
#include "../source/NativeTrailOverride.inc"
	Check(!a_trailOverride, "empty particle-only override prevented ordinary ribbon rule selection");
}

int main()
{
	try {
		using namespace Redsand::Weapons;
		for (const auto path : { "WeaponTrails/AttackTrailD.nif", "Meshes/WeaponTrails/AttackTrailMagic_FireSword.nif", "Data/Meshes/WeaponTrails/AttackTrailMagic_ShockSword_SS.nif", "DawnbreakerRework/Dawnbreaker/AttackTrailMagic_DawnBreaker.nif" }) {
			Check(IsLEDWeaponMesh(path), "known LED weapon path was not recognized");
			Check(ResolveMeleeMesh(true, path, "Effects/WeaponTrails/AttackTrail.nif") == "RedsandParticles/Default.nif", "LED animation override lost the primary particles after LED removal");
			Check(ResolveMeleeMesh(false, path, "Effects/WeaponTrails/AttackTrail.nif", false) == "Effects/WeaponTrails/AttackTrail.nif", "missing LED override prevented the native Precision ribbon after LED removal");
			Check(ResolveMeleeMesh(false, path, "Effects/WeaponTrails/AttackTrail.nif", true) == path, "installed player-selected LED ribbon was replaced");
		}
		Check(!IsLEDWeaponMesh("Other/WeaponTrails/AttackTrail.nif") && !IsLEDWeaponMesh("WeaponTrails/CustomTrail.nif") &&
			!IsLEDWeaponMesh("Effects/WeaponTrails/AttackTrail.nif"), "LED compatibility captured another mod's ribbon");
		Check(CheckMagicConditions(false, false) && CheckMagicConditions(false, true) &&
			!CheckMagicConditions(true, false) && CheckMagicConditions(true, true), "free enchantment skipped actual effect matching or changed ribbon policy");
		Check(!CheckMagicConditions(true, false, true) && CheckMagicConditions(true, true, true), "companion changed upstream magic-condition policy");
		Check(!UseEnchantmentFallback(false, false, true) && !UseEnchantmentFallback(true, true, true), "explicit color was overwritten by fallback");
		Check(UseEnchantmentFallback(false, false, false) && !UseEnchantmentFallback(true, true, false) &&
			!UseEnchantmentFallback(true, false, false) && !UseEnchantmentFallback(false, true, false),
			"Redsand enchantment fallback leaked into the player's ribbon");
		Check(!TrailDefinitionAllowed(false, false) && TrailDefinitionAllowed(false, true), "Redsand consumed an unrelated ribbon mesh/color definition");
		Check(TrailDefinitionAllowed(true, false) && !TrailDefinitionAllowed(true, true), "ribbon consumed a Redsand particle definition");
		Check(TintFor(EnchantmentTone::Fire).red == 1.f && TintFor(EnchantmentTone::Frost).blue == 1.f &&
			TintFor(EnchantmentTone::Shock).blue == 1.f && TintFor(EnchantmentTone::Poison).green == 1.f, "elemental tint palette changed");
		Check(EnchantmentHasCharge(false, false, 0.) && EnchantmentHasCharge(false, true, 10.) &&
			!EnchantmentHasCharge(false, true, 0.) && EnchantmentHasCharge(true, true, 0.), "free enchantment or poison mistaken for depleted charge");
		Check(InsufficientEnchantmentCharge(false, true, true, 0., false, -1.f, -1.f) &&
			InsufficientEnchantmentCharge(false, true, true, .5, true, 2.f, 3.f), "empty or partially insufficient charge did not restore original tint");
		Check(!InsufficientEnchantmentCharge(false, true, true, .5, true, 3.f, 3.f) &&
			!InsufficientEnchantmentCharge(true, true, true, 0., true, 0.f, 3.f) &&
			!InsufficientEnchantmentCharge(false, true, false, 0., false, -1.f, 0.f) &&
			!InsufficientEnchantmentCharge(false, false, true, 0., false, -1.f, -1.f),
			"sufficient charge, poison, free enchantment or plain weapon lost existing color policy");
        {
            Node node; int item=0; RedsandSettings::current={}; RedsandSettings::current.swordReleasedTrail=true;
            AttackTrail replacement(&node,{&node},1,&item,false,false,{});
            Check(!replacement.ShouldAddPrecisionTrail(),"replacement mode stacked the original Precision ribbon");
            RedsandSettings::current={};
        }
		RegistrationKeepsTwoVisualsWithoutRecursion();
		NativeOnlyLifecycleIsBoundedAndSwitchable();
		NativeOverrideDoesNotInheritParticleOnlyDefinition();
		std::cout << "Independent Precision ribbon registration, selection and lifecycle passed\n";
		return 0;
	} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
