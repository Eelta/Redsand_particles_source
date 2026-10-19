#include "time_vfx.hpp"
#include "time_vfx_d3d.hpp"
#include "settings.hpp"
#include <chrono>
#include <mutex>
#include <dxgi.h>

namespace HellbladeTime::VFX {
namespace {
using Clock=std::chrono::steady_clock;
std::mutex frameLock;
VisualFrame published;
Clock::time_point publishedAt;
SessionColors sessionColors{static_cast<std::uint64_t>(Clock::now().time_since_epoch().count())};
std::atomic_bool suppressed{false};
Renderer renderer;
Microsoft::WRL::ComPtr<ID3D11Device> attemptedDevice;
bool ready=false;
using Present=HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
Present previousPresent=nullptr;
IDXGISwapChain* installedChain=nullptr;
bool failed=false;

class MenuSink final:public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent*,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
        auto* ui=RE::UI::GetSingleton();
        suppressed.store(!ui || ui->GameIsPaused() || ui->IsMenuOpen("Console") ||
            ui->IsMenuOpen("Loading Menu") || ui->IsMenuOpen("Main Menu"),std::memory_order_relaxed);
        return RE::BSEventNotifyControl::kContinue;
    }
} menuSink;

void Draw(IDXGISwapChain* swapChain) {
    using Microsoft::WRL::ComPtr;
    if(failed) return;
    ComPtr<ID3D11Device> device;
    if(FAILED(swapChain->GetDevice(IID_PPV_ARGS(device.GetAddressOf())))) return;
    if(attemptedDevice.Get()!=device.Get()) {
        attemptedDevice=device;
        std::string error;
        ready=renderer.Initialize(device.Get(),error);
        if(ready) logger::info("Procedural screen lightning renderer ready; independent D3D11.1 context state");
        else logger::error("Screen lightning disabled: {}. Time slowdown remains available.",error);
    }
    if(!ready || suppressed.load(std::memory_order_relaxed)) return;
    VisualFrame frame;
    {
        std::lock_guard lock(frameLock);


        if(Clock::now()-publishedAt>std::chrono::milliseconds(250)) return;
        frame=published;
    }
    if(!frame.session.active || !frame.options.enabled || frame.options.brightness<=0) return;
    ComPtr<ID3D11Texture2D> buffer;
    if(FAILED(swapChain->GetBuffer(0,IID_PPV_ARGS(buffer.GetAddressOf())))) return;
    D3D11_TEXTURE2D_DESC desc;buffer->GetDesc(&desc);
    ComPtr<ID3D11RenderTargetView> target;
    if(FAILED(device->CreateRenderTargetView(buffer.Get(),nullptr,target.GetAddressOf()))) return;
    if(!renderer.Draw(target.Get(),desc.Width,desc.Height,frame)) {
        failed=true;
        logger::error("Screen lightning rendering failed and was disabled for this session; time handling is unchanged");
    }
}
HRESULT STDMETHODCALLTYPE OnPresent(IDXGISwapChain* chain,UINT interval,UINT flags) noexcept {
    if(chain==installedChain && !(flags&DXGI_PRESENT_TEST)) {
        try { Draw(chain); }
        catch(const std::exception& e) {failed=true;logger::error("Lightning renderer disabled: {}",e.what());}
        catch(...) {failed=true;logger::error("Lightning renderer disabled after an unexpected error");}
    }
    return previousPresent(chain,interval,flags);
}
}
void Install() {
    if (!Settings::visual.enabled) return;
    if(previousPresent) return;
    auto* window=RE::BSGraphics::Renderer::GetCurrentRenderWindow();
    if(!window || !window->swapChain) {
        logger::warn("Screen lightning: no render window at initialization; time effects remain enabled");
        return;
    }
    installedChain=reinterpret_cast<IDXGISwapChain*>(window->swapChain);
    auto* table=*reinterpret_cast<std::uintptr_t**>(installedChain);
    previousPresent=reinterpret_cast<Present>(table[8]);
    const auto replacement=reinterpret_cast<std::uintptr_t>(&OnPresent);
    const auto expected=reinterpret_cast<std::uintptr_t>(previousPresent);
    if(!REL::safe_write(reinterpret_cast<std::uintptr_t>(table+8),&replacement,sizeof(replacement),&expected,sizeof(expected))) {
        previousPresent=nullptr;installedChain=nullptr;
        logger::error("Screen lightning hook changed during initialization; preserving the existing renderer");
        return;
    }
    if(auto* ui=RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuSink);
    logger::info("Screen lightning Present chain installed; no window/input hooks or game data assets");
}
void Publish(const Session& session) {
    if(auto* ui=RE::UI::GetSingleton())
        suppressed.store(ui->GameIsPaused() || ui->IsMenuOpen("Console") ||
            ui->IsMenuOpen("Loading Menu") || ui->IsMenuOpen("Main Menu"),std::memory_order_relaxed);
    VisualFrame next{session,Settings::visual};
    if((session.source==Source::Attack && !next.options.onAttack) ||
        (session.source!=Source::Attack && !next.options.onBlock)) next.options.enabled=false;
    std::lock_guard lock(frameLock);
    sessionColors.Apply(session,next.options);
    published=next;publishedAt=Clock::now();
}
void Reset() {
    std::lock_guard lock(frameLock);
    published={};publishedAt={};
    sessionColors.ResetSession();
}
}
