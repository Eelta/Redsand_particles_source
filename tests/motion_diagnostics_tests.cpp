#include "../source/RedsandMotionTrace.h"

#include <chrono>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

void check(bool value, const char* message)
{
	if (!value) { throw std::runtime_error(message); }
}

namespace RE
{
	struct NiPoint2 { float x = 0, y = 0; };
	struct NiPoint3 { float x = 1, y = 2, z = 3; };
	using BSFixedString = std::string_view;
	struct BSAnimationGraphEvent {};
	struct ActorState
	{
		unsigned weapon = 3, attack = 10, knock = 2, sit = 4;
		unsigned GetWeaponState() const { return weapon; }
		unsigned GetAttackState() const { return attack; }
		unsigned GetKnockState() const { return knock; }
		unsigned GetSitSleepState() const { return sit; }
	};
	struct ImplicitActorState
	{
		unsigned GetWeaponState() const { throw std::runtime_error("unsafe implicit ActorState weapon getter"); }
		unsigned GetAttackState() const { throw std::runtime_error("unsafe implicit ActorState attack getter"); }
		unsigned GetKnockState() const { throw std::runtime_error("unsafe implicit ActorState knock getter"); }
		unsigned GetSitSleepState() const { throw std::runtime_error("unsafe implicit ActorState sit getter"); }
	};
	enum class ActorValue { kSpeedMult, kParalysis, kStamina };
	struct Actor
	{
		enum class BOOL_FLAGS { kMovementBlocked, kAttackingDisabled };
	};
	struct Flags
	{
		unsigned underlying() const { return 0; }
		bool all(Actor::BOOL_FLAGS) const { return false; }
	};
	struct PlayerCharacter : Actor, ImplicitActorState
	{
		ActorState state;
		bool hasCell = true, has3D = true;
		struct RuntimeData { Flags boolFlags; } data;
		ActorState* AsActorState() { return &state; }
		void* GetParentCell() { return hasCell ? this : nullptr; }
		void* Get3D() { return has3D ? this : nullptr; }
		const RuntimeData& GetActorRuntimeData() const { return data; }
		unsigned GetFormID() const { return 0x14; }
		NiPoint3 GetPosition() const { return {}; }
		bool IsMovementAnimationDriven() const { return false; }
		bool IsInKillMove() const { return false; }
		float GetActorValue(ActorValue) const { throw std::runtime_error("unsafe nonessential diagnostic ActorValue read"); }
	};
	struct Mask { unsigned underlying() const { return 0xFFFFFFFF; } };
	struct ControlMap
	{
		struct Data { Mask enabledControls, storedControls; bool ignoreKeyboardMouse = false; } data;
		inline static ControlMap* instance = nullptr;
		static ControlMap* GetSingleton() { return instance; }
		const Data& GetRuntimeData() const { return data; }
		bool IsMovementControlsEnabled() const { return true; }
		bool IsJumpingControlsEnabled() const { return true; }
		bool IsFightingControlsEnabled() const { return true; }
	};
	struct PlayerInputHandler
	{
		bool enabled = true;
		bool IsInputEventHandlingEnabled() const { return enabled; }
	};
	struct PlayerControls
	{
		inline static PlayerControls* instance = nullptr;
		static PlayerControls* GetSingleton() { return instance; }
		bool blockPlayerInput = false;
		PlayerInputHandler* movementHandler = nullptr;
		PlayerInputHandler* jumpHandler = nullptr;
		PlayerInputHandler* attackBlockHandler = nullptr;
		PlayerInputHandler* readyWeaponHandler = nullptr;
		struct Data { NiPoint2 moveInputVec, prevMoveVec; } data;
	};
	struct BShkbAnimationGraph
	{
		std::uintptr_t source = 0;
		unsigned reads = 0;
		template<class T> void* GetEventSource() { return reinterpret_cast<void*>(source); }
		bool GetGraphVariableBool(BSFixedString name, bool& value)
		{
			++reads;
			if (name != "IsAttacking") { return false; }
			value = true;
			return true;
		}
		bool GetGraphVariableInt(BSFixedString name, int& value)
		{
			++reads;
			if (name != "iLeftHandType") { return false; }
			value = 7;
			return true;
		}
	};
}

namespace RedsandLog
{
	struct Entry { std::string format; std::vector<std::string> values; };
	std::vector<Entry> entries;
	template<class T> std::string valueString(const T& value)
	{
		std::ostringstream result;
		result << value;
		return result.str();
	}
	template<class... Args> void info(const char* format, const Args&... args)
	{
		entries.push_back({ format, { valueString(args)... } });
	}
	const Entry& find(std::string_view name)
	{
		for (const auto& entry : entries) { if (entry.format.find(name) != std::string::npos) { return entry; } }
		throw std::runtime_error("missing diagnostic record");
	}
}

namespace fmt
{
	inline void format_to(std::back_insert_iterator<std::string> output, const char*, const char* name)
	{
		const auto text = std::string(" ") + name + "=?";
		for (char character : text) { *output++ = character; }
	}
	template<class T> void format_to(std::back_insert_iterator<std::string> output, const char*, const char* name, const T& value)
	{
		const auto text = std::string(" ") + name + "=" + RedsandLog::valueString(value);
		for (char character : text) { *output++ = character; }
	}
}

Redsand::MotionTrace motionTrace;
struct View { std::uintptr_t source = 0; std::uint64_t epoch = 0; bool firstPerson = false; } playerView;
std::vector<std::shared_ptr<RE::BShkbAnimationGraph>> playerGraphs;

#ifndef REDSAND_MOTION_DIAGNOSTICS_FILE
#define REDSAND_MOTION_DIAGNOSTICS_FILE "../source/BowMotionDiagnostics.inc"
#endif
#include REDSAND_MOTION_DIAGNOSTICS_FILE

int main() try
{
	RE::PlayerCharacter player;
	LogPlayerMotion(&player, true);
	check(RedsandLog::entries.size() == 1, "paused diagnostic sampled native player state");
	RedsandLog::entries.clear();
	LogPlayerMotion(&player, false);
	const auto& actor = RedsandLog::find("motion actor:");
	check(actor.values.size() == 13, "nonessential AV reads or unexpected actor diagnostic contract");
	check(actor.values[1] == "3" && actor.values[2] == "10" && actor.values[7] == "2" && actor.values[8] == "4", "runtime ActorState view values lost");
	check(RedsandLog::find("motion graph:").values.back().find("iLeftHandType=?") != std::string::npos, "missing graph became a known int");
	RE::ControlMap map;
	RE::PlayerControls controls;
	RE::PlayerInputHandler movement, jump, attack, ready;
	jump.enabled = false;
	RE::ControlMap::instance = &map;
	RE::PlayerControls::instance = &controls;
	controls.movementHandler = &movement;
	controls.jumpHandler = &jump;
	controls.attackBlockHandler = &attack;
	controls.readyWeaponHandler = &ready;
	auto inactive = std::make_shared<RE::BShkbAnimationGraph>();
	auto selected = std::make_shared<RE::BShkbAnimationGraph>();
	inactive->source = 1;
	selected->source = playerView.source = 2;
	playerGraphs = { inactive, selected };
	RedsandLog::entries.clear();
	LogPlayerMotion(&player, false);
	check(inactive->reads == 0 && selected->reads > 0, "diagnostic queried an inactive graph");
	const auto& graph = RedsandLog::find("motion graph:").values.back();
	check(graph.find("IsAttacking=1") != std::string::npos && graph.find("iLeftHandType=7") != std::string::npos, "readable graph values lost");
	check(graph.find("BFCO_ComboLocked=?") != std::string::npos && graph.find("BFCO_LastAttack=?") != std::string::npos, "missing variables became zero");
	const auto& input = RedsandLog::find("motion controls:");
	check(input.values[9] == "1" && input.values[10] == "0" && input.values[11] == "1" && input.values[12] == "1", "handler enabled states lost");
	RedsandLog::entries.clear();
	LogPlayerMotion(nullptr, true);
	check(RedsandLog::entries.size() == 1, "missing player performed engine-state reads");
	player.hasCell = false;
	RedsandLog::entries.clear();
	LogPlayerMotion(&player, false);
	check(RedsandLog::entries.size() == 1, "unloaded player sampled native state");
	player.hasCell = true;
	player.has3D = false;
	RedsandLog::entries.clear();
	LogPlayerMotion(&player, false);
	check(RedsandLog::entries.size() == 1, "missing 3D sampled native state");
	std::cout << "PASS: production motion diagnostic avoids AV/implicit ActorState reads and preserves runtime view/unknown states\n";
	return 0;
}
catch (const std::exception& error)
{
	std::cerr << error.what() << '\n';
	return 1;
}
