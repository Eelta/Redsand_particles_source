#pragma once

#include "RedsandBowChargeState.h"

#include <algorithm>
#include <cstdint>

namespace Redsand::Bow
{
	struct OrderedChargeState : ChargeState
	{
		std::uint64_t lastSequence = 0;
		std::uint64_t drawObservationSequence = 0;

		bool SignalOrdered(ChargeSignal signal, std::uint64_t sequence)
		{

			if (sequence <= lastSequence) { return false; }
			lastSequence = sequence;
			ChargeState::Signal(signal);
			return true;
		}

		bool SignalRelease(std::uint64_t sequence)
		{
			if (sequence <= drawObservationSequence) { return false; }
			return SignalOrdered(ChargeSignal::Stop, sequence);
		}

		std::uint64_t DrawSequence() const { return std::max(lastSequence, drawObservationSequence); }

		bool ActiveOrdered(std::uint32_t nativeState, std::optional<bool> graphAttacking, bool nockedArrow,
			float deltaTime, std::optional<bool> graphDrawn, std::uint64_t observationSequence)
		{
			const bool knownPreviousDraw = stopped || fullyDrawn || graphConfirmed;
			const bool earlyDrawEdge = knownPreviousDraw && (nativeState == 8 || nativeState == 9) && nativeState != previousNativeState;
			const bool graphClearEdge = (stopped || graphConfirmed) && graphDrawn == false && !graphDrawnWasClear;
			const bool wasStopped = stopped;
			const bool active = ChargeState::Active(nativeState, graphAttacking, nockedArrow, deltaTime, graphDrawn);
			const bool recoveredDraw = wasStopped && !stopped && latched;
			if (earlyDrawEdge || graphClearEdge || recoveredDraw) {

				drawObservationSequence = std::max(drawObservationSequence, observationSequence);
			}
			return active;
		}
	};

	struct ShotState
	{
		float silence = 0.f;
		float lastLivingTime = -1.f;
		std::uint64_t lastSampleSequence = 0;
		bool releaseConsumed = false;
		bool streamCreated = false;
		bool allowRendererRecovery = false;
		bool rendererResident = false;
		bool flightEnded = false;

		bool ConsumeRelease()
		{

			if (releaseConsumed) { return false; }
			releaseConsumed = true;
			return true;
		}
	};

	template <class Events, class Samples, class EventCallback, class SampleCallback>
	void ReplayEvents(Events& events, Samples& samples, EventCallback onEvent, SampleCallback onSample)
	{
		const auto earlier = [](const auto& left, const auto& right) { return left.sequence < right.sequence; };
		std::stable_sort(events.begin(), events.end(), earlier);
		std::stable_sort(samples.begin(), samples.end(), earlier);
		auto event = events.begin();
		auto sample = samples.begin();
		while (event != events.end() || sample != samples.end()) {
			if (sample == samples.end() || (event != events.end() && event->sequence <= sample->sequence)) {
				onEvent(*event++);
			} else {
				onSample(*sample++);
			}
		}
	}
}
