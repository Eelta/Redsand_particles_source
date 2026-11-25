#include "RedsandMenu.h"
#include "RedsandLog.h"
#include "RedsandSettings.h"
#include "RedsandEnchantmentProfiles.h"
#include "RedsandImpactVFX.h"
#include "vendor/SKSEMenuFrameworkMinimal.h"

#include <string>
#include <algorithm>

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
			Text help, const char*& description, bool seconds = false)
		{
			constexpr int alwaysClamp = 1 << 4;
			const auto format = seconds ? Text{ "%.3f s", "%.3f 秒" }.Get(config) : Text{ "%.2fx", "%.2f 倍" }.Get(config);
			if (menu.SliderFloat(label.Get(config).c_str(), &value, minimum, maximum, format, alwaysClamp)) {
				RedsandSettings::Set(config);
			}
			if (menu.IsItemDeactivatedAfterEdit && menu.IsItemDeactivatedAfterEdit()) { RedsandSettings::FlushPendingSave(); }
			Describe(help, config, description);
		}

		void Color(Label label, RedsandSettings::EffectColor& color, bool weaponColor, RedsandSettings::Config& config, const char*& description)
		{
			const auto caption = std::string(Text{ "Custom ", "自定义" }.Get(config)) + label.text.Get(config) + "###" + label.id + "Enabled";
			if (menu.Checkbox(caption.c_str(), &color.custom)) { SaveNow(config); }
			const Text colorHelp = weaponColor ?
				Text{ "Use a fixed effect color or inherit the enchantment's color. Independent enchantment settings take priority.",
					"勾选使用固定粒子颜色；取消后跟随附魔，无有效附魔时使用默认蓝色。附魔类别的独立设置优先。" } :
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

		void Color(Label label, RedsandSettings::ColorTarget target, RedsandSettings::Config& config, const char*& description)
		{
			Color(label, config.colors[static_cast<unsigned>(target)],
				target == RedsandSettings::ColorTarget::Melee || target == RedsandSettings::ColorTarget::Bow, config, description);
		}

		template <class Settings>
		bool SwingDurationSlider(Label label, Settings& settings, const RedsandSettings::Config& config, const char*& description)
		{
			float duration = RedsandSettings::SwingDuration(settings);
			const bool changed = menu.SliderFloat(label.Get(config).c_str(), &duration, RedsandSettings::minSwingDuration,
				RedsandSettings::maxSwingDuration, Text{"%.3f s", "%.3f 秒"}.Get(config), 1 << 4);
			if (changed) { RedsandSettings::SetSwingDuration(settings, duration); }
			Describe({"Total time until the last grains fade. The arc fades first.", "从出现到散点淡完的总时长。主弧先淡出，散点随后消散。"}, config, description);
			return changed;
		}

		constexpr Text swingSpeedHelp{"Adjust how far the dots drift and settle. 0 keeps them at their birth positions.", "控制圆点沿弧漂移和尾迹飘落的幅度，0 时留在出生位置。"};
		bool DotDensitySlider(const char* id, float& value, const RedsandSettings::Config& config, const char*& description)
		{
			constexpr int alwaysClamp = 1 << 4, logarithmic = 1 << 5;
			const auto caption = Label{{"Dot density", "圆点疏密"}, id}.Get(config);
			const bool changed = menu.SliderFloat(caption.c_str(), &value, 0.f, RedsandSettings::maxRedsandDotDensity,
				Text{"%.3fx", "%.3f 倍"}.Get(config), alwaysClamp | logarithmic);
			Describe({"Higher values emit more closely spaced dots. 0 stops emission.",
				"数值越大，圆点越多、间距越小，0 停止发射。"}, config, description);
			return changed;
		}
		constexpr Text swingWidthHelp{"Spread grains to either side of the arc. 0 is a single row.", "控制散点向弧线两侧展开的宽度，0 为贴着弧线的单行。"};
		constexpr Text swingArcBrightnessHelp{"Set the main arc glow. 0 hides the arc.", "调整主弧亮度，0 隐藏主弧。"};
		bool ArcWidthSlider(Label label, float& width, const RedsandSettings::Config& config, const char*& description)
		{
			float multiplier = width / RedsandSettings::defaultRibbonWidth;
			const bool changed = menu.SliderFloat(label.Get(config).c_str(), &multiplier, 0.f,
				RedsandSettings::maxRibbonWidthMultiplier, Text{"%.2fx", "%.2f 倍"}.Get(config), 1 << 4);
			if (changed) { width = multiplier * RedsandSettings::defaultRibbonWidth; }
			Describe({"Scale the main arc width. 1 is the default, 0 hides the arc.",
				"调整主弧宽度，1 为默认，0 隐藏主弧。"}, config, description);
			return changed;
		}

		constexpr Text swingParticleBrightnessHelp{"Set the glow of the grains. 0 hides the grains.", "调整散点亮度，0 隐藏散点。"};
		constexpr Text redsandDotBrightnessHelp{"Adjust only the Redsand dots. 0 hides them.", "只调整红沙圆点的亮度，0 隐藏圆点。"};
		constexpr Text swingDensityHelp{"Set the number of fine particles. 0 stops emission.", "调整散点数量，0 停止发射。"};
		constexpr Text swingSizeHelp{"Set the size of each newly emitted grain.", "调整新发射的每颗散点的尺寸。"};

		bool MeleeMode(bool& redsand, const char* id, const RedsandSettings::Config& config)
		{
			int selection = redsand ? 1 : 0;
			const auto choices = config.language == RedsandSettings::Language::Chinese ?
				"Precision 剑影\0红沙模式\0" : "Precision trail\0Redsand mode\0";
			if (!menu.Combo(Label{{"Trail type", "剑影类型"}, id}.Get(config).c_str(), &selection, choices, -1)) { return false; }
			redsand = selection == 1;
			return true;
		}

		void SwingTrailOptions(RedsandSettings::Config& config, const char*& description)
		{
			auto& style = config.redsand;
			Slider({{"Arc brightness", "主弧亮度"}, "redsandArcBrightness"}, style.arcBrightness, 0.f, RedsandSettings::maxBrightness, config, swingArcBrightnessHelp, description);
			Slider({{"Redsand dot brightness", "红沙圆点亮度"}, "redsandParticleBrightness"}, style.particleBrightness, 0.f, RedsandSettings::maxBrightness, config, redsandDotBrightnessHelp, description);
			if (ArcWidthSlider({{"Redsand arc width", "红沙弧线宽度"}, "swordRibbonWidth"}, config.swordRibbonWidth, config, description)) {
				RedsandSettings::Set(config);
			}
			if (menu.IsItemDeactivatedAfterEdit && menu.IsItemDeactivatedAfterEdit()) { RedsandSettings::FlushPendingSave(); }
			if (SwingDurationSlider({{"Trail duration", "轨迹持续时间"}, "swordTrailDuration"}, config, config, description)) {
				RedsandSettings::Set(config);
			}
			if (menu.IsItemDeactivatedAfterEdit && menu.IsItemDeactivatedAfterEdit()) { RedsandSettings::FlushPendingSave(); }
			Slider({ { "Speed along arc", "沿弧速度" }, "swordTrailMomentum" },config.swordTrailMomentum,0.f,RedsandSettings::maxSwingSpeed,config,swingSpeedHelp,description);
			Slider({{"Particle width", "粒子宽度"}, "swordParticleWidth"}, config.swordParticleWidth, 0.f,
				RedsandSettings::maxSwingParticleWidth, config, swingWidthHelp, description);
			if (DotDensitySlider("redsandDensity", style.density, config, description)) { RedsandSettings::Set(config); }
			if (menu.IsItemDeactivatedAfterEdit && menu.IsItemDeactivatedAfterEdit()) { RedsandSettings::FlushPendingSave(); }
			Slider({{"Particle size", "粒子尺寸"}, "redsandSize"}, style.size, RedsandSettings::minSize, RedsandSettings::maxSize, config, swingSizeHelp, description);
			Color({{"Redsand color", "红沙颜色"}, "redsandRGB"}, style.color, true, config, description);
			Checkbox({{"Player", "玩家"}, "redsandPlayer"}, style.player, config, {"Show Redsand for your character.", "显示玩家的红沙轨迹。"}, description);
			Checkbox({{"NPCs", "非玩家角色"}, "redsandNPC"}, style.npc, config, {"Show Redsand for other characters.", "显示其他角色的红沙轨迹。"}, description);
		}

		bool Fold(Label label, const RedsandSettings::Config& config)
		{
			if (menu.CollapsingHeader) { return menu.CollapsingHeader(label.Get(config).c_str(), 0); }

			menu.Separator();
			menu.TextUnformatted(label.text.Get(config), nullptr);
			return true;
		}

		int EnchantmentScope(bool bow, const RedsandSettings::Config& config, const char*& description)
		{
			static int meleeScope = 0;
			static int bowScope = 0;
			auto& selected = bow ? bowScope : meleeScope;
			const char* labels = config.language == RedsandSettings::Language::Chinese ?
				"全局\0无附魔／附魔电量不足\0火焰\0冰霜\0雷电\0原版绿色 Shader＋毒药\0原版紫色 Shader\0原版蓝色 Shader\0原版红色 Shader\0束缚武器\0太阳／破晓\0其他／未识别\0" :
				"Global\0Unenchanted / depleted\0Fire\0Frost\0Shock\0Vanilla green shader / poison\0Vanilla purple shader\0Vanilla blue shader\0Vanilla red shader\0Bound weapons\0Sun / Dawnbreaker\0Other / unrecognized\0";
			menu.Combo(Label{{"Scope", "设置范围"}, bow ? "bowEnchantmentScope" : "meleeEnchantmentScope"}.Get(config).c_str(), &selected, labels, 12);
			Describe({"Global settings apply until a matching enchantment override is enabled.", "全局设置默认生效，启用对应附魔的覆盖后才使用分类设置。"}, config, description);
			return std::clamp(selected, 0, static_cast<int>(RedsandEnchantmentProfiles::categoryKeys.size())) - 1;
		}

		void EnchantmentOptions(RedsandSettings::Config& config, const char*& description, int category, bool bow)
		{
			const auto key = std::string(RedsandEnchantmentProfiles::categoryKeys[category]);
			const auto* saved = config.enchantments.Find(key);
			auto appearance = saved ? *saved : RedsandSettings::EnchantmentAppearance{};
			bool changed = false;
			const auto check = [&](Label label, bool& value, Text help) {
				changed |= menu.Checkbox(label.Get(config).c_str(), &value); Describe(help, config, description);
			};
			const auto slider = [&](Label label, float& value, float minimum, float maximum, Text help, bool seconds = false) {
				changed |= menu.SliderFloat(label.Get(config).c_str(), &value, minimum, maximum, seconds ? Text{ "%.3f s", "%.3f 秒" }.Get(config) : Text{ "%.2fx", "%.2f 倍" }.Get(config), 1 << 4);
				Describe(help, config, description);
			};
			const auto color = [&](RedsandSettings::EffectColor& value, const char* id) {
				int mode = value.custom ? 1 : 0;
				const auto caption = std::string(Text{ "Color mode", "颜色模式" }.Get(config)) + "###" + id + "mode";
				if (menu.Combo(caption.c_str(), &mode, config.language == RedsandSettings::Language::Chinese ? "原配色\0指定颜色\0" : "Original color\0Custom color\0", -1)) {
					value.custom = mode == 1; changed = true;
				}
				Describe({ "Original color preserves the existing automatic enchantment palette. Custom color replaces it for this category only. Requires separate category settings.",
					"原配色保留现有自动附魔配色；指定颜色只替换当前类别的粒子颜色。需启用此类别独立设置。" }, config, description);
				if (value.custom) {
					if (menu.ColorEdit3) { changed |= menu.ColorEdit3((std::string("RGB###") + id).c_str(), value.rgb.data(), 1 << 5); }
					else {
						for (unsigned i = 0; i < 3; ++i) {
							const auto caption = std::string(i == 0 ? "R" : i == 1 ? "G" : "B") + "###" + id + std::to_string(i);
							changed |= menu.SliderFloat(caption.c_str(), &value.rgb[i], 0.f, 1.f, "%.3f", 1 << 4);
						}
					}
					Describe({ "Choose a particle color for this category. Brightness has its own setting.", "选择此类别的粒子颜色，亮度单独调整。" }, config, description);
				}
			};
			check({ { "Use independent category settings", "使用此类别独立设置" }, "categoryEnabled" }, appearance.enabled,
				{ "Use the melee and bow settings below independently of the main pages, including enable and actor scope. Turn off to inherit the main pages.",
				  "使用下方独立的近战和弓箭参数，包括开关与角色范围，不受主页数值限制；关闭后沿用主页。" });
			if (appearance.enabled && !bow) {
				changed |= MeleeMode(appearance.swordReleasedTrail, "categoryMeleeMode", config);
				check({{"Add ordinary particles", "添加普通粒子"}, "categorySwordEnabled"}, appearance.swordEnabled,
					{"Add weapon-shaped particles alongside the selected trail.", "在所选剑影上叠加随武器变化的普通粒子。"});
				if (appearance.swordReleasedTrail) {
					menu.Separator();
					menu.TextUnformatted(Text{"Redsand mode", "红沙模式"}.Get(config), nullptr);
					auto& style = appearance.redsand;
					slider({{"Arc brightness", "主弧亮度"}, "categoryArcBrightness"}, style.arcBrightness, 0.f, RedsandSettings::maxBrightness, swingArcBrightnessHelp);
					slider({{"Redsand dot brightness", "红沙圆点亮度"}, "categoryRedBrightness"}, style.particleBrightness, 0.f, RedsandSettings::maxBrightness, redsandDotBrightnessHelp);
					changed |= ArcWidthSlider({{"Arc width", "弧线宽度"}, "categoryArcWidth"}, appearance.swordRibbonWidth, config, description);
					changed |= SwingDurationSlider({{"Trail duration", "轨迹持续时间"}, "categoryTrailDuration"}, appearance, config, description);
					slider({{"Speed along arc", "沿弧速度"}, "categoryArcDrift"}, appearance.swordTrailMomentum, 0.f, RedsandSettings::maxSwingSpeed, swingSpeedHelp);
					slider({{"Particle width", "粒子宽度"}, "categoryParticleWidth"}, appearance.swordParticleWidth, 0.f, RedsandSettings::maxSwingParticleWidth, swingWidthHelp);
					changed |= DotDensitySlider("categoryRedDensity", style.density, config, description);
					slider({{"Particle size", "粒子尺寸"}, "categoryRedSize"}, style.size, RedsandSettings::minSize, RedsandSettings::maxSize, swingSizeHelp);
					color(style.color, "categoryRedsandColor");
					check({{"Player", "玩家"}, "categoryRedsandPlayer"}, style.player, {"Show Redsand for your character.", "显示玩家的红沙轨迹。"});
					check({{"NPCs", "非玩家角色"}, "categoryRedsandNPC"}, style.npc, {"Show Redsand for other characters.", "显示其他角色的红沙轨迹。"});
				}
				if (appearance.swordEnabled) {
					menu.Separator();
					menu.TextUnformatted(Text{"Ordinary particles", "普通粒子"}.Get(config), nullptr);
					slider({{"Ordinary particle brightness", "普通粒子亮度"}, "categorySwordBrightness"}, appearance.brightness, 0.f, RedsandSettings::maxBrightness, swingParticleBrightnessHelp);
					slider({{"Particle count", "粒子数量"}, "categorySwordDensity"}, appearance.density, 0.f, RedsandSettings::maxDensity, swingDensityHelp);
					slider({{"Particle size", "粒子尺寸"}, "categorySwordSize"}, appearance.size, RedsandSettings::minSize, RedsandSettings::maxSize, swingSizeHelp);
					slider({{"Spread", "散布宽度"}, "categorySwordSpread"}, appearance.spread, 0.f, RedsandSettings::maxSpread,
						{"Adjust ordinary particle scatter.", "调整普通近战粒子的散布。"});
					color(appearance.color, "categoryMeleeColor");
					check({{"Player", "玩家"}, "categorySwordPlayer"}, appearance.swordPlayer, {"Show particles for your character.", "显示玩家的普通粒子。"});
					check({{"NPCs", "非玩家角色"}, "categorySwordNPC"}, appearance.swordNPC, {"Show particles for other characters.", "显示其他角色的普通粒子。"});
					check({{"Emit from weapon tip", "从武器尖端发射"}, "categorySwordTip"}, appearance.swordTip,
						{"Uncheck to emit along the blade or weapon head.", "关闭后沿刃部或武器头部发射。"});
				}
			}
			if (appearance.enabled && bow) {
				check({ { "Enable particles", "启用粒子" }, "categoryBowEnabled" }, appearance.bowEnabled, { "Enable bow and crossbow particles for this category.", "启用此类别的弓和弩粒子。" });
				check({ { "Player", "玩家" }, "categoryBowPlayer" }, appearance.bowPlayer, { "Show this category's bow particles for the player.", "显示玩家使用此类别时的弓箭粒子。" });
				check({ { "NPCs", "非玩家角色" }, "categoryBowNPC" }, appearance.bowNPC, { "Show this category's bow particles for NPCs.", "显示其他角色使用此类别时的弓箭粒子。" });
				color(appearance.bowColor, "categoryBowColor");
				slider({ { "Brightness", "亮度" }, "categoryBowBrightness" }, appearance.bowBrightness, 0.f, RedsandSettings::maxBrightness, { "Independent bow brightness. Does not change melee particles.", "独立弓箭亮度，不影响近战粒子。" });
				slider({ { "Particle count", "粒子数量" }, "categoryBowDensity" }, appearance.bowDensity, 0.f, RedsandSettings::maxDensity, { "Independent bow count. 0 stops new emission.", "独立弓箭数量，0 停止新发射。" });
				slider({ { "Particle size", "粒子尺寸" }, "categoryBowSize" }, appearance.bowSize, RedsandSettings::minSize, RedsandSettings::maxSize, { "Independent bow particle size.", "独立弓箭粒子尺寸。" });
				check({ { "Charge gathering", "蓄力聚集" }, "categoryBowGather" }, appearance.bowGathering, { "Gather particles while drawing a bow. Does not apply to crossbows.", "拉弓时聚集粒子，不影响弩。" });
				check({ { "Release rings", "发射环" }, "categoryBowRing" }, appearance.bowRings, { "Create a ring when an arrow or bolt is fired.", "发射箭矢或弩矢时生成粒子环。" });
				check({ { "Flight trails", "飞行轨迹" }, "categoryBowTrail" }, appearance.bowTrails, { "Leave particles behind flying arrows and bolts.", "在飞行箭矢和弩矢后方生成粒子轨迹。" });
			}
			if (menu.SmallButton(Label{ { "Reset this category", "重置此类别" }, "resetEnchantment" }.Get(config).c_str())) { appearance = {}; changed = true; }
			Describe({ "Reset this category and disable its independent settings. Other categories and main settings are unchanged.", "重置并关闭此类别独立设置，其他类别和主页设置不变。" }, config, description);
			if (changed) { config.enchantments.Set(key, appearance); RedsandSettings::Set(config); }
			if (!appearance.enabled) {
				menu.TextUnformatted(Text{"This category uses the global settings.", "此类别当前使用全局设置。"}.Get(config), nullptr);
			}
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
			const char* description = Text{ "Changes save automatically. Hover over a control for help; click a swatch to choose a color.", "修改自动保存；悬停在控件上查看对应说明，点击色块选色。" }.Get(config);
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
					const int category = EnchantmentScope(false, config, description);
					if(category>=0) {
						EnchantmentOptions(config, description, category, false);
					} else {
						if (MeleeMode(config.swordReleasedTrail, "swordMeleeMode", config)) { SaveNow(config); }
						Checkbox({{"Add ordinary particles", "添加普通粒子"}, "swordEnabled"}, config.swordEnabled, config,
							{"Add weapon-shaped particles alongside the selected trail.", "在所选剑影上叠加随武器变化的普通粒子。"}, description);
						if (config.swordReleasedTrail) {
							menu.Separator();
							menu.TextUnformatted(Text{"Redsand mode", "红沙模式"}.Get(config), nullptr);
							SwingTrailOptions(config, description);
						}
						if (config.swordEnabled) {
							menu.Separator();
							menu.TextUnformatted(Text{"Ordinary particles", "普通粒子"}.Get(config), nullptr);
							Slider({{"Ordinary particle brightness", "普通粒子亮度"}, "swordBrightness"}, config.swordBrightness, 0.f, RedsandSettings::maxBrightness, config, swingParticleBrightnessHelp, description);
							Slider({{"Particle count", "粒子数量"}, "swordDensity"}, config.swordDensity, 0.f, RedsandSettings::maxDensity, config, swingDensityHelp, description);
							Slider({{"Particle size", "粒子尺寸"}, "swordSize"}, config.swordSize, RedsandSettings::minSize, RedsandSettings::maxSize, config, swingSizeHelp, description);
							Slider({{"Spread", "散布宽度"}, "swordSpread"}, config.swordSpread, 0.f, RedsandSettings::maxSpread, config,
								{"Adjust ordinary particle scatter.", "调整普通近战粒子的散布。"}, description);
							Color({{"Melee particle color", "近战粒子颜色"}, "meleeRGB"}, RedsandSettings::ColorTarget::Melee, config, description);
							Checkbox({{"Player", "玩家"}, "swordPlayer"}, config.swordPlayer, config, {"Show particles for your character.", "显示玩家的普通粒子。"}, description);
							Checkbox({{"NPCs", "非玩家角色"}, "swordNPC"}, config.swordNPC, config, {"Show particles for other characters.", "显示其他角色的普通粒子。"}, description);
							Checkbox({{"Emit from weapon tip", "从武器尖端发射"}, "swordTip"}, config.swordTip, config,
								{"Uncheck to emit along the blade or weapon head.", "关闭后沿刃部或武器头部发射。"}, description);
						}
					}
					menu.EndTabItem();
				}
				if (menu.BeginTabItem(Label{ { "Bow trails", "弓箭轨迹" }, "redsandBow" }.Get(config).c_str(), nullptr, 0)) {
					const int category = EnchantmentScope(true, config, description);
					if(category>=0) {
						EnchantmentOptions(config, description, category, true);
					} else {
						Checkbox({ { "Enable particles", "启用粒子" }, "bowEnabled" }, config.bowEnabled, config,
							{ "Show particles when using bows and crossbows.", "使用弓或弩时显示粒子特效。" }, description);
						Slider({ { "Brightness", "亮度" }, "bowBrightness" }, config.bowBrightness, 0.0f, RedsandSettings::maxBrightness, config,
							{ "Adjust particle glow. 0 hides the particles; bloom also depends on your graphics settings.",
							  "调整粒子发光亮度，0 隐藏粒子；泛光强弱也受画面设置影响。" }, description);
						Slider({ { "Particle count", "粒子数量" }, "bowDensity" }, config.bowDensity, 0.0f, RedsandSettings::maxDensity, config,
							{ "Adjust particle density. 0 stops new particles; higher counts cost more performance.",
							  "调整粒子疏密，0 停止发射粒子；数量越多，性能开销越大。" }, description);
						Color({ { "Bow particle color", "弓箭粒子颜色" }, "bowRGB" }, RedsandSettings::ColorTarget::Bow, config, description);
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
					Checkbox({ { "Hit sparks", "命中火花" }, "impactHit" }, config.impactHit, config,
						{ "Show melee hit sparks and metal or stone impact bursts.",
						  "近战命中时显示火花，也替换金属、石头等物体的命中效果。" }, description);
					Slider({ { "Hit particle count", "受击粒子数量" }, "hitDensity" }, config.hitDensity, 0.f, RedsandSettings::maxDensity, config,
						{ "Adjust the sparks emitted when hitting characters, stone, or metal. 0 stops emission.",
						  "调整击中人物、石头或金属时的火花数量，0 停止发射。" }, description);
					Slider({ { "Hit particle size", "受击粒子大小" }, "hitSize" }, config.hitSize, RedsandSettings::minSize, RedsandSettings::maxSize, config,
						{ "Scale the hit spark burst, including spark size and reach.",
						  "缩放受击火花，包括火花的大小和扩散距离。" }, description);
					Slider({ { "Hit particle brightness", "受击粒子亮度" }, "hitBrightness" }, config.hitBrightness, 0.f, RedsandSettings::maxBrightness, config,
						{ "Adjust glow for hit sparks. Bloom also depends on your graphics settings.",
						  "调整受击火花的发光亮度，泛光强弱也受画面设置影响。" }, description);
					Color({ { "Hit sparks", "命中火花颜色" }, "hitRGB" }, RedsandSettings::ColorTarget::Hit, config, description);
					menu.Separator();
					Checkbox({ { "Slash effects", "斩击特效" }, "impactSlash" }, config.impactSlash, config,
						{ "When a blade hits a character, show a cut of light along the swing direction. Can be combined with hit sparks.",
						  "刃器命中角色时，沿挥击方向显示切光，可与命中火花同时开启。" }, description);
					Slider({ { "Cut brightness", "切线亮度" }, "cutBrightness" }, config.cutBrightness, 0.f, RedsandSettings::maxBrightness, config,
						{ "Adjust the glow of the cut line only.", "只调整斩击切线的发光亮度。" }, description);
					Slider({ { "Cut length", "切光长度" }, "cutLength" }, config.cutLength, RedsandSettings::minCutLength, RedsandSettings::maxCutLength, config,
						{ "Adjust the length of the cut of light along the swing. Does not change its width or the sparks.",
						  "调整沿挥击方向延伸的切光长度，不改变宽度或火花轨迹。" }, description);
					Slider({ { "Cut thickness", "斩击线粗细" }, "cutWidth" }, config.cutWidth, RedsandSettings::minCutWidth, RedsandSettings::maxCutWidth, config,
						{ "Scale the thickness of the cut of light. 1.0 is the mesh's original width.",
						  "调整斩击线的粗细。1.0 为模型的基准线宽。" }, description);
					Color({ { "Cut of light", "切光颜色" }, "cutRGB" }, RedsandSettings::ColorTarget::Cut, config, description);
					Checkbox({ { "Random slash colors", "斩击颜色随机" }, "slashRandomColor" }, config.slashRandomColor, config,
						{ "Choose one random RGB color per attack for the cut of light and its paired contact sparks. Brightness is unchanged. Numeric variation is controlled separately. Turn off to use your saved colors.",
						  "每次攻击随机生成一组 RGB，切光与配对的命中火花共用。亮度不变；数值随机由另一开关控制。关闭后使用已保存的颜色。" }, description);
                    Checkbox({ { "Random slash values", "斩击数值随机" }, "slashRandomValues" }, config.slashRandomValues, config,
                        { "Vary slash size, cut length and the density of paired hit sparks once per attack.",
                          "每次攻击随机调整斩击尺寸、切光长度和配对命中火花的密度。" }, description);
                    float randomPercent = config.slashRandomRange * 100.f;
                    if (menu.SliderFloat(Label{ { "Numeric variation range", "数值浮动范围" }, "slashRandomRange" }.Get(config).c_str(), &randomPercent, 0.f, 100.f, "%.0f%%", 1 << 4)) {
                        config.slashRandomRange = randomPercent / 100.f;
                        RedsandSettings::Set(config);
                    }
                    if (menu.IsItemDeactivatedAfterEdit && menu.IsItemDeactivatedAfterEdit()) { RedsandSettings::FlushPendingSave(); }
                    Describe({ "50% varies each slash setting from 50% to 150% of its saved value, within that setting's limits. 0% disables numeric variation; 100% permits up to double.",
                        "50% 表示各项数值在当前值的 50%～150% 之间变化，并受各参数上下限约束。0% 不浮动，100% 最高可到两倍。" }, config, description);
					menu.Separator();
					Checkbox({ { "Block sparks", "格挡火花" }, "impactBlock" }, config.impactBlock, config,
						{ "Show sparks at the point of contact on successful blocks.", "成功格挡时从接触点散出火花。" }, description);
					Slider({ { "Block sparks brightness", "格挡火花亮度" }, "impactBrightness" }, config.impactBrightness, 0.f, RedsandSettings::maxBrightness, config,
						{ "Adjust the glow of the block sparks.", "调整格挡火花的发光亮度。" }, description);
					Slider({ { "Block spark count", "格挡火花数量" }, "impactDensity" }, config.impactDensity, 0.f, RedsandSettings::maxDensity, config,
						{ "Adjust the number of block sparks. 0 stops emission.", "调整格挡火花数量，0 停止发射。" }, description);
					Slider({ { "Block and cut scale", "格挡与切线整体尺寸" }, "impactSize" }, config.impactSize, RedsandSettings::minSize, RedsandSettings::maxSize, config,
						{ "Scale block effects and the cut line, including their width and reach.",
						  "整体缩放格挡特效与切线，同时影响宽度和范围。" }, description);
					Color({ { "Block sparks", "格挡火花颜色" }, "blockRGB" }, RedsandSettings::ColorTarget::Block, config, description);
					menu.Separator();
					Checkbox({ { "Add lightning on player blocks", "玩家格挡叠加闪电" }, "blockLightning" }, config.blockLightning, config,
					    { "Add 3D arcs on successful player blocks. An installed sparks companion uses the same trigger and color.",
					      "玩家成功格挡时叠加三维电弧。装有配套火花资源时，火花和电弧共用触发概率与颜色。" }, description);
					float percent = config.blockLightningRandomTrigger ? config.blockLightningChance * 100.f : 100.f;
					if (menu.SliderFloat(Label{ { "Lightning chance", "闪电触发概率" }, "blockLightningChance" }.Get(config).c_str(), &percent, 0.f, 100.f, "%.0f%%", 1 << 4)) {
						config.blockLightningRandomTrigger = percent < 100.f;
						config.blockLightningChance = percent / 100.f;
						SaveNow(config);
					}
					Describe({ "0% disables the extra effect. 100% triggers on every successful player block.",
						"0% 不触发叠加特效，100% 时每次成功格挡都触发。" }, config, description);
					Checkbox({ { "Random lightning color", "闪电颜色随机" }, "blockLightningRandomColor" }, config.blockLightningRandomColor, config,
					    { "Choose one RGB color per activation for the arcs and their companion sparks. Turn off to use your default lightning color.",
					      "每次触发生成一组 RGB，电弧和配套火花共用。关闭后使用下方的默认闪电颜色。" }, description);
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
					Text{ "  Missing game records: ", "  游戏记录缺失：" }.Get(config) + std::to_string(state.missing) +
					Text{ "  Conflicts: ", "  覆盖冲突：" }.Get(config) + std::to_string(state.conflicts);
				menu.TextUnformatted(summary.c_str(), nullptr);
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
