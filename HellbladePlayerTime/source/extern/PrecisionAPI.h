#pragma once


namespace PRECISION_API
{
	constexpr const auto PrecisionPluginName = "Precision";


	enum class InterfaceVersion : uint8_t
	{
		V1,
		V2,
		V3,
		V4
	};


	enum class APIResult : uint8_t
	{

		OK,


		AlreadyRegistered,


		NotRegistered,
	};

	struct PreHitModifier
	{
		enum class ModifierType : uint8_t
		{
			Damage,
			Stagger
		};

		enum class ModifierOperation : uint8_t
		{
			Additive,
			Multiplicative
		};

		ModifierType modifierType;
		ModifierOperation modifierOperation;
		float modifierValue;
	};

	struct PreHitCallbackReturn
	{

		bool bIgnoreHit = false;


		std::vector<PreHitModifier> modifiers;
	};

	struct WeaponCollisionCallbackReturn
	{

		bool bIgnoreHit = true;
	};

	enum class PrecisionLayerType : uint8_t
	{
		None,
		Attack,
		Body
	};

	struct PrecisionLayerSetupCallbackReturn
	{
		PrecisionLayerSetupCallbackReturn() :
			precisionLayerType(PrecisionLayerType::None), layersToAdd(0), layersToRemove(0)
		{}


		PrecisionLayerType precisionLayerType;


		uint64_t layersToAdd;


		uint64_t layersToRemove;
	};

	struct PrecisionHitData
	{
		PrecisionHitData(RE::Actor* a_attacker, RE::TESObjectREFR* a_target, RE::hkpRigidBody* a_hitRigidBody, RE::hkpRigidBody* a_hittingRigidBody, const RE::NiPoint3& a_hitPos,
			const RE::NiPoint3& a_separatingNormal, const RE::NiPoint3& a_hitPointVelocity, RE::hkpShapeKey a_hitBodyShapeKey, RE::hkpShapeKey a_hittingBodyShapeKey) :
			attacker(a_attacker),
			target(a_target), hitRigidBody(a_hitRigidBody), hittingRigidBody(a_hittingRigidBody), hitPos(a_hitPos), separatingNormal(a_separatingNormal),
			hitPointVelocity(a_hitPointVelocity), hitBodyShapeKey(a_hitBodyShapeKey), hittingBodyShapeKey(a_hittingBodyShapeKey)
		{}

		RE::Actor* attacker;
		RE::TESObjectREFR* target;
		RE::hkpRigidBody* hitRigidBody;
		RE::hkpRigidBody* hittingRigidBody;

		RE::NiPoint3 hitPos;
		RE::NiPoint3 separatingNormal;
		RE::NiPoint3 hitPointVelocity;

		RE::hkpShapeKey hitBodyShapeKey;
		RE::hkpShapeKey hittingBodyShapeKey;
	};

	enum class CollisionFilterComparisonResult : uint8_t
	{
		Continue,
		Collide,
		Ignore,
	};

	enum class RequestedAttackCollisionType : uint8_t
	{
		Default,
		Current,
		RightWeapon,
		LeftWeapon,
	};

	using PreHitCallback = std::function<PreHitCallbackReturn(const PrecisionHitData&)>;
	using PostHitCallback = std::function<void(const PrecisionHitData&, const RE::HitData&)>;
	using PrePhysicsStepCallback = std::function<void(RE::bhkWorld*)>;
	using CollisionFilterComparisonCallback = std::function<CollisionFilterComparisonResult(RE::bhkCollisionFilter*, uint32_t, uint32_t)>;
	using WeaponCollisionCallback = std::function<WeaponCollisionCallbackReturn(const PrecisionHitData&)>;
	using CollisionFilterSetupCallback = std::function<void(RE::bhkCollisionFilter*)>;
	using ContactListenerCallback = std::function<void(const RE::hkpContactPointEvent&)>;
	using PrecisionLayerSetupCallback = std::function<PrecisionLayerSetupCallbackReturn()>;


	class IVPrecision1
	{
	public:






		virtual APIResult AddPreHitCallback(SKSE::PluginHandle a_myPluginHandle, PreHitCallback&& a_preHitCallback) noexcept = 0;







		virtual APIResult AddPostHitCallback(SKSE::PluginHandle a_myPluginHandle, PostHitCallback&& a_postHitCallback) noexcept = 0;







		virtual APIResult AddPrePhysicsStepCallback(SKSE::PluginHandle a_myPluginHandle, PrePhysicsStepCallback&& a_prePhysicsStepCallback) noexcept = 0;







		virtual APIResult AddCollisionFilterComparisonCallback(SKSE::PluginHandle a_myPluginHandle, CollisionFilterComparisonCallback&& a_collisionFilterComparisonCallback) noexcept = 0;






		virtual APIResult RemovePreHitCallback(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;






		virtual APIResult RemovePostHitCallback(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;






		virtual APIResult RemovePrePhysicsStepCallback(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;






		virtual APIResult RemoveCollisionFilterComparisonCallback(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;







		virtual float GetAttackCollisionCapsuleLength(RE::ActorHandle a_actorHandle, RequestedAttackCollisionType a_collisionType = RequestedAttackCollisionType::Default) const noexcept = 0;
	};

	class IVPrecision2 : public IVPrecision1
	{
	public:






		virtual APIResult AddWeaponWeaponCollisionCallback(SKSE::PluginHandle a_myPluginHandle, WeaponCollisionCallback&& a_callback) noexcept = 0;






		virtual APIResult RemoveWeaponWeaponCollisionCallback(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;







		virtual APIResult AddWeaponProjectileCollisionCallback(SKSE::PluginHandle a_myPluginHandle, WeaponCollisionCallback&& a_callback) noexcept = 0;






		virtual APIResult RemoveWeaponProjectileCollisionCallback(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;

		[[deprecated("Use ApplyHitImpulse2 instead")]] virtual void ApplyHitImpulse(RE::ActorHandle a_actorHandle, RE::hkpRigidBody* a_rigidBody, const RE::NiPoint3& a_hitVelocity, const RE::hkVector4& a_hitPosition, float a_impulseMult) noexcept = 0;
	};

	class IVPrecision3 : public IVPrecision2
	{
	public:






		virtual APIResult AddCollisionFilterSetupCallback(SKSE::PluginHandle a_myPluginHandle, CollisionFilterSetupCallback&& a_callback) noexcept = 0;






		virtual APIResult RemoveCollisionFilterSetupCallback(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;







		virtual APIResult AddContactListenerCallback(SKSE::PluginHandle a_myPluginHandle, ContactListenerCallback&& a_callback) noexcept = 0;






		virtual APIResult RemoveContactListenerCallback(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;






		[[nodiscard]] virtual bool IsActorActive(RE::ActorHandle a_actorHandle) const noexcept = 0;






		[[nodiscard]] virtual bool IsActorActiveCollisionGroup(uint16_t a_collisionGroup) const noexcept = 0;






		[[nodiscard]] virtual bool IsActorCharacterControllerHittable(RE::ActorHandle a_actorHandle) const noexcept = 0;






		[[nodiscard]] virtual bool IsCharacterControllerHittable(RE::bhkCharacterController* a_characterController) const noexcept = 0;






		[[nodiscard]] virtual bool IsCharacterControllerHittableCollisionGroup(uint16_t a_collisionGroup) const noexcept = 0;







		virtual bool ToggleDisableActor(RE::ActorHandle a_actorHandle, bool a_bDisable) noexcept = 0;
	};

	class IVPrecision4 : public IVPrecision3
	{
	public:






		virtual APIResult AddPrecisionLayerSetupCallback(SKSE::PluginHandle a_myPluginHandle, PrecisionLayerSetupCallback&& a_callback) noexcept = 0;






		virtual APIResult RemovePrecisionLayerSetupCallback(SKSE::PluginHandle a_myPluginHandle) noexcept = 0;







		virtual RE::NiAVObject* GetOriginalFromClone(RE::ActorHandle a_actorHandle, RE::NiAVObject* a_node) noexcept = 0;







		virtual RE::hkpRigidBody* GetOriginalFromClone(RE::ActorHandle a_actorHandle, RE::hkpRigidBody* a_hkpRigidBody) noexcept = 0;










		virtual void ApplyHitImpulse2(RE::ActorHandle a_targetActorHandle, RE::ActorHandle a_sourceActorHandle, RE::hkpRigidBody* a_rigidBody, const RE::NiPoint3& a_hitVelocity, const RE::hkVector4& a_hitPosition, float a_impulseMult) noexcept = 0;
	};

	typedef void* (*_RequestPluginAPI)(const InterfaceVersion interfaceVersion);







	[[nodiscard]] inline void* RequestPluginAPI(const InterfaceVersion a_interfaceVersion = InterfaceVersion::V4)
	{
		auto pluginHandle = GetModuleHandleA("Precision.dll");
		_RequestPluginAPI requestAPIFunction = (_RequestPluginAPI)::GetProcAddress(pluginHandle, "RequestPluginAPI");
		if (requestAPIFunction) {
			return requestAPIFunction(a_interfaceVersion);
		}
		return nullptr;
	}
}
