// Small C ABI bridge so the Qt/MinGW app can use the Windows SDK's C++/WinRT
// projection. No Qt objects or CRT allocations cross the DLL boundary.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <windows.graphics.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <memory>

using namespace winrt;
using namespace Windows::UI::Composition;

namespace {
struct GeometrySource : implements<GeometrySource, Windows::Graphics::IGeometrySource2D, ABI::Windows::Graphics::IGeometrySource2DInterop> {
    com_ptr<ID2D1Geometry> geometry;
    explicit GeometrySource(com_ptr<ID2D1Geometry> value):geometry(std::move(value)){}
    HRESULT __stdcall GetGeometry(ID2D1Geometry** value) noexcept override { geometry.copy_to(value);return S_OK; }
    HRESULT __stdcall TryGetGeometryUsingFactory(ID2D1Factory*,ID2D1Geometry** value) noexcept override { *value=nullptr;return S_OK; }
};
struct Runtime {
    Windows::System::DispatcherQueueController queue{nullptr};
    Compositor compositor{nullptr};
    com_ptr<ID2D1Factory> factory;
    Runtime(){
        const HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(FAILED(apartment)&&apartment!=RPC_E_CHANGED_MODE)check_hresult(apartment);
        if(!Windows::System::DispatcherQueue::GetForCurrentThread()){
            DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
            check_hresult(CreateDispatcherQueueController(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(put_abi(queue))));
        }
        compositor=Compositor();
        check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.put()));
    }
};
Runtime& runtime(){
    // GUI-thread dispatcher/compositor live until process exit. Do not tear down
    // the dispatcher while Windows still has queued compositor callbacks.
    static Runtime* instance=new Runtime;
    return *instance;
}
struct Surface {
    Windows::UI::Composition::Desktop::DesktopWindowTarget target{nullptr};
    SpriteVisual visual{nullptr};
};
void update(Surface& surface,const RECT* rectangles,unsigned count,int width,int height){
    if(!rectangles||!count||width<=0||height<=0)throw hresult_invalid_argument();
    auto& state=runtime();
    com_ptr<ID2D1PathGeometry> geometry;check_hresult(state.factory->CreatePathGeometry(geometry.put()));
    com_ptr<ID2D1GeometrySink> sink;check_hresult(geometry->Open(sink.put()));
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    for(unsigned i=0;i<count;++i){const auto& r=rectangles[i];
        sink->BeginFigure({float(r.left),float(r.top)},D2D1_FIGURE_BEGIN_FILLED);
        const D2D1_POINT_2F corners[]={{float(r.right),float(r.top)},{float(r.right),float(r.bottom)},{float(r.left),float(r.bottom)}};
        sink->AddLines(corners,3);sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    check_hresult(sink->Close());
    auto source=make<GeometrySource>(geometry.as<ID2D1Geometry>());
    auto path=state.compositor.CreatePathGeometry(CompositionPath(source));
    surface.visual.Size({float(width),float(height)});
    surface.visual.Clip(state.compositor.CreateGeometricClip(path));
}
}

extern "C" __declspec(dllexport) void* __cdecl TPGlass_Create(HWND window,const RECT* rectangles,unsigned count,int width,int height,HRESULT* error) noexcept {
    try {
        const BOOL enabled=TRUE;
        check_hresult(DwmSetWindowAttribute(window,17,&enabled,sizeof(enabled))); // USE_HOSTBACKDROPBRUSH
        auto& state=runtime();auto surface=std::make_unique<Surface>();
        auto interop=state.compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        check_hresult(interop->CreateDesktopWindowTarget(window,FALSE,reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(put_abi(surface->target))));
        surface->visual=state.compositor.CreateSpriteVisual();
        surface->visual.BorderMode(CompositionBorderMode::Soft);
        surface->visual.Brush(state.compositor.CreateHostBackdropBrush());
        update(*surface,rectangles,count,width,height);
        surface->target.Root(surface->visual);
        if(error)*error=S_OK;return surface.release();
    }catch(...){if(error)*error=to_hresult();return nullptr;}
}
extern "C" __declspec(dllexport) HRESULT __cdecl TPGlass_Update(void* handle,const RECT* rectangles,unsigned count,int width,int height) noexcept {
    try{if(!handle)return E_INVALIDARG;update(*static_cast<Surface*>(handle),rectangles,count,width,height);return S_OK;}catch(...){return to_hresult();}
}
extern "C" __declspec(dllexport) void __cdecl TPGlass_Destroy(void* handle) noexcept {
    if(!handle)return;std::unique_ptr<Surface> surface(static_cast<Surface*>(handle));
    try{surface->target.Root(nullptr);surface->target.Close();surface->visual.Close();}catch(...){}
}
