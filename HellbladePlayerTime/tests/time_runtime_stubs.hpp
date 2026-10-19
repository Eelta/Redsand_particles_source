#pragma once
#include <algorithm>
#include <cmath>
#include <string_view>
inline bool valhallaLoaded=false;
inline int foreignModuleQueries=0;
inline float globalRate=1;
inline bool paused=false;
inline int timeWrites=0;
inline void* GetModuleHandleW(const wchar_t* name) {
    if (std::wstring_view(name)==L"ValhallaCombat.dll") { return valhallaLoaded ? reinterpret_cast<void*>(1) : nullptr; }
    ++foreignModuleQueries;
    return nullptr;
}
namespace RE {
struct UI {
    static UI* GetSingleton() {static UI instance;return &instance;}
    bool GameIsPaused() {return paused;}
};
struct VATS {
    enum class VATS_MODE {kNone,kKillCam};
    VATS_MODE mode=VATS_MODE::kNone;
    float magicTimeSlowdown=0,playerMagicTimeSlowdown=0;
    static VATS* GetSingleton();
    void SetMagicTimeSlowdown(float world,float player) {
        ++timeWrites;
        magicTimeSlowdown=world;playerMagicTimeSlowdown=player;globalRate=world>0 ? world : 1;
    }
};
inline VATS vats;
inline bool vatsAvailable=true;
inline VATS* VATS::GetSingleton() {return vatsAvailable ? &vats : nullptr;}
struct BSTimer {
    static BSTimer* GetSingleton() {static BSTimer timer;return &timer;}
    void SetGlobalTimeMultiplier(float value,bool) {globalRate=value;}
};
}
namespace Util {
inline float& GetGlobalTimeMult() {return globalRate;}
bool BlockSlowdownSuspended();bool BeginBlockSlowdown();bool ApplyBlockSlowdown(float world);void EndBlockSlowdown();
}
namespace Offsets {extern float* g_DeltaRealTime;}
