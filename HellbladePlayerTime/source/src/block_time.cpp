#ifdef REDSAND_TIME_TEST
#include "time_runtime_stubs.hpp"
#else
#include "util.hpp"
#include <RE/V/VATS.h>
#include <RE/U/UI.h>
#include <Windows.h>
#endif
namespace {
bool owns=false;
float savedWorld=0,savedPlayer=0,lastWorld=0,lastPlayer=0;
bool Same(float a,float b) {return std::abs(a-b)<.0001f;}
bool StillOwns(RE::VATS* vats) {
    return owns && vats && Same(vats->magicTimeSlowdown,lastWorld) && Same(vats->playerMagicTimeSlowdown,lastPlayer);
}
}
bool Util::BlockSlowdownSuspended() {
    auto* ui=RE::UI::GetSingleton();
    return !ui || ui->GameIsPaused();
}
bool Util::BeginBlockSlowdown() {
    if(BlockSlowdownSuspended()) return false;
    auto* vats=RE::VATS::GetSingleton();
    auto reject=[&](const char* reason) {
#ifndef REDSAND_TIME_TEST
        static unsigned logged=0;
        if(logged++<8) logger::warn("Player-time request deferred: reason={}, mode={}, world={}, player={}, global={}",reason,
            vats ? static_cast<unsigned>(vats->mode) : 0xffffffffu,vats ? vats->magicTimeSlowdown : -999.f,
            vats ? vats->playerMagicTimeSlowdown : -999.f,GetGlobalTimeMult());
#endif
        return false;
    };
    if(!vats) return reject("no-vats");
    if(vats->mode!=RE::VATS::VATS_MODE::kNone || owns) return reject("killcam-or-owned");
    const float world=vats->magicTimeSlowdown,player=vats->playerMagicTimeSlowdown;



    if(!std::isfinite(world) || !std::isfinite(player) ||
        !(world<=0 || (Same(world,1.f) && Same(player,1.f)))) return reject("external-vats-time");
    const float global=GetGlobalTimeMult();


    if(!Same(global,1.f) && !(GetModuleHandleW(L"ValhallaCombat.dll") && Same(global,.1f))) return reject("external-global-time");
    savedWorld=vats->magicTimeSlowdown;savedPlayer=vats->playerMagicTimeSlowdown;
    lastWorld=savedWorld;lastPlayer=savedPlayer;owns=true;
    return true;
}
bool Util::ApplyBlockSlowdown(float world) {
    if(BlockSlowdownSuspended()) return owns;
    auto* vats=RE::VATS::GetSingleton();
    if(!StillOwns(vats) || vats->mode!=RE::VATS::VATS_MODE::kNone) {owns=false;return false;}
    lastWorld=std::clamp(world,.05f,1.f);
    lastPlayer=1.f;


    vats->SetMagicTimeSlowdown(lastWorld,lastPlayer);


    return true;
}
void Util::EndBlockSlowdown() {
    auto* vats=RE::VATS::GetSingleton();
    if(StillOwns(vats) && vats->mode==RE::VATS::VATS_MODE::kNone) vats->SetMagicTimeSlowdown(savedWorld,savedPlayer);
    owns=false;
}
