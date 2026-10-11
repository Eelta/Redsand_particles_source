		void LogPlayerMotion(RE::PlayerCharacter* player, bool paused)
		{
			const auto batch = motionTrace.Drain();
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			RedsandLog::info("Redsand motion trace: time={} paused={} source={:X} epoch={} firstPerson={} events={} dropped={}",
				now, paused, playerView.source, playerView.epoch, playerView.firstPerson, batch.count, batch.dropped);
			for (std::size_t i = 0; i < batch.count; ++i) {
				const auto& event = batch.events[i];
				RedsandLog::info("Redsand motion event: seq={} time={} source={:X} epoch={} truncated={} hasNumber={} number={} tag=[{}] payload=[{}]",
					event.sequence, event.time, event.source, event.epoch, event.truncated, event.hasNumber, event.number, event.tag.data(), event.payload.data());
			}
			if (!player || paused || !player->GetParentCell() || !player->Get3D()) { return; }
			auto* map = RE::ControlMap::GetSingleton();
			auto* controls = RE::PlayerControls::GetSingleton();
			const auto handler = [](const RE::PlayerInputHandler* value) { return value ? static_cast<int>(value->IsInputEventHandlingEnabled()) : -1; };
			RedsandLog::info("Redsand motion controls: map={} enabled={:X} stored={:X} movement={} jumping={} fighting={} ignoreKeyboardMouse={} controls={} blockInput={} moveHandler={} jumpHandler={} attackHandler={} readyHandler={} moveInput=[{},{}] prevMoveInput=[{},{}]",
				map != nullptr, map ? map->GetRuntimeData().enabledControls.underlying() : 0, map ? map->GetRuntimeData().storedControls.underlying() : 0,
				map ? static_cast<int>(map->IsMovementControlsEnabled()) : -1, map ? static_cast<int>(map->IsJumpingControlsEnabled()) : -1,
				map ? static_cast<int>(map->IsFightingControlsEnabled()) : -1, map ? static_cast<int>(map->GetRuntimeData().ignoreKeyboardMouse) : -1,
				controls != nullptr, controls ? static_cast<int>(controls->blockPlayerInput) : -1,
				handler(controls ? controls->movementHandler : nullptr), handler(controls ? controls->jumpHandler : nullptr),
				handler(controls ? controls->attackBlockHandler : nullptr), handler(controls ? controls->readyWeaponHandler : nullptr),
				controls ? controls->data.moveInputVec.x : 0.f, controls ? controls->data.moveInputVec.y : 0.f,
				controls ? controls->data.prevMoveVec.x : 0.f, controls ? controls->data.prevMoveVec.y : 0.f);
			const auto flags = player->GetActorRuntimeData().boolFlags;
			const auto position = player->GetPosition();
			const auto* state = player->AsActorState();
			RedsandLog::info("Redsand motion actor: actor={:08X} weaponState={} attackState={} flags={:X} movementBlocked={} attackingDisabled={} movementAnimationDriven={} knockState={} sitSleepState={} killMove={} position=[{},{},{}]",
				player->GetFormID(), static_cast<std::uint32_t>(state->GetWeaponState()), static_cast<std::uint32_t>(state->GetAttackState()), flags.underlying(),
				flags.all(RE::Actor::BOOL_FLAGS::kMovementBlocked), flags.all(RE::Actor::BOOL_FLAGS::kAttackingDisabled), player->IsMovementAnimationDriven(),
				static_cast<std::uint32_t>(state->GetKnockState()), static_cast<std::uint32_t>(state->GetSitSleepState()), player->IsInKillMove(),
				position.x, position.y, position.z);
			RE::BShkbAnimationGraph* selected = nullptr;
			for (const auto& graph : playerGraphs) {
				if (reinterpret_cast<std::uintptr_t>(graph->GetEventSource<RE::BSAnimationGraphEvent>()) == playerView.source) { selected = graph.get(); break; }
			}
			std::string variables;
			for (const auto* name : { "bAnimationDriven", "bMotionDriven", "bAllowRotation", "IsAttacking", "bBowDrawn", "bInJumpState", "HKSMoveON",
				"BFCO_IsInputWindow", "BFCO_IsLooping", "BFCO_IsAttackStarting", "BFCO_bAttackStartReady", "BFCO_IsPlayerInputOK", "BFCO_ComboLocked" }) {
				bool value = false;
				const bool valid = selected && selected->GetGraphVariableBool(RE::BSFixedString(name), value);
				fmt::format_to(std::back_inserter(variables), " {}={}", name, valid ? (value ? "1" : "0") : "?");
			}
			for (const auto* name : { "iWantBlock", "i1stPerson", "iLeftHandType", "BFCO_LastAttack", "BFCO_NextNormal", "BFCO_NextPower", "BFCO_SAtkEnable", "BFCO_PSAtkEnable" }) {
				int value = 0;
				if (selected && selected->GetGraphVariableInt(RE::BSFixedString(name), value)) { fmt::format_to(std::back_inserter(variables), " {}={}", name, value); }
				else { fmt::format_to(std::back_inserter(variables), " {}=?", name); }
			}
			RedsandLog::info("Redsand motion graph: source={:X} graph={}{}", playerView.source, static_cast<void*>(selected), variables);
		}
