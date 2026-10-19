#pragma once
#ifdef REDSAND_TIME_TEST
#include "time_runtime_stubs.hpp"
#else
#include "util.hpp"
#endif
#include "block_time_curve.hpp"
#include "time_session.hpp"
#include <mutex>
template <typename easing_type,typename easing_func=void>
class untyped_slowdown_helper final {
public:
    bool can_run() const {std::lock_guard lock(mutex);return !curve.active;}
    bool is_running() const noexcept {std::lock_guard lock(mutex);return curve.active;}
    bool start(float mult,float dur,HellbladeTime::Source source=HellbladeTime::Source::TimedBlock) {
        std::lock_guard lock(mutex);
        if (!std::isfinite(mult) || !std::isfinite(dur) || dur<=0) return false;
        if (curve.active) {


            if (source <= origin) return false;
            curve.Reset();
        } else pending=true;
        if (!curve.Start(mult,dur)) return false;
        origin=source; ++serial;
        return true;
    }
    HellbladeTime::Session state() const {
        std::lock_guard lock(mutex);
        return {curve.active && !pending,curve.elapsed,curve.duration,serial,origin};
    }
    void update() {
        std::lock_guard lock(mutex);
        if(!curve.active) return;


        if(Util::BlockSlowdownSuspended()) return;


        if(pending) {
            pending=false;
            if(!Util::BeginBlockSlowdown()) {curve.Reset();return;}
        }
        float next=curve.Advance(*Offsets::g_DeltaRealTime);
        if(!curve.active) {Util::EndBlockSlowdown();return;}
        if(!Util::ApplyBlockSlowdown(next)) curve.Reset();
    }
    void stop() {
        std::lock_guard lock(mutex);
        if(!pending) Util::EndBlockSlowdown();
        pending=false;curve.Reset();
    }
    untyped_slowdown_helper() noexcept=default;
    untyped_slowdown_helper(const untyped_slowdown_helper&)=delete;
    untyped_slowdown_helper& operator=(const untyped_slowdown_helper&)=delete;
private:
    mutable std::mutex mutex;
    RedsandBlockTime::Curve curve;
    bool pending=false;
    HellbladeTime::Source origin=HellbladeTime::Source::TimedBlock;
    std::uint32_t serial=0;
};
template <typename T> using slowdown_helper=untyped_slowdown_helper<float,T>;
