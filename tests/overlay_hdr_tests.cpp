// Numeric GPU readback tests. No HDR display or visible window is required.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "detail/overlay_hdr.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <d3d12sdklayers.h>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"HDR:%d %s\n",__LINE__,#x);throw std::runtime_error(#x);}}while(0)
using Color=std::array<double,4>;
double srgb(double c){return c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);}
double pq(double nits){double p=std::pow(nits/10000,2610.0/16384);return std::pow((3424.0/4096+2413.0/128*p)/(1+2392.0/128*p),2523.0/32);}
double unpq(double c){double p=std::pow(c,32.0/2523);return 10000*std::pow(std::max(p-3424.0/4096,0.0)/(2413.0/128-2392.0/128*p),16384.0/2610);}
double half(uint16_t v){int e=(v>>10)&31;double m=v&1023;return (v&32768?-1:1)*(e?std::ldexp(1+m/1024,e-15):std::ldexp(m,-24));}
Color decode(const unsigned char* data,bool hdr10){
    Color c{};if(hdr10){uint32_t v;std::memcpy(&v,data,4);for(int n=0;n<3;++n)c[n]=double((v>>(n*10))&1023)/1023;c[3]=double(v>>30)/3;}
    else{uint16_t v[4];std::memcpy(v,data,8);for(int n=0;n<4;++n)c[n]=half(v[n]);}return c;
}
struct Gpu {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;ComPtr<ID3D12DescriptorHeap> rtv;
    ComPtr<ID3D12InfoQueue> messages;HANDLE event{};uint64_t serial{};
    Gpu(){ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));ComPtr<IDXGIAdapter> warp;
        CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));CHECK(SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
        device.As(&messages);D3D12_COMMAND_QUEUE_DESC q{};CHECK(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
        CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
        CHECK(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));CHECK(SUCCEEDED(list->Close()));
        CHECK(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);
        D3D12_DESCRIPTOR_HEAP_DESC h{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};CHECK(SUCCEEDED(device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&rtv))));
    }
    void begin(){CHECK(SUCCEEDED(allocator->Reset()));CHECK(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));}
    void submit(){CHECK(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
        CHECK(SUCCEEDED(queue->Signal(fence.Get(),++serial)));CHECK(SUCCEEDED(fence->SetEventOnCompletion(serial,event)));CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);}
    void validate(){if(!messages)return;for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T size{};messages->GetMessage(i,nullptr,&size);
        std::vector<unsigned char> bytes(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());CHECK(SUCCEEDED(messages->GetMessage(i,m,&size)));
        // Arbitrary fixture clears intentionally differ from optimized clear
        // colors. Ignore only that performance warning, not state/format errors.
        if(m->ID==D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE)continue;
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_WARNING){std::fprintf(stderr,"DX12: %s\n",m->pDescription);CHECK(false);}}
    }
    ~Gpu(){if(event)CloseHandle(event);}
};
void test(bool hdr10,float white){
    Gpu gpu;gyrolib_hdr::Composer compositor;
    const auto format=hdr10?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT;
    CHECK(SUCCEEDED(compositor.init(gpu.device.Get(),format,hdr10?12:1)));compositor.white_nits=white;
    // Reallocate as on a swapchain resize; repeated frames reuse the same targets.
    for(unsigned width:{8u,16u}){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=1;desc.DepthOrArraySize=1;
        desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> buffer;
        CHECK(SUCCEEDED(gpu.device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PRESENT,nullptr,IID_PPV_ARGS(&buffer))));
        gpu.device->CreateRenderTargetView(buffer.Get(),nullptr,gpu.rtv->GetCPUDescriptorHandleForHeapStart());
        CHECK(SUCCEEDED(compositor.resize(gpu.device.Get(),desc)));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total{};gpu.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&total);
        D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        props.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> before,after;
        CHECK(SUCCEEDED(gpu.device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&before))));
        CHECK(SUCCEEDED(gpu.device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&after))));
        const auto copy=[&](ID3D12Resource* dest){gyrolib_hdr::Composer::transition(gpu.list.Get(),buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=buffer.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=dest;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
            gpu.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);gyrolib_hdr::Composer::transition(gpu.list.Get(),buffer.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);};
        const Color colors[]={{1,1,1,1},{.5,0,0,.5},{0,0,0,0},{.5,.5,.5,1},{0,0,0,.5},{0,.75,0,.75},{0,0,0,0},{.02,.01,.005,1}};
        for(int repeat=0;repeat<2;++repeat){
            gpu.begin();gyrolib_hdr::Composer::transition(gpu.list.Get(),buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
            float background[]={hdr10?float(pq(1000)):-.25f,hdr10?float(pq(600)):7.5f,hdr10?float(pq(50)):.625f,2.f/3};
            gpu.list->ClearRenderTargetView(gpu.rtv->GetCPUDescriptorHandleForHeapStart(),background,0,nullptr);
            gyrolib_hdr::Composer::transition(gpu.list.Get(),buffer.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);copy(before.Get());
            compositor.begin(gpu.list.Get(),buffer.Get());
            for(unsigned x=0;x<width;++x){const auto& c=colors[x%8];float value[]={float(c[0]),float(c[1]),float(c[2]),float(c[3])};D3D12_RECT rect{LONG(x),0,LONG(x+1),1};
                gpu.list->ClearRenderTargetView(compositor.rtv->GetCPUDescriptorHandleForHeapStart(),value,1,&rect);}
            compositor.finish(gpu.list.Get(),buffer.Get(),gpu.rtv->GetCPUDescriptorHandleForHeapStart());copy(after.Get());gpu.submit();
            void *a{},*b{};D3D12_RANGE range{0,size_t(total)},empty{};CHECK(SUCCEEDED(before->Map(0,&range,&a)));CHECK(SUCCEEDED(after->Map(0,&range,&b)));
            for(unsigned x=0;x<width;++x){const auto* bp=static_cast<unsigned char*>(a)+x*(hdr10?4:8);const auto* op=static_cast<unsigned char*>(b)+x*(hdr10?4:8);
                auto bg=decode(bp,hdr10),out=decode(op,hdr10),ui=colors[x%8];for(auto& v:ui)v=std::round(v*255)/255;
                CHECK(out[3]==bg[3]);if(ui[3]==0){CHECK(std::memcmp(bp,op,hdr10?4:8)==0);continue;}
                Color linear{};for(int n=0;n<3;++n)linear[n]=srgb(ui[n]/ui[3])*white;
                if(hdr10){auto c=linear;linear[0]=.627404*c[0]+.329283*c[1]+.043313*c[2];linear[1]=.069097*c[0]+.919540*c[1]+.011362*c[2];linear[2]=.016391*c[0]+.088013*c[1]+.895595*c[2];}
                for(int n=0;n<3;++n){double expected=hdr10?pq(linear[n]*ui[3]+unpq(bg[n])*(1-ui[3])):linear[n]/80*ui[3]+bg[n]*(1-ui[3]);
                    if(std::abs(out[n]-expected)>(hdr10?2.0/1023:.008)){std::fprintf(stderr,"mode %d pixel %u component %d got %.7f expected %.7f\n",hdr10,x,n,out[n],expected);CHECK(false);}}
            }before->Unmap(0,&empty);after->Unmap(0,&empty);
        }
    }gpu.validate();
}
int main()try{SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    CHECK(gyrolib_hdr::automatic_space(DXGI_FORMAT_R10G10B10A2_UNORM,12)==12);
    CHECK(gyrolib_hdr::automatic_space(DXGI_FORMAT_R10G10B10A2_UNORM,0)==0);
    CHECK(gyrolib_hdr::automatic_space(DXGI_FORMAT_R16G16B16A16_FLOAT,0)==1);
    CHECK(gyrolib_hdr::automatic_space(DXGI_FORMAT_R8G8B8A8_UNORM,12)==0);
    for(bool hdr10:{false,true})for(float white:{80.f,203.f,1000.f})test(hdr10,white);
    std::puts("HDR WARP readback: scRGB, HDR10, white levels, linear alpha, gamut, untouched pixels, resize and repeated frames passed.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
