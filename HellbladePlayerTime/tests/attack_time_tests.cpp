#include "time_session.hpp"
#include "slowdown_helper.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace Offsets { inline float realDelta=.02f;inline float* g_DeltaRealTime=&realDelta; }
void Check(bool value) {if(!value) throw std::runtime_error("Attack-time/session regression");}
int main() {
    using namespace HellbladeTime;
    SwingSignals signals;
    Check(ClassifySwing("Collision_AttackStart")==SwingSignal::CollisionStart);
    Check(ClassifySwing("weaponLeftSwing")==SwingSignal::GraphSwing);
    Check(!ClassifySwing("Collision_Add"));Check(!ClassifySwing("BowRelease"));
    Check(signals.Accept(SwingSignal::CollisionStart,1));
    Check(!signals.Accept(SwingSignal::EngineSwing,1.3));
    Check(!signals.Accept(SwingSignal::GraphSwing,1.31));
    Check(!signals.Accept(SwingSignal::CollisionEnd,1.5));
    Check(signals.Accept(SwingSignal::CollisionStart,1.6));
    Check(!signals.Accept(SwingSignal::CollisionStart,1.61));
    Check(!signals.Accept(SwingSignal::GraphSwing,1.9));
    Check(!signals.Accept(SwingSignal::CollisionEnd,2));
    Check(signals.Accept(SwingSignal::EngineSwing,2.1));
    Check(!signals.Accept(SwingSignal::GraphSwing,2.11));
    Check(signals.Accept(SwingSignal::GraphSwing,2.5));
    AttackOptions opt;Check(opt.chance==.1f);opt.Validate();
    AttackGate gate;
    Check(gate.Consider(1,.099f,opt,false,false));
    Check(!gate.Consider(1.01,.01f,opt,false,false));
    Check(!gate.Consider(1.2,.10f,opt,false,false));
    Check(!gate.Consider(1.4,.01f,opt,true,false));
    opt.combatOnly=true;Check(!gate.Consider(1.6,.01f,opt,false,false));
    Check(gate.Consider(1.8,.01f,opt,false,true));
    opt.cooldown=2;Check(!gate.Consider(2,.01f,opt,false,true));
    Check(gate.Consider(3.9,.01f,opt,false,true));
    opt.enabled=false;Check(!gate.Consider(6,.01f,opt,false,true));
    opt={};opt.chance=0;Check(!gate.Consider(7,0,opt,false,true));
    opt.chance=1;Check(gate.Consider(8,.999999f,opt,false,true));
    opt.chance=std::numeric_limits<float>::quiet_NaN();opt.Validate();Check(opt.chance==.1f);
    opt.chance=.1f;gate={};unsigned successes=0;
    for(unsigned i=0;i<10000;++i) successes+=gate.Consider(double(i),float(i)/10000,opt,false,false);
    Check(successes==1000);


    slowdown_helper<int> helper;
    Check(helper.start(.08f,1.5f,Source::Attack));Check(!helper.state().active);
    helper.update();Check(helper.state().active && RE::vats.playerMagicTimeSlowdown==1);
    const auto serial=helper.state().serial;
    Check(!helper.start(.08f,1.5f,Source::Attack));
    Check(helper.start(.6f,1.3f,Source::TimedBlock));
    helper.update();Check(helper.state().serial==serial+1 && helper.state().source==Source::TimedBlock);
    Check(helper.start(.2f,3.9f,Source::PerfectBlock));helper.update();
    Check(helper.state().source==Source::PerfectBlock && helper.state().active);
    Check(!helper.start(.6f,2,Source::TimedBlock));
    auto before=helper.state();paused=true;helper.update();
    Check(helper.state().elapsed==before.elapsed);paused=false;
    for(int i=0;i<220;++i) helper.update();
    Check(!helper.state().active && globalRate==1 && RE::vats.playerMagicTimeSlowdown==0);

    Check(helper.start(.1f,1,Source::Attack));helper.update();
    RE::vats.magicTimeSlowdown=.4f;RE::vats.playerMagicTimeSlowdown=.7f;
    helper.update();Check(!helper.state().active);helper.stop();
    Check(RE::vats.magicTimeSlowdown==.4f && RE::vats.playerMagicTimeSlowdown==.7f);
    std::cout<<"10% boundary, one-roll duplicate gate, optional cooldown/combat, active-session exclusion, block priority, pause and foreign ownership passed.\n";
}
