#include "../source/RedsandBowSimulation.h"
#include "../source/RedsandBowChargeState.h"

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Redsand::Bow;

void check(bool condition, const char* message)
{
	if (!condition) { throw std::runtime_error(message); }
}

bool near(Vec3 a, Vec3 b, float epsilon = 0.002f) { return (a - b).Length() < epsilon; }

void chargeAndCancel()
{
	Simulation still(12), moving(12);
	still.Gather(0.1f, {}, 1.f, 1.f);
	moving.Gather(0.1f, {}, 1.f, 1.f);
	check(still.Particles().size() >= 95 && still.Particles().size() <= 110, "charge emission did not use the reduced density");
	const float originalDistance = still.Particles().front().position.Length();
	still.Advance(0.2f, {});
	check(still.Particles().front().position.Length() > originalDistance * 0.9f, "outer cloud collapsed before its floating phase");
	still.Advance(0.9f, {});
	const Vec3 newTip{ 50.f, -15.f, 20.f };
	moving.Advance(1.1f, newTip);
	check(still.Particles().front().position.Length() < originalDistance * 0.7f, "floating cloud did not subsequently converge toward the tip");
	check(near(moving.Particles().front().position, still.Particles().front().position + newTip), "charge did not follow moving arrow tip");
	const auto before = moving.Particles();
	moving.StopGather();
	check(before.size() == moving.Particles().size(), "cancelling created a burst or deleted live particles");
	for (std::size_t i = 0; i < before.size(); ++i) {
		check(std::abs(before[i].Alpha() - moving.Particles()[i].Alpha()) < 0.0001f, "cancel caused an alpha pop");
	}
	moving.Advance(0.05f, { 1000.f, 1000.f, 1000.f });
	check(near(before.front().position, moving.Particles().front().position), "cancelled cloud followed the shooter");
	check(moving.Particles().front().Alpha() < before.front().Alpha(), "cancelled cloud did not fade");
	moving.Advance(1.f, {});
	check(moving.Empty(), "cancelled charge leaked");
}

void sustainedCloud()
{
	Simulation cloud(124);
	cloud.BeginGather({}, 1.f);
	check(cloud.Particles().size() >= 160 && cloud.Particles().size() <= 200, "late draw seed count is outside the reduced range");
	for (const auto& p : cloud.Particles()) {
		check(p.Alpha() > 0.9f, "seeded cloud is invisible");
		check(p.position.Length() > 26.f && p.position.Length() < 72.f, "floating cloud exceeds the compact gathering range");
		check(p.radius >= 0.72f && p.radius <= 1.43f, "gather particles exceed the reduced size range");
	}
	for (int frame = 0; frame < 1800; ++frame) {
		cloud.Advance(1.f / 60.f, {});
		cloud.Gather(1.f / 60.f, {}, 1.f, 1.f);
	}
	check(cloud.Particles().size() > 1500 && cloud.Particles().size() < 2300, "long aim must sustain the reduced cloud density");
	unsigned floating = 0, converging = 0, atTip = 0;
	for (const auto& p : cloud.Particles()) {
		if (p.age < p.floatTime) { ++floating; }
		else if (p.position.Length() > 5.f) { ++converging; }
		else { ++atTip; }
	}
	check(floating > 300 && converging > 300 && atTip > 70, "aiming must show floating, inward and tip particles together");
	cloud.StopGather();
	cloud.Advance(2.2f, {});
	check(cloud.Empty(), "dense cloud did not drain after cancel");
}

void chargeDetection()
{
	constexpr float dt = 1.f / 60.f;
	ChargeState state;
	check(!state.Active(0, false, true, dt), "idle arrow node enabled charge");
	state.Signal(ClassifyChargeEvent("BowDraw"));

	for (int frame = 0; frame < 104; ++frame) {
		if (frame == 18) { state.Signal(ClassifyChargeEvent("arrowAttach")); }
		if (frame == 52) { state.Signal(ClassifyChargeEvent("SoundPlay.WPNBowPullLPM")); }
		check(!state.Active(5, true, true, dt), "raising/nocking/early drawing created particles before the late stage");
	}
	state.Signal(ClassifyChargeEvent("SoundStop.WPNBowPullLPM"));
	check(!state.Active(5, true, true, dt), "late draw ignored the short readiness debounce");
	for (int frame = 0; frame < 4; ++frame) { state.Active(5, true, true, dt); }
	check(state.Active(5, true, true, dt), "completed custom pull did not enable the cloud");
	state.Signal(ClassifyChargeEvent("BowZoomStop"));
	for (int i = 0; i < 600; ++i) { check(state.Active(5, false, true, dt), "zoom/long aim lost the completed draw state"); }
	state.Signal(ClassifyChargeEvent("SBF_BowAttackStop"));
	check(!state.Active(10, true, true, dt), "cancel did not override stale draw state");
	check(!state.Active(11, true, true, dt), "release-transition state restarted cancelled cloud");
	check(!state.Active(0, false, false, dt), "cancel emitted during reset");
	state.Signal(ClassifyChargeEvent("bowDrawStart"));
	for (auto native : { 8u, 9u, 11u }) {
		for (int frame = 0; frame < 120; ++frame) { check(!state.Active(native, true, true, dt), "early/release native state enabled charge"); }
	}
	for (int frame = 0; frame < 4; ++frame) { state.Active(10, false, false, dt); }
	check(state.Active(10, false, false, dt), "native completed draw cannot use a custom-node fallback");
	check(state.Active(11, false, true, dt), "release transition lost an existing late cloud");
	state.Signal(ChargeSignal::Stop);
	check(!state.Active(10, true, true, dt), "actual release restarted charge from stale animation state");
	state = {};
	state.Signal(ClassifyChargeEvent("SoundStop.WPNBowPullLPM"));
	for (int i = 0; i < 120; ++i) { check(!state.Active(5, true, true, dt), "a lone sound stop or attacking graph enabled charge"); }
	state.Signal(ClassifyChargeEvent("BOWDRAWN"));
	for (int i = 0; i < 120; ++i) { check(!state.Active(5, false, false, dt), "ready event without a nocked arrow enabled charge"); }
	for (int i = 0; i < 4; ++i) { state.Active(5, false, true, dt); }
	check(state.Active(5, false, true, dt), "case-insensitive full-draw event not recognized");
	check(!state.Active(6, true, true, dt), "bow bash failed to clear charge");
	state.Signal(ClassifyChargeEvent("arrowDetach"));
	check(!state.Active(10, false, false, dt), "arrow detach failed to cancel");
	state = {};
	state.Signal(ClassifyChargeEvent("bowDrawStart"));
	state.Signal(ClassifyChargeEvent("arrowAttach"));
	state.Signal(ClassifyChargeEvent("SoundPlay.WPNBowPullLPM"));
	state.Signal(ClassifyChargeEvent("SoundStop.WPNBowPullLPM"));
	check(!state.Active(5, true, true, dt), "rapid shot emitted a transient charge cloud");
	state.Signal(ChargeSignal::Stop);
	for (int i = 0; i < 20; ++i) { check(!state.Active(5, true, true, dt), "quick release left a delayed cloud"); }
	state = {};
	check(!state.Active(0, false, false, dt), "reset retained a previous charge");
}

void nativeDrawRestartAndDuplicateStarts()
{
	constexpr float dt = 1.f / 60.f;
	auto finishNativeDraw = [&](ChargeState& state) {
		check(!state.Active(10, true, true, dt), "new native full draw skipped the readiness debounce");
		for (int i = 0; i < 4; ++i) { state.Active(10, true, true, dt); }
		check(state.Active(10, true, true, dt), "new native full draw remained blocked");
	};
	ChargeState state;

	for (auto native : { 8u, 9u }) {
		for (int i = 0; i < 20; ++i) { check(!state.Active(native, true, true, dt), "normal native early draw enabled particles"); }
	}
	finishNativeDraw(state);
	state.Signal(ChargeSignal::Stop);
	for (auto native : { 10u, 11u, 10u }) {
		for (int i = 0; i < 20; ++i) { check(!state.Active(native, true, true, dt), "stale released native state revived the cloud"); }
	}

	for (auto native : { 8u, 9u }) {
		for (int i = 0; i < 20; ++i) { check(!state.Active(native, true, true, dt), "native restart enabled particles before full draw"); }
	}
	finishNativeDraw(state);
	state.Signal(ChargeSignal::Stop);
	check(!state.Active(9, true, true, dt), "direct native attach enabled particles");
	finishNativeDraw(state);

	state.Signal(ChargeSignal::Stop);
	check(!state.Active(8, true, true, dt), "interrupted restart emitted during draw");
	check(!state.Active(11, true, true, dt), "interrupted restart emitted during release");
	for (int i = 0; i < 20; ++i) { check(!state.Active(10, true, true, dt), "interrupted native restart survived release"); }

	for (auto native : { 8u, 9u }) {
		ChargeState cancelled;
		cancelled.Active(10, true, true, dt);
		cancelled.Signal(ChargeSignal::Stop);
		check(!cancelled.Active(native, true, true, dt), "new native pull emitted before cancellation");
		cancelled.Signal(ChargeSignal::Stop);
		for (int i = 0; i < 20; ++i) {
			check(!cancelled.Active(10, true, true, dt), "real cancellation retained a pending native restart");
		}
		check(!cancelled.Active(native, true, true, dt), "next native pull emitted before full draw");
		finishNativeDraw(cancelled);
	}

	state = {};
	state.Active(8, true, true, dt);
	state.Signal(ChargeSignal::Stop);
	for (int i = 0; i < 20; ++i) { check(!state.Active(8, true, true, dt), "cancelled native draw emitted particles"); }
	for (int i = 0; i < 20; ++i) { check(!state.Active(10, true, true, dt), "unchanged cancelled draw counted as a fresh cycle"); }
	check(!state.Active(8, true, true, dt), "next native draw emitted particles early");
	finishNativeDraw(state);

	state = {};
	state.Signal(ChargeSignal::Start);
	state.Signal(ChargeSignal::PullStart);
	state.Signal(ChargeSignal::Start);
	state.Signal(ChargeSignal::PullEnd);
	for (int i = 0; i < 5; ++i) { state.Active(5, true, true, dt); }
	check(state.Active(5, true, true, dt), "duplicate start discarded the observed pull");
	state.Signal(ChargeSignal::Start);
	check(state.Active(5, true, true, dt), "duplicate start discarded an already completed draw");
	state.Signal(ChargeSignal::Stop);
	state.Signal(ChargeSignal::Start);
	for (int i = 0; i < 20; ++i) { check(!state.Active(5, true, true, dt), "fresh start after stop retained old readiness"); }
	state.Signal(ChargeSignal::PullStart);
	state.Signal(ChargeSignal::PullEnd);
	for (int i = 0; i < 5; ++i) { state.Active(5, true, true, dt); }
	check(state.Active(5, true, true, dt), "fresh start after stop could not complete");
	state = {};
	state.Signal(ChargeSignal::PullStart);
	state.Signal(ChargeSignal::Start);
	state.Signal(ChargeSignal::PullEnd);
	for (int i = 0; i < 5; ++i) { state.Active(5, true, true, dt); }
	check(state.Active(5, true, true, dt), "late actor start discarded the graph's earlier pull start");
}

void cameraSwitchRecovery()
{
	constexpr float dt = 1.f / 60.f;
	ChargeState state;
	ChargeView view;
	check(view.Refresh(false, 100, 1000), "initial third-person graph not selected");
	const auto oldThirdEpoch = view.epoch;
	state.Signal(ChargeSignal::Start);
	state.Signal(ChargeSignal::Attach);
	state.Signal(ChargeSignal::Ready);
	for (int i = 0; i < 5; ++i) { state.Active(5, true, true, dt); }
	check(state.Active(5, true, true, dt), "third-person hold never became ready");
	check(view.Refresh(true, 200, 2000), "first-person switch not detected");
	state.OnViewChanged();
	check(!view.Accepts(1000, oldThirdEpoch), "old third-person cancellation crossed into first person");
	check(view.Accepts(2000, view.epoch), "current first-person graph rejected");
	for (int i = 0; i < 5; ++i) { state.Active(5, true, true, dt); }
	check(state.Active(5, true, true, dt), "held draw did not follow the first-person skeleton");
	check(!view.Refresh(true, 200, 2000), "unchanged camera reset the cloud");
	state.Signal(ChargeSignal::Stop);
	check(!state.Active(5, true, true, dt), "first-person shot did not end charge");

	state.Signal(ClassifyChargeEvent("arrowAttach"));
	state.Signal(ClassifyChargeEvent("SoundPlay", "WPNBowPullLPM"));
	for (int i = 0; i < 100; ++i) { check(!state.Active(5, true, true, dt), "recovery enabled particles during the early pull"); }
	state.Signal(ClassifyChargeEvent("SoundStop", "WPNBowPullLPM"));
	for (int i = 0; i < 5; ++i) { state.Active(5, true, true, dt); }
	check(state.Active(5, true, true, dt), "next first-person draw remained blocked after release");
	const auto firstEpoch = view.epoch;
	state.Signal(ChargeSignal::Stop);
	check(view.Refresh(false, 100, 1000), "return to third person not detected");
	state.OnViewChanged();
	check(!view.Accepts(2000, firstEpoch), "old first-person event affected third person");
	check(!view.Accepts(1000, oldThirdEpoch), "old event survived a round trip to the same graph address");
	check(!state.Active(5, true, true, dt), "switching after release restored an old cloud");
	state.Signal(ClassifyChargeEvent("SoundPlay.WPNBowPullLPM"));
	for (int i = 0; i < 100; ++i) { check(!state.Active(5, true, true, dt), "returning camera bypassed the late-stage gate"); }
	state.Signal(ClassifyChargeEvent("SoundStop.WPNBowPullLPM"));
	for (int i = 0; i < 5; ++i) { state.Active(5, true, true, dt); }
	check(state.Active(5, true, true, dt), "third-person draw failed after the camera round trip");
	check(view.Refresh(false, 300, 3000), "replacement animation graph was not detected");
	state.OnViewChanged();
	check(!view.Accepts(1000, view.epoch), "replaced graph still accepted events");
	check(view.Accepts(0, 0), "camera switch rejected an actor-wide release/sheath");
	state.Signal(ChargeSignal::Stop);
	state.Signal(ClassifyChargeEvent("SoundStop", "WPNBowPullLPM"));
	check(!state.Active(5, true, true, dt), "stale sound stop restarted charge after release");
	check(ClassifyChargeEvent("SoundStop", "UnrelatedSound") == ChargeSignal::None, "unrelated sound completed a bow draw");
}

void pollMissingEventsRecovery()
{
	constexpr float dt = 1.f / 60.f;
	auto completePolledDraw = [&](ChargeState& state) {
		check(!state.Active(5, true, true, dt, true), "polled full draw skipped the readiness debounce");
		for (int i = 0; i < 4; ++i) { state.Active(5, true, true, dt, true); }
		check(state.Active(5, true, true, dt, true), "polled full draw could not recover missing animation events");
	};
	ChargeState state;

	for (int i = 0; i < 120; ++i) {
		check(!state.Active(5, true, true, dt, false), "early draw polling enabled charge before the held-bow state");
	}
	completePolledDraw(state);
	for (int shot = 0; shot < 3; ++shot) {
		state.Signal(ChargeSignal::Stop);
		for (int i = 0; i < 120; ++i) {
			check(!state.Active(5, true, true, dt, true), "unchanged held-bow graph revived a released arrow");
		}
		for (int i = 0; i < 30; ++i) {
			check(!state.Active(5, true, true, dt, false), "next polled draw emitted before it reached full draw");
		}
		completePolledDraw(state);
	}

	ChargeView view;
	check(view.Refresh(false, 100, 1000), "polled third-person graph was not selected");
	for (bool first : { true, false }) {
		check(view.Refresh(first, first ? 200 : 100, first ? 2000 : 1000), "polled camera switch was not detected");
		state.OnViewChanged();
		completePolledDraw(state);
	}

	state.Signal(ChargeSignal::Stop);
	for (bool first : { true, false }) {
		check(view.Refresh(first, first ? 200 : 100, first ? 2000 : 1000), "released camera switch was not detected");
		state.OnViewChanged();
		for (int i = 0; i < 120; ++i) {
			check(!state.Active(5, true, true, dt, true), "camera switch revived an already released held-bow graph");
		}
	}
	check(!state.Active(5, true, true, dt, false), "return to third person bypassed the early draw gate");
	completePolledDraw(state);


	state.Signal(ChargeSignal::Stop);
	for (auto attacking : { std::optional<bool>{ true }, std::optional<bool>{} }) {
		for (int i = 0; i < 30; ++i) {
			check(!state.Active(5, attacking, true, dt, std::nullopt), "missing graph values cleared a release fence");
		}
		for (int i = 0; i < 30; ++i) {
			check(!state.Active(5, true, true, dt, true), "missing held-bow value was treated as a completed old draw");
		}
	}
	check(!state.Active(5, true, true, dt, false), "observed graph reset emitted a cloud");
	completePolledDraw(state);
	check(!state.Active(5, true, true, dt, false), "known held-bow graph leaving full draw did not cancel charge");
	for (int i = 0; i < 120; ++i) {
		check(!state.Active(5, true, true, dt, false), "missed release event left a cloud after the held-bow graph cleared");
	}
	state = {};
	completePolledDraw(state);
	check(!state.Active(10, true, true, dt, false), "graph cancellation lost to a stale native full-draw state");
	for (int i = 0; i < 120; ++i) {
		check(!state.Active(10, true, true, dt, false), "stale native full draw revived a graph-cancelled cloud");
	}
	state.Signal(ChargeSignal::Stop);
	check(!state.Active(5, true, true, dt, false), "released graph reset emitted particles");
	check(!state.Active(11, true, true, dt, true), "release-transition graph unlocked a stopped draw");
	for (int i = 0; i < 120; ++i) {
		check(!state.Active(10, true, true, dt, true), "release-transition graph preserved a restart for stale native full draw");
	}


	state = {};
	state.Signal(ClassifyChargeEvent("SoundPlay.WPNBowPullLPM"));
	for (int i = 0; i < 120; ++i) {
		check(!state.Active(5, true, true, dt, false), "custom pull emitted before its completion event");
	}
	state.Signal(ClassifyChargeEvent("SoundStop.WPNBowPullLPM"));
	for (int i = 0; i < 5; ++i) { state.Active(5, true, true, dt, false); }
	check(state.Active(5, true, true, dt, false), "constant false graph variable disabled custom pull completion");
	for (int i = 0; i < 120; ++i) {
		check(state.Active(5, true, true, dt, false), "constant false graph variable cancelled a custom event-only hold");
	}


	for (auto drawn : { std::optional<bool>{ false }, std::optional<bool>{} }) {
		state = {};
		for (int i = 0; i < 120; ++i) {
			check(!state.Active(5, true, false, dt, drawn), "early/unknown graph created particles without a nocked arrow");
		}
	}
	state = {};
	check(!state.Active(5, true, false, dt, true), "custom-node held-bow fallback skipped readiness debounce");
	for (int i = 0; i < 5; ++i) { state.Active(5, true, false, dt, true); }
	check(state.Active(5, true, false, dt, true), "verified held-bow graph could not use a custom-node fallback");
	for (auto native : { 8u, 9u, 11u }) {
		state = {};
		for (int i = 0; i < 120; ++i) {
			check(!state.Active(native, true, true, dt, true), "polled graph bypassed an early or release-transition native state");
		}
	}
	for (auto native : { 8u, 9u }) {
		state = {};
		completePolledDraw(state);
		for (int i = 0; i < 120; ++i) {
			check(!state.Active(native, true, true, dt, true), "a new early native draw retained the previous held-bow readiness");
		}
	}

	state = {};
	completePolledDraw(state);
	state.OnViewChanged();
	check(!state.Active(5, true, true, dt, false), "camera carry skipped its readiness debounce");
	for (int i = 0; i < 5; ++i) { state.Active(5, true, true, dt, false); }
	check(state.Active(5, true, true, dt, false), "old graph confirmation cancelled a held custom third-person draw");
	state.Signal(ChargeSignal::Stop);
	check(!state.Active(5, true, true, dt, false), "actor release did not stop the carried custom draw");
}

void ringShapeAndTime()
{
	for (Vec3 aim : { Vec3{ 0.f, 1.f, 0.f }, Vec3{ 0.f, 0.f, 1.f }, Vec3{ 1.f, -2.f, -3.f }.Unit() }) {
		Simulation ring(37);
		const Vec3 origin{ 200.f, -100.f, 60.f };
		ring.Ring(origin, aim, 1.f, 1.f);
		check(ring.Particles().size() >= 360 && ring.Particles().size() <= 410, "compact release ring did not gain particle density");
		for (const auto& p : ring.Particles()) {
			const auto offset = p.position - origin;
			check(std::abs(offset.Dot(aim)) < 0.001f, "release ring is not perpendicular to flight");
			check(p.velocity.Dot(aim) >= 64.f, "release lacks forward push");
			check(p.velocity.Dot(offset.Unit()) > 150.f, "release lacks outward expansion");
			check(p.radius >= 1.1f && p.radius <= 1.95f, "release particles exceed the reduced size range");
		}
		const float initialRadius = (ring.Particles().front().position - origin).Length();
		ring.Advance(0.2f, {});
		check((ring.Particles().front().position - origin).Length() > initialRadius + 20.f, "release ring did not expand rapidly");
		ring.Advance(0.3f, {});
		for (const auto& p : ring.Particles()) {
			const Vec3 offset = p.position - origin;
			const float radius = (offset - aim * offset.Dot(aim)).Length();
			check(radius > 52.f && radius < 72.f, "release ring failed the reduced-radius requirement");
		}
	}
	Simulation lowFPS(17), highFPS(17);
	lowFPS.Ring({}, { 0.f, 1.f, 0.f }, 1.f, 1.f);
	highFPS.Ring({}, { 0.f, 1.f, 0.f }, 1.f, 1.f);
	for (int i = 0; i < 15; ++i) { lowFPS.Advance(1.f / 30.f, {}); }
	for (int i = 0; i < 72; ++i) { highFPS.Advance(1.f / 144.f, {}); }
	for (std::size_t i = 0; i < lowFPS.Particles().size(); ++i) {
		check(near(lowFPS.Particles()[i].position, highFPS.Particles()[i].position), "ring speed depends on frame rate");
		check(std::abs(lowFPS.Particles()[i].Alpha() - highFPS.Particles()[i].Alpha()) < 0.0001f, "fade depends on frame rate");
	}
}

void trajectorySampling()
{
	Simulation single(89), split(89), sparse(89);
	single.Trail({}, { 900.f, 0.f, 0.f }, 0.1f, 1.f);
	for (int i = 0; i < 10; ++i) {
		split.Advance(0.01f, {});
		split.Trail({ i * 90.f, 0.f, 0.f }, { (i + 1) * 90.f, 0.f, 0.f }, 0.01f, 1.f);
	}
	sparse.Trail({}, { 900.f, 0.f, 0.f }, 0.1f, 0.25f);
	check(single.Particles().size() == 240 && sparse.Particles().size() == 60, "distance-based density mismatch");
	check(single.Particles().size() == split.Particles().size(), "trajectory density depends on frame rate");
	for (std::size_t i = 0; i < single.Particles().size(); ++i) {
		check(single.Particles()[i].radius >= 0.65f && single.Particles()[i].radius <= 1.25f, "trail particles exceed the reduced size range");
		check(near(single.Particles()[i].position, split.Particles()[i].position), "trajectory sampling is not continuous across updates");
		check(std::abs(single.Particles()[i].Alpha() - split.Particles()[i].Alpha()) < 0.0001f, "particle birth times lost within a flight segment");
	}
	const Vec3 oldPosition = single.Particles().front().position;
	single.Advance(0.1f, { 5000.f, 5000.f, 5000.f });
	check((single.Particles().front().position - oldPosition).Length() < 1.f, "trail moved with the projectile");
	single.Advance(Simulation::maxLifetime, {});
	check(single.Empty() && !split.Empty(), "one arrow reset another arrow's lifetime");
}

void retirementAndLimits()
{
	Simulation flight;
	for (int i = 0; i < 3600; ++i) {
		flight.Advance(1.f / 60.f, {});
		flight.Trail({ i * 60.f, 0.f, 0.f }, { (i + 1) * 60.f, 0.f, 0.f }, 1.f / 60.f, 1.f);
		check(flight.Particles().size() <= Simulation::capacity, "long arrow flight exceeded particle cap");
	}
	check(!flight.Empty(), "continuous flight lost all particles");
	for (const auto& p : flight.Particles()) {
		check(std::isfinite(p.Alpha()) && p.Alpha() >= 0.f && p.Alpha() <= 1.f, "invalid alpha");
		check(p.position.Finite(), "invalid particle position");
	}

	flight.Advance(0.05f, {});
	check(!flight.Empty(), "tail disappeared at impact");
	flight.Advance(Simulation::maxLifetime, {});
	check(flight.Empty(), "tail survived beyond maximum lifetime");
	for (int i = 0; i < 100; ++i) { flight.Ring({}, {}, 1.f, 1.f); }
	check(flight.Particles().size() == Simulation::capacity, "overload was not bounded");
	flight.Advance(10.f, {});
	check(flight.Empty(), "overloaded particles did not drain");
	flight.Trail({}, { 3000.f, 0.f, 0.f }, 0.1f, 1.f);
	check(flight.Empty(), "teleport produced a line across the map");
	const float nan = std::numeric_limits<float>::quiet_NaN();
	flight.Gather(nan, {}, 1.f, 1.f);
	flight.Trail({}, {}, nan, 1.f);
	flight.Ring({ nan, 0.f, 0.f }, {}, 1.f, 1.f);
	flight.Gather(0.1f, {}, nan, 1.f);
	check(flight.Empty(), "non-finite inputs created particles");
	flight.Gather(120.f, {}, 1.f, 1.f);
	check(flight.Particles().size() <= 110, "long frame created an emission spike");
}

void vanillaBowTrajectory()
{
	Simulation single(89), split(89), fallback(89);
	single.SetStyle(TrailStyle::VanillaBow);
	split.SetStyle(TrailStyle::VanillaBow);
	single.Trail({}, { 900.f, 0.f, 0.f }, 0.1f, 1.f);
	fallback.Trail({}, { 900.f, 0.f, 0.f }, 0.1f, 1.f);
	for (int i = 0; i < 10; ++i) {
		split.Advance(0.01f, {});
		split.Trail({ i * 90.f, 0.f, 0.f }, { (i + 1) * 90.f, 0.f, 0.f }, 0.01f, 1.f);
	}
	check(single.Particles().size() == 300 && fallback.Particles().size() == 240,
		"vanilla and mod bows did not retain their separate emission spacing");
	check(single.Particles().size() == split.Particles().size(), "vanilla bow density depends on frame rate");
	for (std::size_t i = 0; i < single.Particles().size(); ++i) {
		const auto& p = single.Particles()[i];
		check(near(p.position, split.Particles()[i].position), "vanilla bow trail disconnected at a frame boundary");
		check(std::abs(p.Alpha() - split.Particles()[i].Alpha()) < 0.0001f, "vanilla birth time depends on frame rate");
		check(p.radius >= 0.48f && p.radius <= 1.f, "vanilla bow lost its narrow spark size");

		check(Vec3{ 0.f, p.position.y, p.position.z }.Length() < 0.4f,
			"vanilla bow particles deviated from their native projectile path");
	}
	single.Advance(Simulation::maxLifetime + 0.1f, {});
	check(single.Empty(), "vanilla bow tail did not drain after flight stopped");
	single.Trail({}, { 3000.f, 0.f, 0.f }, 0.1f, 1.f);
	check(single.Empty(), "vanilla bow tail crossed a teleport gap");
}

void diagonalBowTrailStaysNearItsNativePath()
{
	Simulation flight(284);
	flight.SetStyle(TrailStyle::VanillaBow);
	std::vector<Vec3> path{ {} };
	for (const float z : { 0.898f, 0.8995f, 0.9005f, 0.902f }) {
		const Vec3 next = path.back() + Vec3{ std::sqrt(1.f - z * z), 0.f, z } * 30.f;
		flight.Advance(0.001f, {});
		flight.Trail(path.back(), next, 0.001f, 1.f, true);
		path.push_back(next);
	}
	check(flight.Particles().size() >= 39, "diagonal native bow path lost its usual particle density");
	for (const auto& particle : flight.Particles()) {
		float nearest = std::numeric_limits<float>::max();
		for (std::size_t segment = 1; segment < path.size(); ++segment) {
			const Vec3 direction = path[segment] - path[segment - 1];
			const float t = std::clamp((particle.position - path[segment - 1]).Dot(direction) / direction.Dot(direction), 0.f, 1.f);
			nearest = std::min(nearest, (particle.position - (path[segment - 1] + direction * t)).Length());
		}
		check(nearest < 0.3f, "bow trail jumped off its native path when its flight direction crossed the old basis threshold");
	}
	for (std::size_t i = 1; i < flight.Particles().size(); ++i) {
		check((flight.Particles()[i].position - flight.Particles()[i - 1].position).Length() < 3.55f,
			"diagonal bow trail developed a visible gap between adjacent particle births");
	}
}

void vanillaCrossbowTrajectory()
{
	Simulation single(89), split(89), fallback(89);
	single.SetStyle(TrailStyle::VanillaCrossbow);
	split.SetStyle(TrailStyle::VanillaCrossbow);
	single.Trail({}, { 900.f, 0.f, 0.f }, 0.1f, 1.f);
	fallback.Trail({}, { 900.f, 0.f, 0.f }, 0.1f, 1.f);
	for (int i = 0; i < 10; ++i) {
		split.Advance(0.01f, {});
		split.Trail({ i * 90.f, 0.f, 0.f }, { (i + 1) * 90.f, 0.f, 0.f }, 0.01f, 1.f);
	}
	check(single.Particles().size() == 360 && single.Particles().size() == split.Particles().size(),
		"crossbow straight trail density depends on frame rate or ignores its spacing");
	for (std::size_t i = 0; i < single.Particles().size(); ++i) {
		const auto& p = single.Particles()[i];
		check(near(p.position, split.Particles()[i].position), "crossbow trail disconnected at a frame boundary");
		check(Vec3{ 0.f, p.position.y, p.position.z }.Length() < 0.5f, "crossbow trail is not narrow and straight");
		check(p.radius >= 0.4f && p.radius <= 0.78f, "crossbow spark size is outside its profile");
	}
	single.Advance(1.8f, {});
	fallback.Advance(1.8f, {});
	check(single.Empty() && !fallback.Empty(), "crossbow did not have a shorter tail than the mod fallback");
}

int main() try
{
	{
		Simulation standard(19), expanded(19);
		standard.Ring({}, { 0.f, 1.f, 0.f }, 2.f, 1.f);
		expanded.Ring({}, { 0.f, 1.f, 0.f }, 8.f, 1.f);
		check(expanded.Particles().size() == standard.Particles().size() * 4 && expanded.Particles().size() <= Simulation::capacity,
			"expanded bow density was silently clamped to the old limit");
	}
	chargeAndCancel();
	sustainedCloud();
	chargeDetection();
	nativeDrawRestartAndDuplicateStarts();
	cameraSwitchRecovery();
	pollMissingEventsRecovery();
	ringShapeAndTime();
	trajectorySampling();
	retirementAndLimits();
	diagonalBowTrailStaysNearItsNativePath();
	vanillaBowTrajectory();
	vanillaCrossbowTrajectory();
	std::cout << "PASS: charge convergence/cancellation, directional ring, frame-independent trajectory/fade, density, lifetime and bounds\n";
}
catch (const std::exception& error)
{
	std::cerr << "FAIL: " << error.what() << '\n';
	return 1;
}
