#include "../source/RedsandAppearancePolicy.h"
#include "../source/RedsandUpdateScope.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace std::literals;



namespace RE {
    struct Object { virtual ~Object() = default; };
    inline std::vector<std::unique_ptr<Object>> pool;
    template <class T, class... A> T* Make(A&&... args) {
        auto value = std::make_unique<T>(std::forward<A>(args)...);
        auto* result = value.get(); pool.push_back(std::move(value)); return result;
    }
    using BSFixedString = std::string;
    template <class T> struct NiPointer {
        T* value = nullptr;
        NiPointer(T* p) : value(p) {}
        T* get() const { return value; }
        T* operator->() const { return value; }
        explicit operator bool() const { return value != nullptr; }
    };
    struct NiFloatExtraData : Object {
        BSFixedString name; float value;
        NiFloatExtraData(BSFixedString n, float v) : name(std::move(n)), value(v) {}
        static NiFloatExtraData* Create(const BSFixedString& n, float v) { return Make<NiFloatExtraData>(n, v); }
    };
    struct NiFloatInterpolator : Object {
        std::shared_ptr<int> floatData;
        float floatValue = 0;
        inline static unsigned clones = 0;
        Object* Clone() { ++clones; return Make<NiFloatInterpolator>(*this); }
    };
    struct BSEffectShaderProperty : Object { std::string name = "RSRGBv1_3"; };
    struct RTTI { const char* name; const char* GetName() const { return name; } };
    struct NiInterpController : Object {
        Object* target = nullptr;
        NiInterpController* next = nullptr;
        NiFloatInterpolator* interpolator = nullptr;
        RTTI type{ "unrelated-controller" };
        NiInterpController* GetNext() const { return next; }
        const RTTI* GetRTTI() const { return &type; }
        Object* GetInterpolator(int) const { return interpolator; }
        void SetInterpolator(NiFloatInterpolator* value, int) { interpolator = value; }
    };
    struct BSGeometry : Object {
        struct Runtime { NiPointer<BSEffectShaderProperty> shaderProperty{ Make<BSEffectShaderProperty>() }; } runtime;
        std::vector<NiFloatExtraData*> extras;
        std::string name = "owned test sparks";
        NiInterpController* head = nullptr;
        bool rejectExtra = false;
        unsigned controllerScans = 0;
        Runtime& GetGeometryRuntimeData() { return runtime; }
        Object* GetExtraData(const BSFixedString& n) {
            for (auto* extra : extras) { if (extra->name == n) { return extra; } }
            return nullptr;
        }
        bool AddExtraData(NiFloatExtraData* extra) {
            if (rejectExtra) { return false; } extras.push_back(extra); return true;
        }
        NiInterpController* GetControllers() { ++controllerScans; return head; }
    };
}
template <class T, class O> T netimmerse_cast(O* object) { return dynamic_cast<T>(object); }
namespace logger { template <class... T> void info(const char*, T&&...) {} }
namespace RedsandAppearance {
#include "../source/SlashEmitterControls.inc"
}
void Check(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }

struct Fixture {
    RE::BSGeometry geometry;
    RE::NiInterpController speed, declination, planar, unrelated;
    RE::NiFloatInterpolator originalSpeed, originalDeclination, originalPlanar;
    Fixture() {
        originalSpeed.floatValue = 680.f; originalDeclination.floatValue = .035f; originalPlanar.floatValue = .045f;
        speed = {}; declination = {}; planar = {}; unrelated = {};
        speed.target = declination.target = planar.target = &geometry;
        speed.type.name = "NiPSysEmitterSpeedCtlr"; declination.type.name = "NiPSysEmitterDeclinationVarCtlr";
        planar.type.name = "NiPSysEmitterPlanarAngleVarCtlr";
        speed.interpolator = &originalSpeed; declination.interpolator = &originalDeclination; planar.interpolator = &originalPlanar;
        geometry.head = &unrelated; unrelated.next = &speed; speed.next = &declination; declination.next = &planar;
    }
    void Verify(const RedsandSettings::Config& config) {
        Check(speed.interpolator != &originalSpeed && declination.interpolator != &originalDeclination &&
            planar.interpolator != &originalPlanar, "posed controls are not private");
        Check(speed.interpolator->floatValue == RedsandAppearance::SlashSpeed(680.f, config) &&
            declination.interpolator->floatValue == RedsandAppearance::SlashSpread(.035f, config) &&
            planar.interpolator->floatValue == RedsandAppearance::SlashSpread(.045f, config), "controller output differs from original equations");
        Check(originalSpeed.floatValue == 680.f && originalDeclination.floatValue == .035f && originalPlanar.floatValue == .045f,
            "shared source was modified");
    }
};
int main()
{
    try {
        RedsandSettings::Config config; Fixture fixture;
        RedsandAppearance::UpdateSlashSpeed(&fixture.geometry, config); fixture.Verify(config);
        const auto allocations = RE::pool.size();
        for (unsigned frame = 0; frame < 10000; ++frame) { RedsandAppearance::UpdateSlashSpeed(&fixture.geometry, config); fixture.Verify(config); }
        Check(fixture.geometry.controllerScans == 1 && RE::pool.size() == allocations && RE::NiFloatInterpolator::clones == 3,
            "unchanged playback re-traverses controls or allocates");
        for (const auto distance : {.1f, .327f, 1.f, 20.f}) {
            for (const auto spread : {0.f, .137f, 1.f, 5.f}) {
                config.slashDistance = distance; config.slashSpread = spread;
                RedsandAppearance::UpdateSlashSpeed(&fixture.geometry, config); fixture.Verify(config);
            }
        }
        Check(RE::NiFloatInterpolator::clones == 3, "live settings re-clone private controls");
        Fixture missing; missing.declination.next = nullptr;
        RedsandAppearance::UpdateSlashSpeed(&missing.geometry, config);
        missing.declination.next = &missing.planar;
        RedsandAppearance::UpdateSlashSpeed(&missing.geometry, config); missing.Verify(config);
        Fixture failed; failed.geometry.rejectExtra = true;
        RedsandAppearance::UpdateSlashSpeed(&failed.geometry, config);
        failed.geometry.rejectExtra = false;
        RedsandAppearance::UpdateSlashSpeed(&failed.geometry, config); failed.Verify(config);
        Fixture animated; animated.originalSpeed.floatData = std::make_shared<int>(1);
        RedsandAppearance::UpdateSlashSpeed(&animated.geometry, config);
        Check(animated.speed.interpolator == &animated.originalSpeed && animated.originalSpeed.floatValue == 680.f &&
            !animated.geometry.GetExtraData("RSSlashDistanceApplied_v1"), "unsupported animated control cached or changed");
        Fixture other; other.geometry.runtime.shaderProperty->name = "other-mod-shader";
        RedsandAppearance::UpdateSlashSpeed(&other.geometry, config);
        Check(other.geometry.controllerScans == 0 && other.geometry.extras.empty(), "unowned geometry modified");
        Fixture independent; RedsandSettings::Config independentConfig; independentConfig.slashDistance = .4f;
        RedsandAppearance::UpdateSlashSpeed(&independent.geometry, independentConfig); independent.Verify(independentConfig); fixture.Verify(config);

        int a, b; unsigned original = 0, prepared = 0, applied = 0;
        const auto update = [&](auto&& self, const void* root, unsigned depth) -> void {
            RedsandAppearance::UpdateScope scope(root);
            if (!scope.Nested()) { ++prepared; }
            ++original;
            if (depth) { self(self, root == &a && depth == 2 ? &b : root, depth - 1); }
            if (!scope.Nested()) { ++applied; }
        };
        update(update, &a, 3);
        Check(original == 4 && prepared == 2 && applied == 2, "nested root guard skipped original or an independent scene");
        try { RedsandAppearance::UpdateScope throwing(&a); throw 1; } catch (...) {}
        RedsandAppearance::UpdateScope outer(&a);
        Check(!outer.Nested(), "exception left stale stack scope");
        bool isolated = false;
        std::thread worker([&] { RedsandAppearance::UpdateScope scope(&a); isolated = !scope.Nested(); }); worker.join();
        Check(isolated, "scope leaked across engine threads");
        std::cout << "Production emitter controls: 10000 stable frames, exact slider outputs, independent scenes, retry paths, no repeated clones; nested update originals and thread isolation passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
