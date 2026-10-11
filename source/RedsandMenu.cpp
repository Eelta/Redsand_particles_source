#include "RedsandMenu.h"
#include "RedsandLog.h"
#include "RedsandSettings.h"
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
			const char* description = Text{ "Hover over an option to see its description.", "鼠标移到选项上可查看说明。" }.Get(config);
			Describe({ "Switch the menu language immediately. Your choice is saved automatically.",
				"立即切换菜单语言，并自动保存所选语言。" }, config, description);
			menu.SameLine(0.0f, -1.0f);
			if (menu.SmallButton(Label{ { "Defaults", "恢复默认" }, "redsandDefaults" }.Get(config).c_str())) {
				RedsandSettings::ResetDefaults();
				RedsandLog::Sync();
				config = RedsandSettings::Get();
			}
			Describe({ "Restore all melee and bow particle defaults and turn diagnostic logging off, keeping your selected language.",
				"立即恢复近战与弓箭的全部默认设置并关闭诊断日志，保留当前菜单语言并自动保存。" }, config, description);
			menu.Separator();
			Checkbox({ { "Diagnostic logging", "诊断日志" }, "redsandDiagnostics" }, config.diagnostics, config,
				{ "Write diagnostic details to RedsandParticles.log to investigate NPC particles getting darker after repeated attacks. Off by default.",
				  "向 RedsandParticles.log 写入诊断信息，用于排查敌人粒子随多次攻击逐渐变暗的问题。默认关闭。" }, description);
			menu.Separator();
			if (menu.BeginTabBar("RedsandParticleTabs", 0)) {
				const bool meleeTab = menu.BeginTabItem(Label{ { "Melee", "近战" }, "redsandMelee" }.Get(config).c_str(), nullptr, 0);
				Describe({ "Configure melee particles, including tip trails, spread and particle size.",
					"设置近战粒子、剑尖轨迹、散布强度与粒子尺寸。" }, config, description);
				if (meleeTab) {
					Checkbox({ { "Enable particles", "启用粒子" }, "swordEnabled" }, config.swordEnabled, config,
						{ "Enable melee particles. Vanilla weapon categories use distinct shapes; added mod weapons use the default effect.",
						  "控制近战粒子。原版武器按类别使用不同形状，模组新增武器使用默认效果。" }, description);
					Checkbox({ { "Player", "玩家" }, "swordPlayer" }, config.swordPlayer, config,
						{ "Show particles on the player's melee weapons. Changes take effect immediately and save automatically.",
						  "控制玩家近战武器的粒子，修改立即生效并自动保存。" }, description);
					Checkbox({ { "NPCs", "非玩家角色" }, "swordNPC" }, config.swordNPC, config,
						{ "Show particles on NPC melee weapons. Changes take effect immediately and save automatically.",
						  "控制非玩家角色近战武器的粒子，修改立即生效并自动保存。" }, description);
					Checkbox({ { "Sword tip trail", "绑定剑尖轨迹" }, "swordTip" }, config.swordTip, config,
						{ "Emit along the moving weapon tip. On by default; turn off to emit along the weapon blade or head instead.",
						  "默认开启，沿移动的武器尖端轨迹发射；关闭后沿武器刃部或头部发射。" }, description);
					Slider({ { "Brightness", "亮度" }, "swordBrightness" }, config.swordBrightness, 0.0f, RedsandSettings::maxBrightness, config,
						{ "Melee brightness: 0 to 5x. 1x is the default; 0 hides the effect. Drag to change it immediately.",
						  "近战粒子亮度为 0 至 5 倍。1 倍为默认，0 倍隐藏效果；拖动立即生效。" }, description);
					Slider({ { "Particle count", "粒子数量" }, "swordDensity" }, config.swordDensity, 0.0f, RedsandSettings::maxDensity, config,
						{ "Melee particle count: 0 to 2x. 1x is the default; 0 stops emission. Drag to change it immediately.",
						  "近战粒子数量为 0 至 2 倍。1 倍为默认，0 倍停止发射；拖动立即生效。" }, description);
					Slider({ { "Spread", "散布强度" }, "swordSpread" }, config.swordSpread, 0.0f, RedsandSettings::maxSpread, config,
						{ "Melee spread: 0 to 2x, default 1x. Lower values tighten the trail; 0 is tightest but keeps some random motion. New particles use changes immediately.",
						  "近战散布强度为 0 至 2 倍，默认 1 倍。调低可收束轨迹，0 倍最收束但仍保留少量随机抖动；新发射粒子立即使用该值。" }, description);
					Slider({ { "Particle size", "粒子尺寸" }, "swordSize" }, config.swordSize, RedsandSettings::minSize, RedsandSettings::maxSize, config,
						{ "Melee particle size: 0.25 to 2x, default 1x. Changes affect newly emitted particles; increase it to inspect their shape.",
						  "近战粒子尺寸为 0.25 至 2 倍，默认 1 倍。调整影响新发射的粒子，可放大查看粒子形状。" }, description);
					menu.EndTabItem();
				}
				const bool bowTab = menu.BeginTabItem(Label{ { "Bow", "弓箭" }, "redsandBow" }.Get(config).c_str(), nullptr, 0);
				Describe({ "Configure bow and crossbow gathering, release rings and flight trails.",
					"设置弓箭与弩的蓄力聚集、发射环与飞行轨迹。" }, config, description);
				if (bowTab) {
					Checkbox({ { "Enable particles", "启用粒子" }, "bowEnabled" }, config.bowEnabled, config,
						{ "Enable bow and crossbow particles. Changes take effect immediately and save automatically.",
						  "控制弓箭与弩的粒子，修改立即生效并自动保存。" }, description);
					Checkbox({ { "Player", "玩家" }, "bowPlayer" }, config.bowPlayer, config,
						{ "Show bow and crossbow particles for the player. Changes take effect immediately and save automatically.",
						  "控制玩家弓箭与弩的粒子，修改立即生效并自动保存。" }, description);
					Checkbox({ { "NPCs", "非玩家角色" }, "bowNPC" }, config.bowNPC, config,
						{ "Show bow and crossbow particles for NPCs. Changes take effect immediately and save automatically.",
						  "控制非玩家角色弓箭与弩的粒子，修改立即生效并自动保存。" }, description);
					Checkbox({ { "Charge gathering", "蓄力聚集" }, "bowGathering" }, config.bowGathering, config,
						{ "Gather particles near the arrowhead while drawing a bow. Crossbows use release rings and flight trails.",
						  "拉弓蓄力时在箭头附近聚集粒子。弩使用发射环与飞行轨迹。" }, description);
					Checkbox({ { "Release rings", "发射环" }, "bowRings" }, config.bowRings, config,
						{ "Create a particle ring when an arrow or crossbow bolt is fired.", "射出箭矢或弩矢时生成粒子环。" }, description);
					Checkbox({ { "Flight trails", "飞行轨迹" }, "bowTrails" }, config.bowTrails, config,
						{ "Leave a particle trail behind arrows and crossbow bolts in flight.", "在飞行中的箭矢或弩矢后方生成粒子轨迹。" }, description);
					Slider({ { "Brightness", "亮度" }, "bowBrightness" }, config.bowBrightness, 0.0f, RedsandSettings::maxBrightness, config,
						{ "Bow and crossbow brightness: 0 to 5x. 1x is the default; 0 hides the effect. Drag to change it immediately.",
						  "弓箭与弩的粒子亮度为 0 至 5 倍。1 倍为默认，0 倍隐藏效果；拖动立即生效。" }, description);
					Slider({ { "Particle count", "粒子数量" }, "bowDensity" }, config.bowDensity, 0.0f, RedsandSettings::maxDensity, config,
						{ "Bow and crossbow particle count: 0 to 2x. 1x is the default; 0 stops emission. Drag to change it immediately.",
						  "弓箭与弩的粒子数量为 0 至 2 倍。1 倍为默认，0 倍停止发射；拖动立即生效。" }, description);
					menu.EndTabItem();
				}
				menu.EndTabBar();
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
