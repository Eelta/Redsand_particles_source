#pragma once

namespace RedsandAppearance
{



    class UpdateScope
    {
    public:
        explicit UpdateScope(const void* root) : root_(root), previous_(current_)
        {
            for (auto* scope = current_; scope; scope = scope->previous_) {
                if (scope->root_ == root) { nested_ = true; break; }
            }
            current_ = this;
        }
        ~UpdateScope() { current_ = previous_; }
        UpdateScope(const UpdateScope&) = delete;
        UpdateScope& operator=(const UpdateScope&) = delete;
        bool Nested() const { return nested_; }
    private:
        inline static thread_local UpdateScope* current_ = nullptr;
        const void* root_;
        UpdateScope* previous_;
        bool nested_ = false;
    };
}
