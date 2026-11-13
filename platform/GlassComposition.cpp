// C ABI bridge for Qt/MinGW. No Qt objects or CRT allocations cross this boundary.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <windows.graphics.effects.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace winrt;
using namespace Windows::UI::Composition;
namespace effects=Windows::Graphics::Effects;
namespace abiEffects=ABI::Windows::Graphics::Effects;
namespace {
struct Runtime {
    Windows::System::DispatcherQueueController queue{nullptr};
    com_ptr<ID2D1Factory1> factory;
    Runtime(){
        const HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(FAILED(apartment)&&apartment!=RPC_E_CHANGED_MODE)check_hresult(apartment);
        if(!Windows::System::DispatcherQueue::GetForCurrentThread()){
            DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
            check_hresult(CreateDispatcherQueueController(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(put_abi(queue))));
        }
        check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.put()));
    }
};
Runtime& runtime(){static Runtime* instance=new Runtime;return *instance;}

// Backdrop brushes are supported as effect inputs, not as MaskBrush.Source.
struct FadeEffect : implements<FadeEffect,effects::IGraphicsEffect,effects::IGraphicsEffectSource,abiEffects::IGraphicsEffectD2D1Interop> {
    effects::IGraphicsEffectSource sources[2]{CompositionEffectSourceParameter(L"Mask"),CompositionEffectSourceParameter(L"Backdrop")};
    hstring Name() const{return L"GlassFade";} void Name(hstring const&){}
    HRESULT __stdcall GetEffectId(GUID* id) noexcept override{*id=CLSID_D2D1Composite;return S_OK;}
    HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR,UINT*,abiEffects::GRAPHICS_EFFECT_PROPERTY_MAPPING*) noexcept override{return E_INVALIDARG;}
    HRESULT __stdcall GetPropertyCount(UINT* count) noexcept override{*count=1;return S_OK;}
    HRESULT __stdcall GetProperty(UINT index,ABI::Windows::Foundation::IPropertyValue** value) noexcept override{
        *value=nullptr;if(index)return E_INVALIDARG;
        try{Windows::Foundation::PropertyValue::CreateUInt32(D2D1_COMPOSITE_MODE_SOURCE_IN).as<ABI::Windows::Foundation::IPropertyValue>().copy_to(value);return S_OK;}catch(...){return to_hresult();}
    }
    HRESULT __stdcall GetSource(UINT index,abiEffects::IGraphicsEffectSource** source) noexcept override{
        *source=nullptr;if(index>=2)return E_INVALIDARG;
        try{sources[index].as<abiEffects::IGraphicsEffectSource>().copy_to(source);return S_OK;}catch(...){return to_hresult();}
    }
    HRESULT __stdcall GetSourceCount(UINT* count) noexcept override{*count=2;return S_OK;}
};
struct Surface {
    Compositor compositor;
    Windows::UI::Composition::Desktop::DesktopWindowTarget target{nullptr};
    SpriteVisual visual{nullptr};
    CompositionGraphicsDevice graphics{nullptr};
    CompositionDrawingSurface mask{nullptr};
    int maskWidth=0,maskHeight=0;
    CompositionEffectBrush brush{nullptr};
    com_ptr<ID3D11Device> d3d;
    com_ptr<ID2D1Device> d2d;
};

// Exact squared Euclidean distance transform. Linear work avoids repeated
// boolean geometry operations on thousands of DPI-scaled scan-line edges.
void distanceLine(std::vector<float>& grid,int start,int stride,int length,std::vector<int>& sites,std::vector<double>& limits,std::vector<float>& output){
    constexpr float infinity=1e15f;int k=-1;
    for(int q=0;q<length;++q){
        if(grid[start+q*stride]>=infinity)continue;
        double intersection=-infinity;
        while(k>=0){const int v=sites[k];intersection=(double(grid[start+q*stride])+double(q)*q-grid[start+v*stride]-double(v)*v)/(2.0*(q-v));if(intersection>limits[k])break;--k;}
        ++k;sites[k]=q;limits[k]=k?intersection:-infinity;limits[k+1]=infinity;
    }
    if(k<0)return;
    int envelope=0;
    for(int q=0;q<length;++q){while(envelope<k&&limits[envelope+1]<q)++envelope;const int d=q-sites[envelope];output[q]=float(d*d)+grid[start+sites[envelope]*stride];}
    for(int q=0;q<length;++q)grid[start+q*stride]=output[q];
}
std::vector<uint32_t> opacityPixels(const RECT* rectangles,unsigned count,int width,int height,float diffusion){
    std::vector<float> distances(size_t(width)*height,1e15f);
    for(unsigned i=0;i<count;++i){const auto& r=rectangles[i];const int left=std::clamp(int(r.left),0,width),right=std::clamp(int(r.right),0,width);
        if(right<=left)continue;
        for(int y=std::max(0,int(r.top));y<std::min(height,int(r.bottom));++y)std::fill(distances.begin()+y*width+left,distances.begin()+y*width+right,0.0f);
    }
    if(diffusion>0){
        const int longest=std::max(width,height);std::vector<int> sites(longest);std::vector<double> limits(longest+1);std::vector<float> output(longest);
        for(int y=0;y<height;++y)distanceLine(distances,y*width,1,width,sites,limits,output);
        for(int x=0;x<width;++x)distanceLine(distances,x,width,height,sites,limits,output);
    }
    std::vector<uint32_t> pixels(distances.size());
    for(size_t i=0;i<pixels.size();++i){const float t=diffusion>0?std::min(1.0f,std::sqrt(distances[i])/diffusion):(distances[i]==0?0.0f:1.0f);const auto alpha=uint32_t(std::lround(255*(1-t*t*(3-2*t))));pixels[i]=alpha*0x01010101u;}
    return pixels;
}
void update(Surface& surface,const RECT* rectangles,unsigned count,int width,int height,float diffusion){
    if(!rectangles||!count||count>100000||width<=0||height<=0||width>8192||height>8192||uint64_t(width)*height>16000000||!std::isfinite(diffusion)||diffusion<0||diffusion>800)throw hresult_invalid_argument();
    // Sample only the soft opacity contour; live blur and Qt artwork stay at
    // native resolution. Bound this work during wide particle transitions.
    const float sampleScale=std::min(1.0f,512.0f/std::max(width,height));
    const int maskWidth=int(std::ceil(width*sampleScale)),maskHeight=int(std::ceil(height*sampleScale));
    std::vector<RECT> sampled;sampled.reserve(count);
    for(unsigned i=0;i<count;++i){const auto& r=rectangles[i];sampled.push_back({LONG(std::floor(r.left*sampleScale)),LONG(std::floor(r.top*sampleScale)),LONG(std::ceil(r.right*sampleScale)),LONG(std::ceil(r.bottom*sampleScale))});}
    const auto pixels=opacityPixels(sampled.data(),count,maskWidth,maskHeight,diffusion*sampleScale);
    const bool resized=!surface.mask||surface.maskWidth!=maskWidth||surface.maskHeight!=maskHeight;
    auto mask=resized?surface.graphics.CreateDrawingSurface({float(maskWidth),float(maskHeight)},Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied):surface.mask;
    auto interop=mask.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
    com_ptr<ID2D1DeviceContext> context;POINT offset{};
    check_hresult(interop->BeginDraw(nullptr,__uuidof(ID2D1DeviceContext),context.put_void(),&offset));
    try{
        context->SetDpi(96,96);context->SetTransform(D2D1::Matrix3x2F::Translation(float(offset.x),float(offset.y)));
        context->Clear(D2D1::ColorF(0,0,0,0));
        com_ptr<ID2D1Bitmap1> bitmap;
        const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        check_hresult(context->CreateBitmap(D2D1::SizeU(maskWidth,maskHeight),pixels.data(),maskWidth*4,properties,bitmap.put()));
        context->DrawBitmap(bitmap.get(),D2D1::RectF(0,0,float(maskWidth),float(maskHeight)),1,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    }catch(...){interop->EndDraw();throw;}
    check_hresult(interop->EndDraw());
    if(resized){auto maskBrush=surface.compositor.CreateSurfaceBrush(mask);maskBrush.Stretch(CompositionStretch::Fill);surface.brush.SetSourceParameter(L"Mask",maskBrush);}
    surface.maskWidth=maskWidth;surface.maskHeight=maskHeight;
    surface.mask=mask;surface.visual.Size({float(width),float(height)});
}
}
extern "C" __declspec(dllexport) void* __cdecl TPGlass_CreateV2(HWND window,const RECT* rectangles,unsigned count,int width,int height,float diffusion,HRESULT* error) noexcept {
    try {
        const BOOL enabled=TRUE;check_hresult(DwmSetWindowAttribute(window,17,&enabled,sizeof(enabled)));
        auto& state=runtime();auto surface=std::make_unique<Surface>();
        HRESULT result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,surface->d3d.put(),nullptr,nullptr);
        if(FAILED(result))check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,surface->d3d.put(),nullptr,nullptr));
        check_hresult(state.factory->CreateDevice(surface->d3d.as<IDXGIDevice>().get(),surface->d2d.put()));
        auto compositorInterop=surface->compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();
        check_hresult(compositorInterop->CreateGraphicsDevice(surface->d2d.get(),reinterpret_cast<ABI::Windows::UI::Composition::ICompositionGraphicsDevice**>(put_abi(surface->graphics))));
        auto desktop=surface->compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        check_hresult(desktop->CreateDesktopWindowTarget(window,FALSE,reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(put_abi(surface->target))));
        surface->visual=surface->compositor.CreateSpriteVisual();
        surface->brush=surface->compositor.CreateEffectFactory(make<FadeEffect>()).CreateBrush();
        surface->brush.SetSourceParameter(L"Backdrop",surface->compositor.CreateHostBackdropBrush());
        surface->visual.Brush(surface->brush);
        update(*surface,rectangles,count,width,height,diffusion);surface->target.Root(surface->visual);
        if(error)*error=S_OK;return surface.release();
    }catch(...){if(error)*error=to_hresult();return nullptr;}
}
extern "C" __declspec(dllexport) HRESULT __cdecl TPGlass_UpdateV2(void* handle,const RECT* rectangles,unsigned count,int width,int height,float diffusion) noexcept {
    try{if(!handle)return E_INVALIDARG;update(*static_cast<Surface*>(handle),rectangles,count,width,height,diffusion);return S_OK;}catch(...){return to_hresult();}
}
extern "C" __declspec(dllexport) void __cdecl TPGlass_Destroy(void* handle) noexcept {
    if(!handle)return;std::unique_ptr<Surface> surface(static_cast<Surface*>(handle));
    try{surface->target.Root(nullptr);surface->target.Close();surface->visual.Close();surface->mask.Close();surface->graphics.Close();surface->compositor.Close();}catch(...){}
}
extern "C" __declspec(dllexport) HRESULT __cdecl TPGlass_SetStrength(void* handle,float strength) noexcept {
    try{if(!handle||!(strength>=0&&strength<=1))return E_INVALIDARG;
        static_cast<Surface*>(handle)->visual.Opacity(strength);return S_OK;
    }catch(...){return to_hresult();}
}
