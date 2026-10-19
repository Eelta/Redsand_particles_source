
		std::string_view redsandColorSource = baseColorOverride ? "particle-rule-or-api" : "unresolved";
		const auto redsandCharge = a_weaponItem ? a_weaponItem->GetEnchantmentCharge() : std::optional<double>{};
		bool redsandInsufficientCharge = false;
		float redsandAvailableCharge = -1.f, redsandChargeCost = -1.f;
		if ((!redsandLegacyTrail) && a_weaponItem) {
			const auto* poison = GetPoison(a_weaponItem);
			auto* enchantment = GetEnchantment(a_weaponItem);
			bool costAvailable = false;
			if (!poison && enchantment && redsandActor) {
				const auto casting = a_bIsLeftHand ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;
				const auto value = GetActorValueForCost(enchantment, casting);
				if (value != RE::ActorValue::kNone) {
					redsandAvailableCharge = redsandActor->AsActorValueOwner()->GetActorValue(value);
					redsandChargeCost = enchantment->CalculateMagickaCost(redsandActor.get());
					costAvailable = std::isfinite(redsandAvailableCharge) && std::isfinite(redsandChargeCost);
				}
			}
			redsandInsufficientCharge = Redsand::Weapons::InsufficientEnchantmentCharge(poison != nullptr, enchantment != nullptr,
				redsandCharge.has_value(), redsandCharge.value_or(0.), costAvailable, redsandAvailableCharge, redsandChargeCost);
			if (redsandInsufficientCharge && !(a_trailOverride && a_trailOverride->baseColorOverride)) {
				const auto tint = Redsand::Weapons::defaultTrailTint;
				baseColorOverride = RE::NiColorA{ tint.red, tint.green, tint.blue, 1.f };
				redsandColorSource = "insufficient-charge-original-blue";
			}
		}
		if (a_trailOverride && !redsandInsufficientCharge && a_weaponItem &&
			Redsand::Weapons::UseEnchantmentFallback(redsandLegacyTrail, redsandNativeOnly, baseColorOverride.has_value())) {



			TrailDefinition particleRule;
			if (GetTrailDefinition(a_actorHandle, a_weaponItem, a_bIsLeftHand, particleRule) && particleRule.trailOverride.baseColorOverride) {
				baseColorOverride = particleRule.trailOverride.baseColorOverride;
				redsandColorSource = "particle-rule-with-api-mesh";
			}
		}
		if (a_weaponItem && Redsand::Weapons::UseEnchantmentFallback(redsandLegacyTrail, redsandNativeOnly, baseColorOverride.has_value())) {




			if ((GetPoison(a_weaponItem) || GetEnchantment(a_weaponItem)) &&
				Redsand::Weapons::EnchantmentHasCharge(GetPoison(a_weaponItem) != nullptr,
					redsandCharge.has_value(), redsandCharge.value_or(0.))) {
				TrailDefinition compatibleRule;
				if (GetTrailDefinition(a_actorHandle, a_weaponItem, a_bIsLeftHand, compatibleRule, true) && compatibleRule.trailOverride.baseColorOverride) {
					baseColorOverride = compatibleRule.trailOverride.baseColorOverride;
					redsandColorSource = "compatible-enchantment-color-rule";
				}
			}
		}
		if (a_weaponItem && Redsand::Weapons::UseEnchantmentFallback(redsandLegacyTrail, redsandNativeOnly, baseColorOverride.has_value())) {
			auto* magic = GetPoison(a_weaponItem);
			const bool poison = magic != nullptr;
			if (!magic) { magic = GetEnchantment(a_weaponItem); }
			const auto charge = a_weaponItem->GetEnchantmentCharge();
			const bool active = Redsand::Weapons::EnchantmentHasCharge(poison, charge.has_value(), charge.value_or(0.));
			if (magic && active) {
				using Tone = Redsand::Weapons::EnchantmentTone;
				std::uint32_t selectedEffect = 0;
				const char* selectedSource = "none";
				const auto tint = Redsand::Weapons::ResolveEnchantmentTint(magic, [&](auto* item) -> std::optional<Redsand::Weapons::EnchantmentTint> {
					auto* effect = item->baseEffect;
					if (!effect) { return std::nullopt; }
					selectedEffect = effect->GetFormID();
					for (const auto& [name, tone] : std::array<std::pair<const char*, Tone>, 3>{
						std::pair{ "MagicDamageFire", Tone::Fire }, { "MagicDamageFrost", Tone::Frost }, { "MagicDamageShock", Tone::Shock } }) {
						if (effect->HasKeywordString(name)) { selectedSource = name; return Redsand::Weapons::TintFor(tone); }
					}
					if (poison) { selectedSource = "poison"; return Redsand::Weapons::TintFor(Tone::Poison); }
					if (const auto* shader = effect->data.enchantShader) {
						const auto& data = shader->data;
						const std::array colors{ data.fillTextureEffectColorKey1, data.edgeEffectColor,
							data.fillTextureEffectColorKey2, data.fillTextureEffectColorKey3, data.colorKey1, data.colorKey2, data.colorKey3 };
						if (const auto candidate = Redsand::Weapons::FirstEnchantmentHue(colors)) {
							selectedSource = "chromatic-shader-key";
							return candidate;
						}
					}
					return std::nullopt;
				});
				if (tint) { baseColorOverride = RE::NiColorA{ tint->red, tint->green, tint->blue, 1.f }; redsandColorSource = "enchantment-fallback"; }
				static std::atomic<unsigned> effectLogs{ 0 };
				if (effectLogs.load(std::memory_order_relaxed) < 12 && effectLogs.fetch_add(1, std::memory_order_relaxed) < 12) {
					logger::info("Redsand enchantment selection: magic={:08X} effects={} selected={:08X} source={} resolved={}",
						magic->GetFormID(), magic->effects.size(), tint ? selectedEffect : 0, tint ? selectedSource : "original-blue", tint.has_value());
				}
			}
			static std::atomic<unsigned> tintLogs{ 0 };
			if (magic && tintLogs.fetch_add(1) < 12) {
				const auto color = baseColorOverride.value_or(RE::NiColorA{ .025f, .080f, .650f, 1.f });
				RedsandLog::info("Redsand enchantment tint: weapon={:08X} magic={:08X} poison={} active={} rgb={}/{}/{}",
					redsandWeapon ? redsandWeapon->GetFormID() : 0, magic->GetFormID(), poison, active, color.red, color.green, color.blue);
			}
		}
		if (!redsandLegacyTrail) {
			static std::array<std::atomic<unsigned>, 2> colorSelections{};
			if (colorSelections[redsandInsufficientCharge ? 1 : 0].fetch_add(1) < 12) {
				const auto tint = Redsand::Weapons::defaultTrailTint;
				const auto color = baseColorOverride.value_or(RE::NiColorA{ tint.red, tint.green, tint.blue, 1.f });
				if (!baseColorOverride) { redsandColorSource = "original-blue-default"; }
				logger::info("Redsand resolved trail color: actor={:08X} weapon={:08X} companion={} source={} rgb={}/{}/{} customRGB={} chargePercent={} availableCharge={} cost={} insufficientCharge={}",
					redsandActor ? redsandActor->GetFormID() : 0, redsandWeapon ? redsandWeapon->GetFormID() : 0,
					redsandNativeOnly, redsandColorSource, color.red, color.green, color.blue,
					redsandConfig.colors[static_cast<unsigned>(RedsandSettings::ColorTarget::Melee)].custom,
					redsandCharge.value_or(-1.), redsandAvailableCharge, redsandChargeCost, redsandInsufficientCharge);
			}
		}
