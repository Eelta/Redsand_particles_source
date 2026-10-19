#include "../source/RedsandMeleeStyle.h"
#include <iostream>
#include <stdexcept>
#include <vector>

struct Color { unsigned char red, green, blue; };
struct Effect { std::array<Color, 7> colors{}; unsigned calls = 0; };
struct Magic {
    std::vector<Effect*> effects;
    Effect* dominant = nullptr;
    Effect* GetCostliestEffectItem() { return dominant; }
};
void Check(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }
int main()
{
    try {
        using namespace Redsand::Weapons;
        Effect gray, green, red;
        gray.colors[0] = {31,31,31}; gray.colors[1] = {255,255,255};
        green.colors[6] = {25,255,80}; red.colors[0] = {255,40,10};
        auto resolve = [](Effect* effect) { ++effect->calls; return FirstEnchantmentHue(effect->colors); };
        Magic magic{{ &gray, &green, &red }, &gray};
        for (unsigned i=0; i<1000; ++i) {
            const auto color = ResolveEnchantmentTint(&magic, resolve);
            Check(color && color->green == 1.f && color->red == 25.f/255.f,
                "neutral costliest effect hid green in the three-effect enchantment");
        }
        Check(gray.calls == 1000 && green.calls == 1000 && red.calls == 0,
            "duplicate dominant evaluation or unstable effect precedence");
        magic.dominant = &red;
        Check(ResolveEnchantmentTint(&magic, resolve)->red == 1.f, "dominant chromatic effect lost priority");
        magic = {{nullptr, &gray}, nullptr};
        Check(!ResolveEnchantmentTint(&magic, resolve), "neutral masks invented a white enchantment tint");
        Check(!ResolveEnchantmentTint<Magic>(nullptr, resolve), "null enchantment produced a tint");
        Check(!HasEnchantmentHue({1,.99f,1}) && !HasEnchantmentHue({0,0,0}) &&
            HasEnchantmentHue({.1f,.8f,.2f}), "shader hue filter failed");


        Check(!UseEnchantmentFallback(false,false,true) && !UseEnchantmentFallback(false,true,false),
            "explicit white or companion colors entered the hue filter");
        Check(InsufficientEnchantmentCharge(false,true,true,100.,true,41.f,163.f), "depleted actor charge was ignored");
        Check(defaultTrailTint.blue == .650f && !EnchantmentHasCharge(false,true,0.), "depleted original blue changed");
        std::cout << "Stable three-effect selection, neutral/black masks, late shader keys, dominant priority and explicit color boundaries passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
