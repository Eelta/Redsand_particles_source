#pragma once







#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Redsand::MenuFramework
{
	using RenderFunction = void(__stdcall*)();
	struct API
	{
		using AddSectionItemFunction = void (*)(const char*, RenderFunction);
		using CheckboxFunction = bool (*)(const char*, bool*);
		using SliderFloatFunction = bool (*)(const char*, float*, float, float, const char*, int);
		using SmallButtonFunction = bool (*)(const char*);
		using TextUnformattedFunction = void (*)(const char*, const char*);
		using VoidFunction = void (*)();
		using ItemDeactivatedAfterEditFunction = bool (*)();
		using BeginTabBarFunction = bool (*)(const char*, int);
		using BeginTabItemFunction = bool (*)(const char*, bool*, int);
		using IsItemHoveredFunction = bool (*)(int);
		using PushTextWrapPosFunction = void (*)(float);
		using ComboFunction = bool (*)(const char*, int*, const char*, int);
		using SameLineFunction = void (*)(float, float);
		using SetNextItemWidthFunction = void (*)(float);
		using ColorEdit3Function = bool (*)(const char*, float*, int);
		using CollapsingHeaderFunction = bool (*)(const char*, int);
		using BeginDisabledFunction = void (*)(bool);

		AddSectionItemFunction AddSectionItem = nullptr;
		CheckboxFunction Checkbox = nullptr;
		SliderFloatFunction SliderFloat = nullptr;
		SmallButtonFunction SmallButton = nullptr;
		TextUnformattedFunction TextUnformatted = nullptr;
		VoidFunction Separator = nullptr;
		ItemDeactivatedAfterEditFunction IsItemDeactivatedAfterEdit = nullptr;
		BeginTabBarFunction BeginTabBar = nullptr;
		VoidFunction EndTabBar = nullptr;
		BeginTabItemFunction BeginTabItem = nullptr;
		VoidFunction EndTabItem = nullptr;
		IsItemHoveredFunction IsItemHovered = nullptr;
		PushTextWrapPosFunction PushTextWrapPos = nullptr;
		VoidFunction PopTextWrapPos = nullptr;
		ComboFunction Combo = nullptr;
		SameLineFunction SameLine = nullptr;
		SetNextItemWidthFunction SetNextItemWidth = nullptr;
		ColorEdit3Function ColorEdit3 = nullptr;
		CollapsingHeaderFunction CollapsingHeader = nullptr;
		BeginDisabledFunction BeginDisabled = nullptr;
		VoidFunction EndDisabled = nullptr;

		bool Load()
		{

			const auto module = GetModuleHandleW(L"SKSEMenuFramework.dll");
			if (!module) { return false; }
			AddSectionItem = Resolve<AddSectionItemFunction>(module, "AddSectionItem");
			Checkbox = Resolve<CheckboxFunction>(module, "igCheckbox");
			SliderFloat = Resolve<SliderFloatFunction>(module, "igSliderFloat");
			SmallButton = Resolve<SmallButtonFunction>(module, "igSmallButton");
			TextUnformatted = Resolve<TextUnformattedFunction>(module, "igTextUnformatted");
			Separator = Resolve<VoidFunction>(module, "igSeparator");
			IsItemDeactivatedAfterEdit = Resolve<ItemDeactivatedAfterEditFunction>(module, "igIsItemDeactivatedAfterEdit");
			BeginTabBar = Resolve<BeginTabBarFunction>(module, "igBeginTabBar");
			EndTabBar = Resolve<VoidFunction>(module, "igEndTabBar");
			BeginTabItem = Resolve<BeginTabItemFunction>(module, "igBeginTabItem");
			EndTabItem = Resolve<VoidFunction>(module, "igEndTabItem");
			IsItemHovered = Resolve<IsItemHoveredFunction>(module, "igIsItemHovered");
			PushTextWrapPos = Resolve<PushTextWrapPosFunction>(module, "igPushTextWrapPos");
			PopTextWrapPos = Resolve<VoidFunction>(module, "igPopTextWrapPos");
			Combo = Resolve<ComboFunction>(module, "igCombo_Str");
			SameLine = Resolve<SameLineFunction>(module, "igSameLine");
			SetNextItemWidth = Resolve<SetNextItemWidthFunction>(module, "igSetNextItemWidth");
			ColorEdit3 = Resolve<ColorEdit3Function>(module, "igColorEdit3");
			CollapsingHeader = Resolve<CollapsingHeaderFunction>(module, "igCollapsingHeader_TreeNodeFlags");
			BeginDisabled = Resolve<BeginDisabledFunction>(module, "igBeginDisabled");
			EndDisabled = Resolve<VoidFunction>(module, "igEndDisabled");
			return AddSectionItem && Checkbox && SliderFloat && SmallButton && TextUnformatted && Separator &&
				BeginTabBar && EndTabBar && BeginTabItem && EndTabItem && IsItemHovered && PushTextWrapPos && PopTextWrapPos &&
				Combo && SameLine && SetNextItemWidth;
		}

	private:
		template <class Function>
		static Function Resolve(HMODULE module, const char* name)
		{
			return reinterpret_cast<Function>(GetProcAddress(module, name));
		}
	};
}
