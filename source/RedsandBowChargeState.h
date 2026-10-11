#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace Redsand::Bow
{
	enum class ChargeSignal { None, Start, Attach, Ready, PullStart, PullEnd, Stop };

	inline bool EqualTag(std::string_view left, std::string_view right)
	{
		if (left.size() != right.size()) { return false; }
		for (std::size_t i = 0; i < left.size(); ++i) {
			const char c = left[i] >= 'A' && left[i] <= 'Z' ? left[i] + ('a' - 'A') : left[i];
			if (c != right[i]) { return false; }
		}
		return true;
	}

	inline ChargeSignal ClassifyChargeEvent(std::string_view tag, std::string_view payload = {})
	{
		for (auto start : { "bowdraw", "bowdrawstart", "sbf_bowattackstart" }) {
			if (EqualTag(tag, start)) { return ChargeSignal::Start; }
		}
		if (EqualTag(tag, "arrowattach")) { return ChargeSignal::Attach; }
		if (EqualTag(tag, "bowdrawn")) { return ChargeSignal::Ready; }
		if (EqualTag(tag, "soundplay.wpnbowpulllpm") || (EqualTag(tag, "soundplay") && EqualTag(payload, "wpnbowpulllpm"))) { return ChargeSignal::PullStart; }
		if (EqualTag(tag, "soundstop.wpnbowpulllpm") || (EqualTag(tag, "soundstop") && EqualTag(payload, "wpnbowpulllpm"))) { return ChargeSignal::PullEnd; }
		for (auto stop : { "arrowrelease", "bowrelease", "bowreset", "arrowdetach", "attackstop", "sbf_bowattackstop" }) {
			if (EqualTag(tag, stop)) { return ChargeSignal::Stop; }
		}

		return ChargeSignal::None;
	}

	struct ChargeState
	{
		bool latched = false;
		bool attached = false;
		bool fullyDrawn = false;
		bool pulling = false;
		bool stopped = false;
		bool seen = false;
		bool nativeRestartPending = false;
		bool graphDrawnWasClear = false;
		bool graphConfirmed = false;
		bool eventReady = false;
		bool eventReadyPending = false;
		std::uint32_t previousNativeState = 0;
		float readyTime = 0.f;
		static constexpr float readyDelay = 0.06f;

		void ResetDraw()
		{
			latched = attached = fullyDrawn = pulling = false;
			nativeRestartPending = false;
			graphConfirmed = false;
			eventReady = eventReadyPending = false;
			readyTime = 0.f;
		}

		void Signal(ChargeSignal signal)
		{

			if (stopped && (signal == ChargeSignal::Attach || signal == ChargeSignal::PullStart)) {
				ResetDraw();
				stopped = false;
			}
			if (signal == ChargeSignal::Start) {

				if (stopped || !latched) { ResetDraw(); }
				latched = true;
				stopped = false;
			}
			else if (signal == ChargeSignal::Attach) { latched = attached = true; }
			else if (signal == ChargeSignal::Ready && !stopped) { latched = fullyDrawn = eventReady = eventReadyPending = true; }
			else if (signal == ChargeSignal::PullStart && !stopped) {
				if (!pulling && fullyDrawn) {
					fullyDrawn = graphConfirmed = eventReady = eventReadyPending = false;
					readyTime = 0.f;
				}
				latched = pulling = true;
			}
			else if (signal == ChargeSignal::PullEnd && pulling && !stopped) {

				pulling = false;
				fullyDrawn = eventReady = eventReadyPending = true;
			}
			else if (signal == ChargeSignal::Stop) {
				ResetDraw();
				stopped = true;
				graphDrawnWasClear = false;
			}
		}

		void OnViewChanged()
		{

			nativeRestartPending = false;
			graphDrawnWasClear = false;
			graphConfirmed = false;
			readyTime = 0.f;
		}

		bool Active(std::uint32_t nativeState, std::optional<bool> graphAttacking, bool nockedArrow,
			float deltaTime, std::optional<bool> graphDrawn = std::nullopt)
		{

			const auto previousNative = previousNativeState;
			previousNativeState = nativeState;
			const bool freshEventReady = eventReadyPending;
			eventReadyPending = false;
			const bool drawing = nativeState >= 8 && nativeState <= 11;
			const bool graphCanConfirm = nativeState != 8 && nativeState != 9 && nativeState != 11;
			if (graphDrawn == false) {

				if (graphConfirmed) { Signal(ChargeSignal::Stop); }
				graphDrawnWasClear = true;
			}
			const bool graphReadyEdge = graphDrawn == true && graphDrawnWasClear;
			if (graphDrawn == true) { graphDrawnWasClear = false; }
			if (stopped) {

				if (graphReadyEdge && graphCanConfirm) {
					ResetDraw();
					stopped = false;
				} else if (nativeState == 8 || nativeState == 9) {
					if (nativeState != previousNative) { nativeRestartPending = true; }
				} else if (nativeState == 10 && nativeRestartPending) {
					ResetDraw();
					stopped = false;
				} else if (nativeState != 10) { nativeRestartPending = false; }
				if (stopped) {
					if (!drawing && graphAttacking == false && graphDrawn != true) { stopped = false; }
					return false;
				}
			}
			if ((nativeState == 6 && !(graphDrawn == true && nockedArrow)) || (nativeState >= 12 && nativeState <= 14)) {
				Signal(ChargeSignal::Stop);
				return false;
			}
			if (nativeState == 8 || nativeState == 9) {

				latched = true;
				const bool newNativeDraw = previousNative != 8 && previousNative != 9;
				if (!eventReady || (newNativeDraw && !freshEventReady)) {
					fullyDrawn = graphConfirmed = eventReady = false;
					readyTime = 0.f;
					return false;
				}
			}
			if (nativeState == 10) { latched = fullyDrawn = attached = true; }

			if (graphDrawn == true && graphCanConfirm) {

				latched = fullyDrawn = attached = graphConfirmed = true;
				graphDrawnWasClear = false;
			}
			if (!fullyDrawn || !(attached || nockedArrow)) { readyTime = 0.f; return false; }
			if (nativeState == 11 && readyTime < readyDelay) { readyTime = 0.f; return false; }

			if (std::isfinite(deltaTime) && deltaTime > 0.f) { readyTime = std::min(readyDelay, readyTime + std::min(deltaTime, 0.03f)); }
			return readyTime >= readyDelay;
		}
	};

	struct ChargeView
	{
		std::uintptr_t root = 0, source = 0;
		std::uint64_t epoch = 0;
		bool firstPerson = false;

		bool Refresh(bool first, std::uintptr_t nextRoot, std::uintptr_t nextSource)
		{
			if (firstPerson == first && root == nextRoot && source == nextSource) { return false; }
			firstPerson = first;
			root = nextRoot;
			source = nextSource;
			++epoch;
			return true;
		}

		bool Accepts(std::uintptr_t eventSource, std::uint64_t eventEpoch) const
		{

			return eventSource == 0 || (source != 0 && eventSource == source && eventEpoch == epoch);
		}
	};
}
