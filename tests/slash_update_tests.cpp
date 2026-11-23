#include "../source/RedsandUpdateScope.h"
#include <iostream>
#include <stdexcept>
#include <thread>

void Check(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }

int main()
{
    try {
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
        std::cout << "Nested update originals and thread isolation passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
