#include "../source/RedsandBowEventState.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Redsand::Bow;

namespace
{
	void check(bool condition, const char* message)
	{
		if (!condition) { throw std::runtime_error(message); }
	}

	struct Event
	{
		std::uint64_t sequence;
		ChargeSignal signal;
	};

	struct Sample
	{
		std::uint64_t sequence;
		ShotState* shot;
	};

	bool FinishDraw(OrderedChargeState& state)
	{
		for (int i = 0; i < 5; ++i) { state.Active(5, true, true, 1.f / 60.f); }
		return state.Active(5, true, true, 1.f / 60.f);
	}

	void Replay(OrderedChargeState& state, std::vector<Event>& events, std::vector<Sample>& samples)
	{
		ReplayEvents(events, samples,
			[&](const Event& event) { state.SignalOrdered(event.signal, event.sequence); },
			[&](const Sample& sample) {
				if (sample.shot->ConsumeRelease()) { state.SignalRelease(sample.sequence); }
			});
	}

	void sameBatchDrawAndReleaseOrdering()
	{
		OrderedChargeState nextDraw;
		ShotState previousShot;

		std::vector<Event> events{ { 4, ChargeSignal::PullEnd }, { 2, ChargeSignal::Start }, { 3, ChargeSignal::PullStart } };
		std::vector<Sample> samples{ { 1, &previousShot } };
		Replay(nextDraw, events, samples);
		check(FinishDraw(nextDraw), "previous launch cancelled the next draw in the same batch");
		check(previousShot.releaseConsumed, "previous launch was not consumed");

		OrderedChargeState released;
		ShotState currentShot;
		events = { { 3, ChargeSignal::PullEnd }, { 1, ChargeSignal::Start }, { 2, ChargeSignal::PullStart } };
		samples = { { 4, &currentShot } };
		Replay(released, events, samples);
		check(!FinishDraw(released) && released.stopped, "current launch failed to cancel its preceding draw");
	}

	void delayedWorkerSampleAndNewCancellation()
	{
		OrderedChargeState state;
		std::vector<Event> events{ { 12, ChargeSignal::PullEnd }, { 10, ChargeSignal::Start }, { 11, ChargeSignal::PullStart } };
		std::vector<Sample> samples;
		Replay(state, events, samples);
		check(FinishDraw(state), "next draw was not ready before the delayed sample");
		ShotState delayedShot;
		events.clear();
		samples = { { 9, &delayedShot } };
		Replay(state, events, samples);
		check(FinishDraw(state) && state.lastSequence == 12, "late worker sample reset a newer draw across batches");
		check(delayedShot.releaseConsumed, "late worker release was left available for retry");
		check(state.SignalOrdered(ChargeSignal::Stop, 13), "new cancellation was rejected");
		check(!FinishDraw(state) && state.stopped, "new cancellation left charge active");
		check(!state.SignalOrdered(ChargeSignal::Start, 12), "old start crossed the new cancellation");
		check(!state.SignalOrdered(ChargeSignal::Start, 13), "duplicate sequence crossed the new cancellation");
		check(!FinishDraw(state), "stale events revived a cancelled draw");
		OrderedChargeState initial;
		check(!initial.SignalOrdered(ChargeSignal::Start, 0), "reserved initial sequence was accepted");
	}

	void repeatedSamplesAndRendererRetry()
	{
		OrderedChargeState state;
		ShotState shot;
		int releases = 0;
		int streams = 0;
		auto sample = [&](std::uint64_t sequence, bool spawnSucceeds) {
			if (shot.ConsumeRelease()) {
				++releases;
				state.SignalRelease(sequence);
			}
			if (!shot.streamCreated && spawnSucceeds) {
				shot.streamCreated = true;
				++streams;
			}
		};
		sample(1, false);
		check(shot.releaseConsumed && !shot.streamCreated, "failed particle creation lost the release or marked a stream created");
		state.SignalOrdered(ChargeSignal::Start, 2);
		state.SignalOrdered(ChargeSignal::PullStart, 3);
		state.SignalOrdered(ChargeSignal::PullEnd, 4);
		sample(5, false);
		check(FinishDraw(state), "failed renderer retry cancelled the next draw");
		sample(6, true);
		sample(7, true);
		check(FinishDraw(state) && state.lastSequence == 4, "repeated projectile samples cancelled a new draw");
		check(releases == 1 && streams == 1 && shot.streamCreated, "retry repeated release consumption or stream creation");
	}

	void polledDrawFencesOnlyOlderProjectileWork()
	{
		constexpr float dt = 1.f / 60.f;
		OrderedChargeState graph;
		graph.SignalOrdered(ChargeSignal::Stop, 10);
		check(!graph.ActiveOrdered(5, true, true, dt, false, 20), "cleared graph emitted before a new full draw");
		check(!graph.SignalRelease(11), "old worker sample erased the observed graph reset");
		check(!graph.ActiveOrdered(5, true, true, dt, true, 30), "polled graph recovery bypassed readiness debounce");
		for (std::uint64_t sequence = 40; sequence < 45; ++sequence) {
			graph.ActiveOrdered(5, true, true, dt, true, sequence);
		}
		check(graph.DrawSequence() == 30 && graph.lastSequence == 10, "continuous graph hold advanced the release fence or explicit event sequence");
		check(!graph.SignalRelease(25) && FinishDraw(graph), "old worker sample cancelled a recovered graph-only draw");

		check(graph.SignalRelease(35) && !FinishDraw(graph), "continuous graph polling swallowed the current real release");

		for (auto native : { 8u, 9u }) {
			OrderedChargeState state;
			state.SignalOrdered(ChargeSignal::Stop, 10);
			check(!state.ActiveOrdered(native, true, true, dt, std::nullopt, 20), "early native recovery emitted particles");
			check(!state.SignalRelease(11), "old worker sample cleared pending native recovery");
			check(!state.ActiveOrdered(native, true, true, dt, std::nullopt, 21), "unchanged early native draw emitted particles");
			check(state.DrawSequence() == 20, "unchanged native early draw advanced the observation fence");
			check(!state.ActiveOrdered(10, true, true, dt, std::nullopt, 30), "native recovery bypassed readiness debounce");
			for (std::uint64_t sequence = 40; sequence < 45; ++sequence) {
				state.ActiveOrdered(10, true, true, dt, std::nullopt, sequence);
			}
			check(state.DrawSequence() == 30 && !state.SignalRelease(25) && FinishDraw(state), "old sample cancelled a recovered native-only draw");
			check(state.SignalRelease(35) && !FinishDraw(state), "continuous native hold swallowed its real release");
		}

		OrderedChargeState eventOnly;
		eventOnly.SignalOrdered(ChargeSignal::Start, 1);
		eventOnly.SignalOrdered(ChargeSignal::Attach, 2);
		eventOnly.SignalOrdered(ChargeSignal::Ready, 3);
		for (std::uint64_t sequence = 4; sequence < 10; ++sequence) {
			eventOnly.ActiveOrdered(5, true, true, dt, false, sequence);
		}
		check(eventOnly.DrawSequence() == 3 && FinishDraw(eventOnly), "constant false custom graph created an unproven observation fence");
		check(eventOnly.SignalRelease(5) && !FinishDraw(eventOnly), "constant false custom graph swallowed a real release");
	}

	void initialPollingCannotHideItsFirstRelease()
	{
		constexpr float dt = 1.f / 60.f;
		OrderedChargeState graph;
		for (std::uint64_t sequence = 10; sequence < 16; ++sequence) {
			graph.ActiveOrdered(10, true, true, dt, false, sequence);
		}
		check(FinishDraw(graph) && graph.DrawSequence() == 0, "initial false graph incorrectly proved an older draw cycle");
		check(graph.SignalRelease(1) && !FinishDraw(graph), "initial false graph swallowed its delayed first release");
		for (auto native : { 8u, 9u }) {
			OrderedChargeState state;
			state.ActiveOrdered(native, true, true, dt, std::nullopt, 10);
			for (std::uint64_t sequence = 11; sequence < 17; ++sequence) {
				state.ActiveOrdered(10, true, true, dt, std::nullopt, sequence);
			}
			check(FinishDraw(state) && state.DrawSequence() == 0, "initial native pull incorrectly proved an older draw cycle");
			check(state.SignalRelease(1) && !FinishDraw(state), "initial native pull swallowed its delayed first release");
		}
	}

	void explicitCancellationWinsOverLaterPolling()
	{
		constexpr float dt = 1.f / 60.f;
		for (auto native : { 8u, 9u }) {
			OrderedChargeState state;
			state.SignalOrdered(ChargeSignal::Stop, 90);
			state.ActiveOrdered(native, true, true, dt, std::nullopt, 102);
			check(!state.SignalRelease(101), "early draw observation accepted an older projectile release");

			check(state.SignalOrdered(ChargeSignal::Stop, 101), "observation fence rejected a delayed explicit cancellation");
			for (std::uint64_t sequence = 103; sequence < 123; ++sequence) {
				check(!state.ActiveOrdered(10, true, true, dt, std::nullopt, sequence), "delayed explicit cancellation preserved a pending native restart");
			}
			check(state.SignalOrdered(ChargeSignal::Stop, 123), "new explicit stop was rejected");
			check(!FinishDraw(state), "new explicit stop failed to keep charge cancelled");
		}
	}

	void stableMergeAndEmptyInputs()
	{
		struct Entry { std::uint64_t sequence; char label; };
		std::vector<Entry> events{ { 4, 'd' }, { 2, 'b' }, { 2, 'c' } };
		std::vector<Entry> samples{ { 5, 'f' }, { 1, 'a' }, { 4, 'e' } };
		std::string observed;
		auto append = [&](const Entry& entry) { observed += entry.label; };
		ReplayEvents(events, samples, append, append);
		check(observed == "abcdef", "merge did not preserve chronological and stable equal-sequence order");
		events.clear();
		samples.clear();
		observed.clear();
		ReplayEvents(events, samples, append, append);
		check(observed.empty(), "empty queues dispatched an event");
		events = { { 2, 'b' }, { 1, 'a' } };
		ReplayEvents(events, samples, append, append);
		check(observed == "ab", "event-only queue was not sorted");
		events.clear();
		samples = { { 2, 'd' }, { 1, 'c' } };
		ReplayEvents(events, samples, append, append);
		check(observed == "abcd", "sample-only queue was not sorted");
	}

	void completedCustomDrawSurvivesEarlyNativeState()
	{
		constexpr float dt = 1.f / 60.f;
		for (auto native : { 8u, 9u }) {
			OrderedChargeState state;
			state.SignalOrdered(ChargeSignal::Start, 1);
			state.SignalOrdered(ChargeSignal::Attach, 2);
			state.SignalOrdered(ChargeSignal::PullStart, 3);
			for (std::uint64_t sequence = 4; sequence < 124; ++sequence) {
				check(!state.ActiveOrdered(native, true, true, dt, false, sequence), "custom early native pull emitted before its completion event");
			}
			state.SignalOrdered(ChargeSignal::PullEnd, 124);
			check(!state.ActiveOrdered(native, true, true, dt, false, 125), "completed custom native pull skipped the readiness debounce");
			for (std::uint64_t sequence = 126; sequence < 132; ++sequence) {
				state.ActiveOrdered(native, true, true, dt, false, sequence);
			}
			check(state.ActiveOrdered(native, true, true, dt, false, 132), "paired custom pull completion was overwritten by a stale early native state");
			check(state.SignalRelease(133), "completed custom pull swallowed its current release");
			for (std::uint64_t sequence = 134; sequence < 154; ++sequence) {
				check(!state.ActiveOrdered(native, true, true, dt, false, sequence), "released custom pull revived from its stale early native state");
			}
			state.SignalOrdered(ChargeSignal::Attach, 154);
			state.SignalOrdered(ChargeSignal::PullStart, 155);
			state.SignalOrdered(ChargeSignal::PullEnd, 156);
			for (std::uint64_t sequence = 157; sequence < 164; ++sequence) {
				state.ActiveOrdered(native, true, true, dt, std::nullopt, sequence);
			}
			check(state.ActiveOrdered(native, true, true, dt, std::nullopt, 164), "next paired custom pull remained blocked after rapid release");
			state.SignalOrdered(ChargeSignal::PullStart, 165);
			check(!state.ActiveOrdered(native, true, true, dt, std::nullopt, 166), "new pull retained its preceding completion without another pull end");
			check(!state.ActiveOrdered(12, true, true, dt, true, 167), "native arrow release failed to cancel a completed custom pull");
		}
		for (auto signal : { ChargeSignal::Ready, ChargeSignal::PullEnd }) {
			OrderedChargeState noArrow;
			noArrow.SignalOrdered(ChargeSignal::Start, 1);
			if (signal == ChargeSignal::PullEnd) { noArrow.SignalOrdered(ChargeSignal::PullStart, 2); }
			noArrow.SignalOrdered(signal, 3);
			for (std::uint64_t sequence = 4; sequence < 24; ++sequence) {
				check(!noArrow.ActiveOrdered(8, true, false, dt, std::nullopt, sequence), "completion without arrow-attachment evidence enabled a custom cloud");
			}
		}
		OrderedChargeState quickRelease;
		quickRelease.SignalOrdered(ChargeSignal::Start, 1);
		quickRelease.SignalOrdered(ChargeSignal::Attach, 2);
		quickRelease.SignalOrdered(ChargeSignal::PullStart, 3);
		quickRelease.SignalOrdered(ChargeSignal::PullEnd, 4);
		check(!quickRelease.ActiveOrdered(8, true, true, dt, false, 5), "quick custom draw bypassed readiness debounce");
		for (std::uint64_t sequence = 6; sequence < 26; ++sequence) {
			check(!quickRelease.ActiveOrdered(11, true, true, dt, true, sequence), "quick release transition started a late custom cloud");
		}
		OrderedChargeState stale;
		stale.SignalOrdered(ChargeSignal::Start, 1);
		stale.SignalOrdered(ChargeSignal::Attach, 2);
		stale.SignalOrdered(ChargeSignal::Ready, 3);
		check(FinishDraw(stale), "event-proven hold did not start before the next native draw");
		for (std::uint64_t sequence = 4; sequence < 24; ++sequence) {
			check(!stale.ActiveOrdered(8, true, true, dt, true, sequence), "next native early draw retained an older event completion");
		}
	}

	void inGameHeldGraphSurvivesStaleBashState()
	{
		constexpr float dt = 1.f / 60.f;
		OrderedChargeState state;
		std::uint64_t sequence = 100;
		state.SignalOrdered(ChargeSignal::Stop, sequence++);
		for (int frame = 0; frame < 180; ++frame) {
			check(!state.ActiveOrdered(6, true, true, dt, false, sequence++), "native bash or early custom pull created a charge cloud");
		}
		check(!state.ActiveOrdered(6, true, true, dt, true, sequence++), "in-game full-draw graph bypassed readiness confirmation");
		for (int frame = 0; frame < 180; ++frame) {
			const bool active = state.ActiveOrdered(6, true, true, dt, true, sequence++);
			if (frame > 4) { check(active, "in-game native 6 overrode a completed nocked full-draw graph"); }
		}
		state.SignalOrdered(ChargeSignal::Stop, sequence++);
		for (int frame = 0; frame < 180; ++frame) {
			check(!state.ActiveOrdered(6, true, true, dt, true, sequence++), "release revived a cloud from an unchanged full-draw graph");
		}
		check(!state.ActiveOrdered(6, true, true, dt, false, sequence++), "next observed graph reset emitted a charge cloud");
		for (int frame = 0; frame < 6; ++frame) { state.ActiveOrdered(6, true, true, dt, true, sequence++); }
		check(state.ActiveOrdered(6, true, true, dt, true, sequence++), "next real full-draw graph edge stayed blocked after release");
		check(!state.ActiveOrdered(6, true, true, dt, false, sequence++), "leaving the confirmed full-draw graph failed to stop the cloud");
		for (auto drawn : { std::optional<bool>{ false }, std::optional<bool>{} }) {
			OrderedChargeState bash;
			bash.SignalOrdered(ChargeSignal::Start, 1);
			bash.SignalOrdered(ChargeSignal::Attach, 2);
			bash.SignalOrdered(ChargeSignal::Ready, 3);
			for (int frame = 0; frame < 30; ++frame) {
				check(!bash.ActiveOrdered(6, true, true, dt, drawn, frame + 4), "event readiness alone bypassed a real or unknown bash state");
			}
		}
		OrderedChargeState noNockedArrow;
		for (int frame = 0; frame < 30; ++frame) {
			check(!noNockedArrow.ActiveOrdered(6, true, false, dt, true, frame + 1), "full-draw graph without a nocked arrow bypassed the native bash guard");
		}
		for (auto released : { 11u, 12u, 13u, 14u }) {
			OrderedChargeState quick;
			quick.ActiveOrdered(6, true, true, dt, false, 1);
			quick.ActiveOrdered(6, true, true, dt, true, 2);
			for (int frame = 0; frame < 30; ++frame) {
				check(!quick.ActiveOrdered(released, true, true, dt, true, frame + 3), "rapid-release graph started a late gathering cloud");
			}
		}
	}
}

int main() try
{
	sameBatchDrawAndReleaseOrdering();
	delayedWorkerSampleAndNewCancellation();
	repeatedSamplesAndRendererRetry();
	polledDrawFencesOnlyOlderProjectileWork();
	initialPollingCannotHideItsFirstRelease();
	explicitCancellationWinsOverLaterPolling();
	completedCustomDrawSurvivesEarlyNativeState();
	inGameHeldGraphSurvivesStaleBashState();
	stableMergeAndEmptyInputs();
	std::cout << "PASS: ordered charge events, late worker samples, one-shot release consumption and renderer retries\n";
}
catch (const std::exception& error)
{
	std::cerr << "FAIL: " << error.what() << '\n';
	return 1;
}
