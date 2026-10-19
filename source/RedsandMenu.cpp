#include "RedsandMenu.h"
#include "RedsandLog.h"
#include "RedsandSettings.h"
#include "RedsandImpactVFX.h"
#include "vendor/SKSEMenuFrameworkMinimal.h"

#include <string>

namespace RedsandMenu
{
	namespace
	{
		Redsand::MenuFramework::API menu;
		bool registered = false;

		struct Text
		{
			const char* english;
			const char* chinese;
			const char* Get(const RedsandSettings::Config& config) const
			{
				return config.language == RedsandSettings::Language::Chinese ? chinese : english;
			}
		};

		struct Label
		{
			Text text;
			const char* id;
			std::string Get(const RedsandSettings::Config& config) const { return std::string(text.Get(config)) + "###" + id; }
		};

		void SaveNow(const RedsandSettings::Config& config)
		{
			RedsandSettings::Set(config);
			RedsandLog::Sync();
			RedsandSettings::FlushPendingSave();
		}

		void Describe(Text text, const RedsandSettings::Config& config, const char*& description)
		{
			if (menu.IsItemHovered(0)) { description = text.Get(config); }
		}

		void Checkbox(Label label, bool& value, RedsandSettings::Config& config, Text help, const char*& description)
		{
			if (menu.Checkbox(label.Get(config).c_str(), &value)) { SaveNow(config); }
			Describe(help, config, description);
		}

		void Slider(Label label, float& value, float minimum, float maximum, RedsandSettings::Config& config,
			Text help, const char*& description)
		{
			constexpr int alwaysClamp = 1 << 4;
			const auto format = Text{ "%.2fx", "%.2f 倍" }.Get(config);
			if (menu.SliderFloat(label.Get(config).c_str(), &value, minimum, maximum, format, alwaysClamp)) {
				RedsandSettings::Set(config);
			}
			if (menu.IsItemDeactivatedAfterEdit && menu.IsItemDeactivatedAfterEdit()) { RedsandSettings::FlushPendingSave(); }
			Describe(help, config, description);
		}

		void ValhallaRing(RedsandSettings::Config& config, const char*& description)
		{
			const Label label{ { "Keep Valhalla block ring", "保留 Valhalla 格挡圆环" }, "preserveValhallaRing" };
			const Text help{ "Keep the installed Valhalla perfect-block ring. Its size, glow and color come from its own resource. Uncheck to use the adjustable Redsand ring.",
				"保留 Valhalla 完美格挡圆环，大小、亮度和颜色由该资源决定；关闭后使用可调的红沙圆环。" };
			const auto state = RedsandImpactVFX::GetStatus();
			auto* handler = RE::TESDataHandler::GetSingleton();
			const bool available = state.availability == RedsandImpactVFX::Availability::Ready && state.valhallaRingControlled && handler &&
				handler->LookupForm<RE::BGSExplosion>(0x60C32, "ValhallaCombat.esp");
			if (available) { Checkbox(label, config.preserveValhallaRing, config, help, description); return; }
			if (menu.BeginDisabled && menu.EndDisabled) {
				menu.BeginDisabled(true);
				auto display = config.preserveValhallaRing;
				menu.Checkbox(label.Get(config).c_str(), &display);
				menu.EndDisabled();
			} else { menu.TextUnformatted(label.text.Get(config), nullptr); }
			menu.TextUnformatted(Text{ "Requires Valhalla Combat and the Redsand block-effects component.", "需要启用 Valhalla Combat，并安装红沙格挡效果组件。" }.Get(config), nullptr);
		}

		void Color(Label label, RedsandSettings::ColorTarget target, RedsandSettings::Config& config, const char*& description)
		{
			auto& color = config.colors[static_cast<unsigned>(target)];
			const auto caption = std::string(Text{ "Custom ", "自定义" }.Get(config)) + label.text.Get(config) + "###" + label.id + "Enabled";
			if (menu.Checkbox(caption.c_str(), &color.custom)) { SaveNow(config); }
			const bool weaponColor = target == RedsandSettings::ColorTarget::Melee || target == RedsandSettings::ColorTarget::Bow;
			const Text colorHelp = weaponColor ?
				Text{ "Use a fixed particle color. Uncheck for enchantment colors or the default blue. Precision ribbon colors are unchanged.",
					"勾选使用固定粒子颜色；取消后跟随附魔，无有效附魔时使用默认蓝色。不改变 Precision 剑影颜色。" } :
				Text{ "Use a custom color. Uncheck to restore this effect's default color.",
					"勾选使用自定义颜色；取消后恢复此效果的默认颜色。" };
			Describe(colorHelp, config, description);
			if (menu.ColorEdit3) {
				menu.SameLine(0.0f, -1.0f);


				constexpr int swatchOnly = (1 << 5) | (1 << 7);
				if (menu.ColorEdit3(label.Get(config).c_str(), color.rgb.data(), swatchOnly)) {
					color.custom = true;
					RedsandSettings::Set(config);
				}
				if (menu.IsItemDeactivatedAfterEdit && menu.IsItemDeactivatedAfterEdit()) { RedsandSettings::FlushPendingSave(); }
			} else {
				const char* names[] = { "R", "G", "B" };
				for (unsigned k = 0; k < 3; ++k) {
					const auto key = std::string(names[k]) + "###" + label.id + names[k];
					if (menu.SliderFloat(key.c_str(), &color.rgb[k], 0.f, 1.f, "%.3f", 1 << 4)) { color.custom = true; RedsandSettings::Set(config); }
				}
				menu.TextUnformatted(Text{ "Adjust the RGB sliders to choose a color.", "使用 RGB 滑块调整颜色。" }.Get(config), nullptr);
			}
			Describe({ "Click the swatch to choose a color or enter RGB values. Choosing a color enables it automatically.",
				"点击色块选色或输入 RGB 数值，选色后自动启用。" }, config, description);
		}

		bool Fold(Label label, const RedsandSettings::Config& config)
		{
			if (menu.CollapsingHeader) { return menu.CollapsingHeader(label.Get(config).c_str(), 0); }

			menu.Separator();
			menu.TextUnformatted(label.text.Get(config), nullptr);
			return true;
		}

		void __stdcall Render()
		{
			auto config = RedsandSettings::Get();
			int language = static_cast<int>(config.language);
			menu.SetNextItemWidth(160.0f);
			if (menu.Combo(Label{ { "Language", "语言" }, "redsandLanguage" }.Get(config).c_str(),
				&language, "English\0简体中文\0", -1)) {
				config.language = static_cast<RedsandSettings::Language>(language);
				SaveNow(config);
			}
			const char* description = Text{ "All multipliers default to 1. Changes save automatically. Hover for help; click a swatch to choose a color.", "所有倍率默认 1。修改自动保存；悬停查看说明，点击色块选色。" }.Get(config);
			Describe({ "Choose the menu language.", "选择菜单语言。" }, config, description);
			menu.SameLine(0.0f, -1.0f);
			if (menu.SmallButton(Label{ { "Defaults", "恢复默认" }, "redsandDefaults" }.Get(config).c_str())) {
				RedsandSettings::ResetDefaults();
				RedsandLog::Sync();
				config = RedsandSettings::Get();
			}
			Describe({ "Reset all effects and colors, and turn logging off. Keeps the selected language.",
				"恢复所有效果和颜色的默认设置，并关闭日志；保留所选语言。" }, config, description);
			menu.Separator();
			if (menu.BeginTabBar("RedsandParticleTabs", 0)) {
				if (menu.BeginTabItem(Label{ { "Melee trails", "近战轨迹" }, "redsandMelee" }.Get(config).c_str(), nullptr, 0)) {
					Checkbox({ { "Enable particles", "启用粒子" }, "swordEnabled" }, config.swordEnabled, config,
						{ "Show particles along melee weapon swings.", "挥动近战武器时显示粒子轨迹。" }, description);
					Checkbox({ { "Show Precision trail", "同时显示 Precision 剑影" }, "swordKeepPrecisionTrail" }, config.swordKeepPrecisionTrail, config,
						{ "Show Precision's weapon ribbon alongside particles from the next swing. Precision's own trail setting must also be enabled.",
						  "粒子轨迹与 Precision 剑影同时显示，从下一次挥击生效；也需开启 Precision 自身的剑影设置。" }, description);
					Slider({ { "Brightness", "亮度" }, "swordBrightness" }, config.swordBrightness, 0.0f, RedsandSettings::maxBrightness, config,
						{ "Adjust particle glow. 0 hides the particles; bloom also depends on your graphics settings.",
						  "调整粒子发光亮度，0 隐藏粒子；泛光强弱也受画面设置影响。" }, description);
					Slider({ { "Particle count", "粒子数量" }, "swordDensity" }, config.swordDensity, 0.0f, RedsandSettings::maxDensity, config,
						{ "Adjust trail density. 0 stops new particles; higher counts cost more performance.",
						  "调整轨迹疏密，0 停止发射粒子；数量越多，性能开销越大。" }, description);
					Slider({ { "Particle size", "粒子尺寸" }, "swordSize" }, config.swordSize, RedsandSettings::minSize, RedsandSettings::maxSize, config,
						{ "Adjust the size of each particle. Applies to newly emitted particles.",
						  "调整每颗粒子的大小，作用于新发射的粒子。" }, description);
					Color({ { "Melee particle color", "近战粒子颜色" }, "meleeRGB" }, RedsandSettings::ColorTarget::Melee, config, description);
					if (Fold({ { "Advanced", "高级设置" }, "meleeAdvanced" }, config)) {
						Checkbox({ { "Player", "玩家" }, "swordPlayer" }, config.swordPlayer, config,
							{ "Show melee particles for your character.", "显示玩家的近战粒子。" }, description);
						Checkbox({ { "NPCs", "非玩家角色" }, "swordNPC" }, config.swordNPC, config,
							{ "Show melee particles for other characters, including followers and enemies.", "显示其他角色的近战粒子，包括随从和敌人。" }, description);
						Checkbox({ { "Emit from weapon tip", "从武器尖端发射" }, "swordTip" }, config.swordTip, config,
							{ "Place particles along the weapon tip's path. Uncheck to emit along the blade or weapon head.",
							  "沿武器尖端的运动路径发射粒子；关闭后沿刃部或武器头部发射。" }, description);
						Slider({ { "Spread", "散布宽度" }, "swordSpread" }, config.swordSpread, 0.0f, RedsandSettings::maxSpread, config,
							{ "Lower values keep particles closer to the weapon path. 0 is narrowest, with slight random motion remaining.",
							  "数值越小，粒子越贴近武器轨迹；0 最收束，仍保留少量随机运动。" }, description);
					}
					menu.EndTabItem();
				}
				if (menu.BeginTabItem(Label{ { "Bow trails", "弓箭轨迹" }, "redsandBow" }.Get(config).c_str(), nullptr, 0)) {
					Checkbox({ { "Enable particles", "启用粒子" }, "bowEnabled" }, config.bowEnabled, config,
						{ "Show particles when using bows and crossbows.", "使用弓或弩时显示粒子特效。" }, description);
					Slider({ { "Brightness", "亮度" }, "bowBrightness" }, config.bowBrightness, 0.0f, RedsandSettings::maxBrightness, config,
						{ "Adjust particle glow. 0 hides the particles; bloom also depends on your graphics settings.",
						  "调整粒子发光亮度，0 隐藏粒子；泛光强弱也受画面设置影响。" }, description);
					Slider({ { "Particle count", "粒子数量" }, "bowDensity" }, config.bowDensity, 0.0f, RedsandSettings::maxDensity, config,
						{ "Adjust particle density. 0 stops new particles; higher counts cost more performance.",
						  "调整粒子疏密，0 停止发射粒子；数量越多，性能开销越大。" }, description);
					Color({ { "Bow particle color", "弓箭粒子颜色" }, "bowRGB" }, RedsandSettings::ColorTarget::Bow, config, description);
					if (Fold({ { "Advanced", "高级设置" }, "bowAdvanced" }, config)) {
						Checkbox({ { "Player", "玩家" }, "bowPlayer" }, config.bowPlayer, config,
							{ "Show bow and crossbow particles for your character.", "显示玩家使用弓或弩时的粒子。" }, description);
						Checkbox({ { "NPCs", "非玩家角色" }, "bowNPC" }, config.bowNPC, config,
							{ "Show bow and crossbow particles for other characters, including followers and enemies.", "显示其他角色使用弓或弩时的粒子，包括随从和敌人。" }, description);
						Checkbox({ { "Charge gathering", "蓄力聚集" }, "bowGathering" }, config.bowGathering, config,
							{ "Gather particles around the arrowhead while drawing a bow. Does not apply to crossbows.",
							  "拉弓蓄力时在箭头附近聚集粒子，不影响弩。" }, description);
						Checkbox({ { "Release rings", "发射环" }, "bowRings" }, config.bowRings, config,
							{ "Create a particle ring when an arrow or crossbow bolt is fired.", "射出箭矢或弩矢时生成粒子环。" }, description);
						Checkbox({ { "Flight trails", "飞行轨迹" }, "bowTrails" }, config.bowTrails, config,
							{ "Leave a particle trail behind arrows and crossbow bolts in flight.", "在飞行中的箭矢或弩矢后方生成粒子轨迹。" }, description);
					}
					menu.EndTabItem();
				}
				if (menu.BeginTabItem(Label{ { "Hit / block", "命中与格挡" }, "redsandImpactVFX" }.Get(config).c_str(), nullptr, 0)) {
					Checkbox({ { "Enable hit and block effects", "启用命中与格挡特效" }, "impactEnabled" }, config.impactEnabled, config,
						{ "Enable the effects on this page. Uncheck to restore existing hit and block visuals and hide slash effects; active particles finish fading.",
						  "控制本页所有特效。关闭后恢复原有命中、格挡效果并隐藏斩击特效；已生成粒子继续消散。" }, description);
					Checkbox({ { "Hit sparks and shockwaves", "命中火花与冲击波" }, "impactHit" }, config.impactHit, config,
						{ "Show melee hit sparks and metal or stone impact bursts. Player power attacks and critical hits also produce a shockwave.",
						  "近战命中时显示火花，也替换金属、石头等物体的命中效果；玩家重击或暴击时追加冲击波。" }, description);
					Checkbox({ { "Slash effects", "斩击特效" }, "impactSlash" }, config.impactSlash, config,
						{ "When a blade hits a character, show a cut of light and sparks along the swing direction. Can be combined with hit sparks.",
						  "刃器命中角色时，沿挥击方向显示切光与火花，可与命中火花同时开启。" }, description);
					Checkbox({ { "Block effects", "格挡特效" }, "impactBlock" }, config.impactBlock, config,
						{ "Replace supported block visuals without changing timing or damage. Timed and perfect blocks require a combat mod that provides them.",
						  "替换受支持的格挡效果，不改变判定或伤害；限时与完美格挡需对应战斗模组提供判定。" }, description);
                    if (Fold({ { "Block lightning", "格挡闪电" }, "blockLightningOptions" }, config)) {
                        Checkbox({ { "Add lightning on player blocks", "玩家格挡叠加闪电" }, "blockLightning" }, config.blockLightning, config,
                            { "Add expanding 3D arcs when you successfully block a hit. Keeps existing block effects, sounds and slowdown unchanged. NPC blocks do not trigger it.",
                              "玩家成功挡住攻击时叠加三维电弧，保留原有格挡特效、声音与减速。敌人格挡不触发。" }, description);
                        Checkbox({ { "Chance-based activation", "概率触发" }, "blockLightningRandomTrigger" }, config.blockLightningRandomTrigger, config,
                            { "Roll once per successful block. Turn off to activate on every block. This controls lightning only.",
                              "每次成功格挡抽取一次。关闭后每次格挡都触发，仅控制闪电。" }, description);
                        float percent = config.blockLightningChance*100.f;
                        const bool canDisableChance = menu.BeginDisabled && menu.EndDisabled;
                        if (canDisableChance) menu.BeginDisabled(!config.blockLightningRandomTrigger);
                        if (menu.SliderFloat(Label{ { "Activation chance", "触发概率" }, "blockLightningChance" }.Get(config).c_str(), &percent, 0.f, 100.f, "%.0f%%", 1 << 4)) {
                            config.blockLightningChance = percent/100.f; SaveNow(config);
                        }
                        Describe({ "Used when chance-based activation is on. 0% never triggers; 100% always triggers.",
                                   "开启概率触发时生效。0% 不触发，100% 每次触发。" }, config, description);
                        if (canDisableChance) menu.EndDisabled();
                        Checkbox({ { "Random lightning color", "闪电颜色随机" }, "blockLightningRandomColor" }, config.blockLightningRandomColor, config,
                            { "Choose one RGB color per activation for all arc branches. Turn off to use your default lightning color. Does not recolor trails or other block effects.",
                              "每次触发生成一组 RGB，所有电弧共用。关闭后使用下方的默认闪电颜色，不影响轨迹或其它格挡特效。" }, description);
                        if (menu.ColorEdit3) {
                            if (menu.ColorEdit3(Label{ { "Default lightning color", "闪电默认颜色" }, "blockLightningRGB" }.Get(config).c_str(), config.blockLightningColor.data(), 1 << 5)) {
                                RedsandSettings::Set(config);
                            }
                            if (menu.IsItemDeactivatedAfterEdit && menu.IsItemDeactivatedAfterEdit()) RedsandSettings::FlushPendingSave();
                        } else {
                            const char* channelNames[] = { "R###blockLightningR", "G###blockLightningG", "B###blockLightningB" };
                            menu.TextUnformatted(Text{ "Default lightning color", "闪电默认颜色" }.Get(config), nullptr);
                            for (unsigned k = 0; k < 3; ++k) {
                                if (menu.SliderFloat(channelNames[k], &config.blockLightningColor[k], 0.f, 1.f, "%.3f", 1 << 4)) RedsandSettings::Set(config);
                            }
                        }
                        Describe({ "Click the swatch to preview and choose RGB/HSV. Used on the next activation when random colors are off; your selection is saved while random colors are on.",
                                   "点击色块预览并用 RGB/HSV 选色。关闭颜色随机后，下次触发使用此色；开启随机时仍保留你的选色。" }, config, description);
                        Slider({ { "Lightning glow", "闪电亮度" }, "blockLightningBrightness" }, config.blockLightningBrightness, 0.f, RedsandSettings::maxBrightness, config,
                            { "Adjust lightning glow only. Bloom depends on your graphics setup.", "只调整闪电发光强度，泛光观感取决于画面设置。" }, description);
                        Slider({ { "Lightning reach", "闪电扩散范围" }, "blockLightningSize" }, config.blockLightningSize, .1f, 3.f, config,
                            { "Scale the 3D arcs around the block point. Larger arcs can be hidden behind nearby scenery.",
                              "缩放格挡点周围的三维电弧，附近场景会遮挡电弧。" }, description);
                    }
					Checkbox({ { "Random slash colors", "斩击颜色随机" }, "slashRandomColor" }, config.slashRandomColor, config,
						{ "Choose one random RGB color per attack for the cut of light, slash sparks and the accompanying contact sparks. Brightness is unchanged. Numeric variation is controlled separately. Turn off to use your saved colors.",
						  "每次攻击随机生成一组 RGB，切光与斩击时产生的火花（包括接触点火花）共用。亮度不变；数值随机由另一开关控制。关闭后使用已保存的颜色。" }, description);
                    Checkbox({ { "Random slash values", "斩击数值随机" }, "slashRandomValues" }, config.slashRandomValues, config,
                        { "Vary slash size, spark count, travel, spread and cut length once per attack around your saved settings. Brightness stays fixed; zero count/spread stays zero.",
                          "每次攻击围绕当前设置随机选择斩击尺寸、火花数量、飞距、散布与切光长度。亮度固定；数量或散布为 0 时仍保持为 0。" }, description);
                    float randomPercent = config.slashRandomRange * 100.f;
                    if (menu.SliderFloat(Label{ { "Numeric variation range", "数值浮动范围" }, "slashRandomRange" }.Get(config).c_str(), &randomPercent, 0.f, 100.f, "%.0f%%", 1 << 4)) {
                        config.slashRandomRange = randomPercent / 100.f;
                        RedsandSettings::Set(config);
                    }
                    if (menu.IsItemDeactivatedAfterEdit && menu.IsItemDeactivatedAfterEdit()) { RedsandSettings::FlushPendingSave(); }
                    Describe({ "50% varies each slash setting from 50% to 150% of its saved value, within that setting's limits. 0% disables numeric variation; 100% permits up to double.",
                        "50% 表示各项数值在当前值的 50%～150% 之间变化，并受各参数上下限约束。0% 不浮动，100% 最高可到两倍。" }, config, description);
					Slider({ { "Brightness", "亮度" }, "impactBrightness" }, config.impactBrightness, 0.f, RedsandSettings::maxBrightness, config,
						{ "Adjust glow for hit, slash and block effects. Bloom also depends on your graphics settings.",
						  "调整命中、斩击与格挡特效的发光亮度；泛光强弱也受画面设置影响。" }, description);
					Slider({ { "Overall particle count", "总体粒子数量" }, "impactDensity" }, config.impactDensity, 0.f, RedsandSettings::maxDensity, config,
						{ "Adjust particle density for this page. 0 stops new particles; higher counts cost more performance.",
						  "调整本页特效的粒子疏密，0 停止发射粒子；数量越多，性能开销越大。" }, description);
					Slider({ { "Effect size", "效果尺寸" }, "impactSize" }, config.impactSize, RedsandSettings::minSize, RedsandSettings::maxSize, config,
						{ "Scale the whole effect, including its width and reach.", "缩放整组效果，同时影响宽度与扩散距离。" }, description);
					Slider({ { "Slash particle count", "斩击粒子数量" }, "slashDensity" }, config.slashDensity, 0.f, RedsandSettings::maxSlashDensity, config,
						{ "Adjust slash spark density together with the overall count, up to the default maximum. 0 hides slash sparks without hiding the cut of light.",
						  "调整斩击火花数量，与总体数量共同作用，最高为默认数量；0 隐藏火花，不影响切光。" }, description);
					Slider({ { "Slash spread width", "斩击散布宽度" }, "slashSpread" }, config.slashSpread, 0.f, RedsandSettings::maxSlashSpread, config,
						{ "Lower values keep sparks closer to the slash line. 0 sends them out in parallel. Applies to newly emitted particles.",
						  "数值越小，火花越贴近斩击直线；0 沿斩击方向平行飞出。作用于新发射的粒子。" }, description);
					Slider({ { "Slash spark travel", "斩击火花飞行距离" }, "slashDistance" }, config.slashDistance, RedsandSettings::minSlashDistance, RedsandSettings::maxSlashDistance, config,
						{ "Increase how far slash sparks fly by increasing their speed. Does not change particle size or fade time.",
						  "调整斩击火花的飞行距离，数值越大飞得越快、越远；不改变粒子大小或消散时间。" }, description);
					Slider({ { "Cut length", "切光长度" }, "cutLength" }, config.cutLength, RedsandSettings::minCutLength, RedsandSettings::maxCutLength, config,
						{ "Adjust the length of the cut of light along the swing. Does not change its width or the sparks.",
						  "调整沿挥击方向延伸的切光长度，不改变宽度或火花轨迹。" }, description);
					menu.Separator();
					Color({ { "Hit sparks", "命中火花颜色" }, "hitRGB" }, RedsandSettings::ColorTarget::Hit, config, description);
					Color({ { "Slash sparks", "斩击火花颜色" }, "slashSparksRGB" }, RedsandSettings::ColorTarget::SlashSparks, config, description);
					Color({ { "Cut of light", "切光颜色" }, "cutRGB" }, RedsandSettings::ColorTarget::Cut, config, description);
					Color({ { "Block effects", "格挡特效颜色" }, "blockRGB" }, RedsandSettings::ColorTarget::Block, config, description);
					Color({ { "Shockwave rim", "冲击波边缘颜色" }, "waveRGB" }, RedsandSettings::ColorTarget::Wave, config, description);
					if (Fold({ { "Combat mod compatibility", "战斗模组兼容" }, "impactAdvanced" }, config)) {
						ValhallaRing(config, description);
					}
					const auto state = RedsandImpactVFX::GetStatus();
					if (state.availability != RedsandImpactVFX::Availability::Ready) {
						const auto unavailable = state.availability == RedsandImpactVFX::Availability::InvalidRoutes ?
							Text{ "Effect resources could not be validated. Check the mod installation and file conflicts.", "特效资源校验失败，请检查安装是否完整及文件覆盖。" } :
							Text{ "Effect resources are not loaded. Check the mod installation and restart the game.", "特效资源尚未载入，请检查模组安装并重新启动游戏。" };
						menu.TextUnformatted(unavailable.Get(config), nullptr);
					}
					menu.EndTabItem();
				}
				menu.EndTabBar();
			}
			if (Fold({ { "Troubleshooting", "排错信息" }, "redsandDiagnosticDetails" }, config)) {
				Checkbox({ { "Diagnostic logging", "诊断日志" }, "redsandDiagnostics" }, config.diagnostics, config,
					{ "Record effect activity in RedsandParticles.log for troubleshooting. Leave off during normal play.",
					  "将特效运行信息写入 RedsandParticles.log，供排查问题使用；正常游玩时建议关闭。" }, description);
				const auto state = RedsandImpactVFX::GetStatus();
				const auto summary = std::string(Text{ "Available effects: ", "可用效果：" }.Get(config)) +
					std::to_string(state.resolved) + "/" + std::to_string(state.definitions) +
					Text{ "  Optional effects absent: ", "  可选效果未安装：" }.Get(config) + std::to_string(state.missing) +
					Text{ "  Conflicts: ", "  覆盖冲突：" }.Get(config) + std::to_string(state.conflicts);
				menu.TextUnformatted(summary.c_str(), nullptr);
				menu.TextUnformatted(Text{ "Optional effects may be absent when their combat mods are not installed.",
					"未安装相应战斗模组时，可选效果缺少属于正常情况。" }.Get(config), nullptr);
				if (state.resourceFailures || state.wrongTypes) {
					const auto failures = std::string(Text{ "Missing model resources: ", "模型资源不可用：" }.Get(config)) + std::to_string(state.resourceFailures) +
						Text{ "  Incompatible records: ", "  不兼容记录：" }.Get(config) + std::to_string(state.wrongTypes);
					menu.TextUnformatted(failures.c_str(), nullptr);
				}
				if (!state.detail.empty()) { menu.TextUnformatted(state.detail.c_str(), nullptr); }
				const int desired = 42;
				const bool acknowledged = state.appliedIndex == desired && state.appliedEnabled == config.impactEnabled &&
					state.appliedHit == config.impactHit && state.appliedSlash == config.impactSlash && state.appliedBlock == config.impactBlock;
				menu.TextUnformatted((acknowledged ? Text{ "Settings applied; changes appear on the next effect.", "设置已应用，从下一次触发生效。" } :
					Text{ "Settings are waiting to be applied.", "设置等待应用。" }).Get(config), nullptr);
				if (state.slashCallback) {
					const auto slash = std::string(Text{ "Slash hits / effects / failures: ", "斩击命中 / 生成 / 失败：" }.Get(config)) +
						std::to_string(state.slashHits) + "/" + std::to_string(state.slashSpawned) + "/" + std::to_string(state.slashFailed) +
						Text{ "  Estimated directions: ", "  估算方向：" }.Get(config) + std::to_string(state.slashFallbacks);
					menu.TextUnformatted(slash.c_str(), nullptr);
				} else { menu.TextUnformatted(Text{ "Slash detection is unavailable.", "斩击检测不可用。" }.Get(config), nullptr); }
			}
			menu.Separator();
			menu.PushTextWrapPos(0.0f);
			menu.TextUnformatted(description, nullptr);
			RedsandSettings::TickPendingSave();
			if (RedsandSettings::SaveFailed()) {
				menu.TextUnformatted(Text{ "Settings could not be saved. Changes still apply; saving will retry automatically.",
					"设置保存失败，修改仍已生效，将自动重试保存。" }.Get(config), nullptr);
			}
			menu.PopTextWrapPos();
		}
	}

	void Register()
	{
		if (registered) { return; }
		if (!menu.Load()) {
			RedsandLog::warn("Redsand Particles menu unavailable: install SKSE Menu Framework 2 or 3. Particle settings still load from the INI.");
			return;
		}
		menu.AddSectionItem("Redsand Particles/Particle Settings", Render);
		registered = true;
		RedsandLog::info("Registered Redsand Particles / Particle Settings in SKSE Menu Framework");
	}
}
