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

		void BlockStyle(RedsandSettings::Config& config, const char*& description)
		{
			const auto state = RedsandImpactVFX::GetStatus();
			const bool available = state.availability == RedsandImpactVFX::Availability::Ready && state.blockAvailable;
			const bool canDisable = menu.BeginDisabled && menu.EndDisabled;
			const Label label{ { "Block style", "格挡样式" }, "blockStyle" };
			if (available || canDisable) {
				if (canDisable) { menu.BeginDisabled(!available); }
				constexpr RedsandSettings::BlockStyle styles[]{RedsandSettings::BlockStyle::NormalBlock,RedsandSettings::BlockStyle::TimedSparks,
					RedsandSettings::BlockStyle::TimedFlare,RedsandSettings::BlockStyle::SimpleTimed,RedsandSettings::BlockStyle::StarRing};
				const auto current=config.blockStyle==RedsandSettings::BlockStyle::PerfectRing ? RedsandSettings::BlockStyle::TimedFlare : config.blockStyle;
				int selected=0;
				for (int i=0;i<5;++i) { if (styles[i]==current) { selected=i;break; } }
				const auto* choices = config.language == RedsandSettings::Language::Chinese ?
					"普通火花\0密集火花\0闪光圆环\0线状火花\0星芒圆环\0" : "Sparks\0Dense sparks\0Flash ring\0Line sparks\0Star ring\0";
				if (menu.Combo(label.Get(config).c_str(), &selected, choices, -1) && available) {
					config.blockStyle = styles[std::clamp(selected,0,4)];
					SaveNow(config);
				}
				if (canDisable) { menu.EndDisabled(); }
			} else { menu.TextUnformatted(label.text.Get(config), nullptr); }
			Describe(config.blockStyle == RedsandSettings::BlockStyle::StarRing ?
				Text{ "The star ring uses a fixed particle count. Color, brightness and size remain adjustable. Changes apply to the next block and do not change block timing or damage.",
					"星芒圆环的粒子数量固定，颜色、亮度和尺寸可调整。下次格挡生效，不改变判定或伤害。" } :
				Text{ "Choose the visual shown on successful blocks. Changes apply to the next block and do not change block timing or damage. Missing styles use the default sparks.",
					"选择成功格挡时的外观，下次格挡生效，不改变判定或伤害。样式资源缺失时使用普通火花。" }, config, description);
			if (!available) {
				menu.TextUnformatted(Text{ "Install the block-effects component to choose a style.", "安装格挡特效组件后可选择样式。" }.Get(config), nullptr);
			}
		}

		void Color(Label label, RedsandSettings::ColorTarget target, RedsandSettings::Config& config, const char*& description)
		{
			auto& color = config.colors[static_cast<unsigned>(target)];
			const auto caption = std::string(Text{ "Custom ", "自定义" }.Get(config)) + label.text.Get(config) + "###" + label.id + "Enabled";
			if (menu.Checkbox(caption.c_str(), &color.custom)) { SaveNow(config); }
			const bool weaponColor = target == RedsandSettings::ColorTarget::Melee || target == RedsandSettings::ColorTarget::Bow;
			const Text colorHelp = weaponColor ?
				Text{ "Use a fixed particle color. Uncheck for enchantment colors or the default blue. Separate profiles in More options take priority. Precision ribbon colors are unchanged.",
					"勾选使用固定粒子颜色；取消后跟随附魔，无有效附魔时使用默认蓝色。“更多选项”的独立设置优先。不改变 Precision 剑影颜色。" } :
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

		void SwingTrailOptions(RedsandSettings::Config& config, const char*& description)
		{
			Checkbox({ { "Redsand mode", "红沙模式" }, "swordReleasedTrail" },config.swordReleasedTrail,config,
				{ "From the next swing, replace the Precision ribbon and ordinary melee particles. Both fade smoothly; fine particles inherit swing motion and fall under gravity. Empty swings also work.",
				  "从下一次挥击起替换原 Precision 剑影和普通近战粒子。两者均平滑淡出，细粒子继承挥砍运动并受重力下落，空挥也生效。" },description);
			if (config.swordReleasedTrail) {
				Slider({ { "Arc hold time", "弧线保留时间" }, "swordRibbonLifetime" },config.swordRibbonLifetime,.03f,.8f,config,
					{ "After the swing, retain the arc for this long before its separate fade. Active segments wait at most 0.24 seconds before this phase.", "收招后先保留这段时间，再进入单独的淡出阶段；挥动中的每段轨迹最多等待 0.24 秒后进入此阶段。" },description,true);
				Slider({ { "Arc fade time", "弧线淡出时间" }, "swordRibbonFadeOut" },config.swordRibbonFadeOut,.06f,1.2f,config,
					{ "Smooth opacity fade after the hold, with a slight narrowing. This time is added to the hold time.", "保留结束后平滑降低透明度，同时轻微收窄；这段时间额外计入总持续时间。" },description,true);
				Slider({ { "Particle hold time", "粒子保留时间" }, "swordParticleLifetime" },config.swordParticleLifetime,.05f,1.2f,config,
					{ "Retain fine particles before their separate fade. Individual durations vary slightly so the trail thins naturally.", "散点进入独立淡出前的保留时间，个体时长略有差异，使残迹自然变稀。" },description,true);
				Slider({ { "Particle fade time", "粒子淡出时间" }, "swordParticleFadeOut" },config.swordParticleFadeOut,.08f,1.6f,config,
					{ "Smooth fade after particle retention. The particle tail always finishes after the arc.", "粒子保留结束后的平滑淡出时间，散点尾迹始终比弧线更晚结束。" },description,true);
				Slider({ { "Particle gravity", "粒子重力" }, "swordParticleGravity" },config.swordParticleGravity,0.f,3.f,config,
					{ "Downward acceleration of newly emitted fine particles with air resistance. 0 disables gravity. The arc stays unaffected; no damage or collision is added.", "控制新散点受空气阻力下落的强弱，0 关闭重力；剑影不受重力影响，不增加伤害或碰撞。" },description);
				Slider({ { "Arc width", "弧线宽度" }, "swordRibbonWidth" },config.swordRibbonWidth,.15f,3.f,config,{ "Width of the bright arc, independent of particle size.", "发光弧线的宽度，独立于散点粒子尺寸。" },description);
				Slider({ { "Outward drift", "甩出漂移" }, "swordTrailMomentum" },config.swordTrailMomentum,0.f,1.f,config,{ "Inherit a small amount of swing motion. 0 leaves the emitted arc in place. Never adds damage or a projectile.", "继承少量挥砍运动，0 在生成位置短暂留存。不增加伤害或真实弹射物。" },description);
			}
		}

		bool Fold(Label label, const RedsandSettings::Config& config)
		{
			if (menu.CollapsingHeader) { return menu.CollapsingHeader(label.Get(config).c_str(), 0); }

			menu.Separator();
			menu.TextUnformatted(label.text.Get(config), nullptr);
			return true;
		}

		void EnchantmentOptions(RedsandSettings::Config& config, const char*& description)
		{
			static int selected = 0;
			const char* labels = config.language == RedsandSettings::Language::Chinese ?
				"无附魔／附魔电量不足\0火焰\0冰霜\0雷电\0原版绿色 Shader＋毒药\0原版紫色 Shader\0原版蓝色 Shader\0原版红色 Shader\0束缚武器\0太阳／破晓\0其他／未识别\0" :
				"Unenchanted / depleted\0Fire\0Frost\0Shock\0Vanilla green shader / poison\0Vanilla purple shader\0Vanilla blue shader\0Vanilla red shader\0Bound weapons\0Sun / Dawnbreaker\0Other / unrecognized\0";
			menu.Combo(Label{ { "Enchantment category", "附魔类别" }, "enchantmentChoice" }.Get(config).c_str(), &selected, labels, 11);
			Describe({ "Categories use the existing enchantment matching order. Mods with matching effects join the same category. Multiple enchantments select one category without mixing colors or multiplying particles.",
				"沿用现有附魔匹配顺序，MOD 中符合条件的附魔自动归类。多附魔选择一个类别，不混色、不按附魔数增加粒子。" }, config, description);
			const auto key = std::string(RedsandEnchantmentProfiles::categoryKeys[selected]);
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
			if (Fold({ { "Melee trails", "近战轨迹" }, "categoryMelee" }, config)) {
				check({ { "Enable particles", "启用粒子" }, "categorySwordEnabled" }, appearance.swordEnabled, { "Enable melee particles for this category.", "启用此类别的近战粒子。" });
				check({ { "Player", "玩家" }, "categorySwordPlayer" }, appearance.swordPlayer, { "Show this category's melee particles for the player.", "显示玩家使用此类别时的近战粒子。" });
				check({ { "NPCs", "非玩家角色" }, "categorySwordNPC" }, appearance.swordNPC, { "Show this category's melee particles for NPCs.", "显示其他角色使用此类别时的近战粒子。" });
				check({ { "Show Precision trail", "同时显示 Precision 剑影" }, "categorySwordRibbon" }, appearance.swordKeepPrecisionTrail,
					{ "Show the companion Precision ribbon. Its original color rules remain unchanged, and Precision trails must also be enabled.", "同时显示 Precision 剑影，保留剑影原配色；也需开启 Precision 自身的剑影设置。" });
				check({ { "Emit from weapon tip", "从武器尖端发射" }, "categorySwordTip" }, appearance.swordTip, { "Turn off to emit along the blade or weapon head.", "关闭后沿刃部或武器头部发射。" });
				check({ { "Redsand mode", "红沙模式" }, "categoryReleasedTrail" },appearance.swordReleasedTrail,
					{ "Replace the Precision ribbon and ordinary melee particles. Both fade smoothly; fine particles inherit motion and gravity. Overrides the companion-ribbon and weapon-tip options.",
					  "替换原 Precision 剑影和普通近战粒子；两者均平滑淡出，细粒子继承惯性并受重力下落；启用时取代同时显示剑影及尖端发射选项。" });
				if (appearance.swordReleasedTrail) {
					slider({ { "Arc hold time", "弧线保留时间" }, "categoryArcLife" },appearance.swordRibbonLifetime,.03f,.8f,{ "Retain the arc after the swing before its separate fade. Active history waits at most 0.24 seconds before this phase.", "收招后先保留，再单独淡出；挥动中的每段轨迹最多等待 0.24 秒后进入此阶段。" },true);
					slider({ { "Arc fade time", "弧线淡出时间" }, "categoryArcFade" },appearance.swordRibbonFadeOut,.06f,1.2f,{ "Additional smooth opacity fade, with slight narrowing, after retention.", "保留结束后额外的平滑淡出时间，同时轻微收窄。" },true);
					slider({ { "Particle hold time", "粒子保留时间" }, "categoryParticleLife" },appearance.swordParticleLifetime,.05f,1.2f,{ "Retain fine particles before their separate fade; individual durations vary slightly.", "散点开始独立淡出前的保留时间，个体时长略有差异。" },true);
					slider({ { "Particle fade time", "粒子淡出时间" }, "categoryParticleFade" },appearance.swordParticleFadeOut,.08f,1.6f,{ "Smoothly fade the residual particles. They always end after the arc.", "平滑淡出残留散点，其尾迹始终比弧线更晚结束。" },true);
					slider({ { "Particle gravity", "粒子重力" }, "categoryParticleGravity" },appearance.swordParticleGravity,0.f,3.f,{ "Gravity and air resistance for newly emitted fine particles. 0 disables gravity; the arc does not fall.", "新散点受重力及空气阻力影响，0 关闭重力；剑影不会下落。" });
					slider({ { "Arc width", "弧线宽度" }, "categoryArcWidth" },appearance.swordRibbonWidth,.15f,3.f,{ "Arc width is separate from the small residual particles.", "弧线宽度独立于细小残迹粒子。" });
					slider({ { "Outward drift", "甩出漂移" }, "categoryArcDrift" },appearance.swordTrailMomentum,0.f,1.f,{ "Small motion inherited at emission; 0 leaves the trail in world space.", "生成时继承少量运动；0 在世界空间原位短暂留存。" });
				}
				color(appearance.color, "categoryMeleeColor");
				slider({ { "Brightness", "亮度" }, "categorySwordBrightness" }, appearance.brightness, 0.f, RedsandSettings::maxBrightness, { "Independent melee brightness. 0 hides the glow; it does not stop emission.", "独立近战亮度；0 隐藏发光，不停止发射。" });
				slider({ { "Particle count", "粒子数量" }, "categorySwordDensity" }, appearance.density, 0.f, RedsandSettings::maxDensity, { "Independent melee count. 0 stops new emission; higher counts cost more performance.", "独立近战数量；0 停止新发射，数量越多性能开销越大。" });
				slider({ { "Particle size", "粒子尺寸" }, "categorySwordSize" }, appearance.size, RedsandSettings::minSize, RedsandSettings::maxSize, { "Independent melee particle size.", "独立近战粒子尺寸。" });
				slider({ { "Spread", "散布宽度" }, "categorySwordSpread" }, appearance.spread, 0.f, RedsandSettings::maxSpread, { "Independent melee spread. Does not affect bow or hit slash sparks.", "独立近战散布，不影响弓箭或命中斩击火花。" });
			}
			if (Fold({ { "Bow trails", "弓箭轨迹" }, "categoryBow" }, config)) {
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
			menu.TextUnformatted(Text{ "Category settings replace the main pages; values are not multiplied together. Existing colors and matching priorities are preserved in Original color mode.",
				"分类参数替代主页参数，不与主页相乘。原配色模式保留现有颜色与匹配优先级。" }.Get(config), nullptr);
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
					Checkbox({ { "Enable particles", "启用粒子" }, "swordEnabled" }, config.swordEnabled, config,
						{ "Show particles along melee weapon swings.", "挥动近战武器时显示粒子轨迹。" }, description);
					Checkbox({ { "Show Precision trail", "同时显示 Precision 剑影" }, "swordKeepPrecisionTrail" }, config.swordKeepPrecisionTrail, config,
						{ "Show Precision's weapon ribbon alongside ordinary particles. Redsand mode replaces this ribbon. Precision's own trail setting must also be enabled.",
						  "普通粒子与 Precision 剑影同时显示；红沙模式启用时替换原剑影。也需开启 Precision 自身的剑影设置。" }, description);
					Slider({ { "Brightness", "亮度" }, "swordBrightness" }, config.swordBrightness, 0.0f, RedsandSettings::maxBrightness, config,
						{ "Adjust particle glow. 0 hides the particles; bloom also depends on your graphics settings.",
						  "调整粒子发光亮度，0 隐藏粒子；泛光强弱也受画面设置影响。" }, description);
					Slider({ { "Particle count", "粒子数量" }, "swordDensity" }, config.swordDensity, 0.0f, RedsandSettings::maxDensity, config,
						{ "Adjust trail density. 0 stops new particles; higher counts cost more performance.",
						  "调整轨迹疏密，0 停止发射粒子；数量越多，性能开销越大。" }, description);
					Slider({ { "Particle size", "粒子尺寸" }, "swordSize" }, config.swordSize, RedsandSettings::minSize, RedsandSettings::maxSize, config,
						{ "Adjust the size of each particle. Applies to newly emitted particles.",
						  "调整每颗粒子的大小，作用于新发射的粒子。" }, description);
					SwingTrailOptions(config,description);
					Color({ { "Melee particle color", "近战粒子颜色" }, "meleeRGB" }, RedsandSettings::ColorTarget::Melee, config, description);
					if (Fold({ { "More options", "更多选项" }, "meleeAdvanced" }, config)) {
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
					if (Fold({ { "More options", "更多选项" }, "bowAdvanced" }, config)) {
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
						{ "Show the selected sparks or flash when an attack is blocked. Does not change block timing or damage.",
						  "成功挡住攻击时显示所选火花或闪光，不改变格挡判定或伤害。" }, description);
					BlockStyle(config, description);
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
						{ "Adjust slash spark density together with the overall count, up to the supported maximum. 0 hides slash sparks without hiding the cut of light.",
						  "调整斩击火花数量，与总体数量共同作用，最高为支持的最大数量；0 隐藏火花，不影响切光。" }, description);
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
					const auto state = RedsandImpactVFX::GetStatus();
					if (state.availability != RedsandImpactVFX::Availability::Ready) {
						const auto unavailable = state.availability == RedsandImpactVFX::Availability::InvalidRoutes ?
							Text{ "Effect resources could not be validated. Check the mod installation and file conflicts.", "特效资源校验失败，请检查安装是否完整及文件覆盖。" } :
							Text{ "Effect resources are not loaded. Check the mod installation and restart the game.", "特效资源尚未载入，请检查模组安装并重新启动游戏。" };
						menu.TextUnformatted(unavailable.Get(config), nullptr);
					}
					menu.EndTabItem();
				}
				if (menu.BeginTabItem(Label{ { "More options", "更多选项" }, "redsandMoreOptions" }.Get(config).c_str(), nullptr, 0)) {
					EnchantmentOptions(config, description);
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
