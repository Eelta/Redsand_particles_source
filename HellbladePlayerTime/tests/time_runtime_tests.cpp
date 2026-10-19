#include "time_runtime_stubs.hpp"
#include <stdexcept>
#include <iostream>
#include <utility>
void Check(bool b) {if(!b) throw std::runtime_error("Time ownership regression");}
int main() {
    paused=true;Check(Util::BlockSlowdownSuspended());Check(!Util::BeginBlockSlowdown());Check(timeWrites==0);paused=false;
    Check(Util::BeginBlockSlowdown());Check(Util::ApplyBlockSlowdown(.2f));
    Check(globalRate==.2f && RE::vats.playerMagicTimeSlowdown==1);
    const auto writesBeforePause=timeWrites;paused=true;
    Check(Util::ApplyBlockSlowdown(.6f));Check(timeWrites==writesBeforePause && globalRate==.2f);
    paused=false;Check(Util::ApplyBlockSlowdown(.2f));Check(timeWrites==writesBeforePause+1);

    globalRate=1;Check(Util::ApplyBlockSlowdown(.2f));Check(globalRate==.2f);
    Util::EndBlockSlowdown();Check(globalRate==1 && RE::vats.magicTimeSlowdown==0 && RE::vats.playerMagicTimeSlowdown==0);
    globalRate=.5f;Check(!Util::BeginBlockSlowdown());globalRate=1;
    RE::vats.magicTimeSlowdown=.4f;RE::vats.playerMagicTimeSlowdown=.8f;
    Check(!Util::BeginBlockSlowdown());Util::EndBlockSlowdown();Check(RE::vats.magicTimeSlowdown==.4f);
    RE::vats={};Check(Util::BeginBlockSlowdown());Check(Util::ApplyBlockSlowdown(.3f));
    RE::vats.magicTimeSlowdown=.7f;RE::vats.playerMagicTimeSlowdown=.9f;
    Check(!Util::ApplyBlockSlowdown(.3f));Util::EndBlockSlowdown();Check(RE::vats.magicTimeSlowdown==.7f && RE::vats.playerMagicTimeSlowdown==.9f);
    RE::vats={};globalRate=.1f;valhallaLoaded=true;
    Check(Util::BeginBlockSlowdown());Check(Util::ApplyBlockSlowdown(.2f));Util::EndBlockSlowdown();Check(globalRate==1);
    for(const auto pair : {std::pair{0.f,1.f},std::pair{0.f,.4f},std::pair{1.f,1.f}}) {
        RE::vats={};RE::vats.magicTimeSlowdown=pair.first;RE::vats.playerMagicTimeSlowdown=pair.second;
        globalRate=1;
        Check(Util::BeginBlockSlowdown());Check(Util::ApplyBlockSlowdown(.08f));
        Check(RE::vats.playerMagicTimeSlowdown==1 && globalRate==.08f);
        Util::EndBlockSlowdown();Check(globalRate==1);
        Check(RE::vats.magicTimeSlowdown==pair.first && RE::vats.playerMagicTimeSlowdown==pair.second);
    }
    RE::vats={};RE::vats.magicTimeSlowdown=1;RE::vats.playerMagicTimeSlowdown=.4f;
    Check(!Util::BeginBlockSlowdown());RE::vats={};
    RE::vats.mode=RE::VATS::VATS_MODE::kKillCam;Check(!Util::BeginBlockSlowdown());RE::vats={};
    for(const float world : {.05f,.3f,.8f,1.f,2.f}) {
        Check(Util::BeginBlockSlowdown());Check(Util::ApplyBlockSlowdown(world));
        Check(RE::vats.playerMagicTimeSlowdown==1.f);
        Check(RE::vats.magicTimeSlowdown==std::clamp(world,.05f,1.f));
        Util::EndBlockSlowdown();
    }
    Check(foreignModuleQueries==0);
    RE::vatsAvailable=false;Check(!Util::BeginBlockSlowdown());
    std::cout<<"Production time adapter: player always 1x, world slowdown, restore, foreign ownership, Valhalla overlap and no Redsand dependency passed.\n";
}
