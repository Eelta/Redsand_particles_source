#pragma once
#include <algorithm>
#include <cmath>
namespace RedsandBlockTime {
struct Curve {
    float elapsed=0,duration=0,minimum=1;
    bool active=false;
    bool Start(float mult,float seconds) noexcept {
        if(active || !std::isfinite(mult) || !std::isfinite(seconds) || seconds<=0) return false;
        elapsed=0;duration=seconds;minimum=std::clamp(mult,.05f,1.f);active=true;return true;
    }
    static float Smooth(float x) noexcept { x=std::clamp(x,0.f,1.f);return x*x*(3-2*x); }
    float Advance(float dt) noexcept {
        if(!active) return 1;
        if(std::isfinite(dt) && dt>0) elapsed=std::min(duration,elapsed+dt);
        const float enter=std::min(.04f,duration*.1f),release=duration*.72f;
        float result=minimum;
        if(elapsed<enter) result=1+(minimum-1)*Smooth(elapsed/enter);
        else if(elapsed>release) result=minimum+(1-minimum)*Smooth((elapsed-release)/(duration-release));
        if(elapsed>=duration) {active=false;result=1;}
        return result;
    }
    void Reset() noexcept {*this={};}
};
}
