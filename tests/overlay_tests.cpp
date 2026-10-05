// A real C-API consumer: no ImGui, SDL, host frontend or camera engine linkage.
#include <gyrolib/overlay.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <vector>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#ifdef GL_OVERLAY_TEST_HOST_IMGUI
#include <imgui.h>
#endif
using Microsoft::WRL::ComPtr;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"overlay:%d %s (%s)\n",__LINE__,#x,gl_overlay_error());throw std::runtime_error(#x);}}while(0)
struct Worker {
    std::mutex mutex;std::condition_variable condition;std::deque<std::function<void()>> work;bool quit{};
    std::thread thread{[this]{for(;;){std::function<void()> job;{std::unique_lock lock(mutex);condition.wait(lock,[&]{return quit||!work.empty();});
        if(quit&&work.empty())break;job=std::move(work.front());work.pop_front();}job();}}};
    void run(std::function<void()> f){auto task=std::make_shared<std::packaged_task<void()>>(std::move(f));auto done=task->get_future();
        {std::lock_guard lock(mutex);work.emplace_back([task]{(*task)();});}condition.notify_one();done.get();}
    ~Worker(){{std::lock_guard lock(mutex);quit=true;}condition.notify_one();thread.join();}
};
LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
struct Graphics {
    HWND window{};ComPtr<IDXGIFactory4> factory;ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swap;ComPtr<ID3D12DescriptorHeap> heap;ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;HANDLE event{};uint64_t value{};
    void drain(){CHECK(SUCCEEDED(queue->Signal(fence.Get(),++value)));CHECK(SUCCEEDED(fence->SetEventOnCompletion(value,event)));CHECK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);}
    Graphics(DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM){WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"GyroLibOverlayHiddenTest";RegisterClassW(&cls);
        window=CreateWindowW(cls.lpszClassName,L"hidden overlay fixture",WS_POPUP,0,0,1024,720,nullptr,nullptr,cls.hInstance,nullptr);CHECK(window);
        CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));ComPtr<IDXGIAdapter> warp;CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
        CHECK(SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;CHECK(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
        DXGI_SWAP_CHAIN_DESC1 sc{};sc.Width=1024;sc.Height=720;sc.Format=format;sc.SampleDesc.Count=1;sc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.BufferCount=2;sc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;ComPtr<IDXGISwapChain1> first;
        CHECK(SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(),window,&sc,nullptr,nullptr,&first)));CHECK(SUCCEEDED(first.As(&swap)));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;CHECK(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))));
        CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
        CHECK(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));CHECK(SUCCEEDED(list->Close()));
        CHECK(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);
    }
    void begin(){drain();CHECK(SUCCEEDED(allocator->Reset()));CHECK(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));}
    void submit(){CHECK(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);drain();}
    static D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};return b;}
    void clear(){begin();ComPtr<ID3D12Resource> buffer;CHECK(SUCCEEDED(swap->GetBuffer(swap->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer))));
        auto b=barrier(buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&b);
        auto rtv=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(buffer.Get(),nullptr,rtv);const float color[]={.02f,.04f,.06f,1};list->ClearRenderTargetView(rtv,color,0,nullptr);
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);submit();}
    std::vector<uint32_t> pixels(){begin();ComPtr<ID3D12Resource> buffer;CHECK(SUCCEEDED(swap->GetBuffer(swap->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer))));
        auto desc=buffer->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total{};device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&total);
        D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;
        rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;
        CHECK(SUCCEEDED(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))));
        auto b=barrier(buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=buffer.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);submit();
        void* mapped{};D3D12_RANGE range{0,size_t(total)};CHECK(SUCCEEDED(readback->Map(0,&range,&mapped)));std::vector<uint32_t> out(size_t(desc.Width)*desc.Height);
        for(unsigned y=0;y<desc.Height;++y)std::memcpy(out.data()+y*desc.Width,static_cast<char*>(mapped)+y*footprint.Footprint.RowPitch,size_t(desc.Width)*4);
        D3D12_RANGE empty{};readback->Unmap(0,&empty);return out;
    }
    ~Graphics(){if(event)CloseHandle(event);swap.Reset();if(window)DestroyWindow(window);}
};
void bmp(const char* path,const std::vector<uint32_t>& p,int width,int height){
    auto copy=p;for(auto& v:copy)v=(v&0xff00ff00)|((v&255)<<16)|((v>>16)&255);
    BITMAPFILEHEADER file{0x4d42,DWORD(sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+copy.size()*4),0,0,sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)};
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-height;info.biPlanes=1;info.biBitCount=32;
    FILE* stream{};fopen_s(&stream,path,"wb");CHECK(stream);fwrite(&file,sizeof(file),1,stream);fwrite(&info,sizeof(info),1,stream);fwrite(copy.data(),4,copy.size(),stream);fclose(stream);
}
// Exercise the documented poll -> process -> update -> render order, with
// timestamps from the current poll rather than the previous core frame.
static void fresh_input_options(){
    Graphics g;Worker render;auto* c=gl_create(GL_ABI_VERSION);CHECK(c);
    const gl_gameplay_context view{1,"Input freshness","Public overlay API regression",0};
    CHECK(gl_register_gameplay_context(c,&view)==GL_OK);
    CHECK(gl_setting_set(c,"context.1.gyro.activation",GL_HOLD)==GL_OK);
    CHECK(gl_setting_set(c,"context.1.gyro.space",GL_SPACE_LOCAL_YAW)==GL_OK);
    gl_endpoint device{};device.id=device.physical_id=42;device.source=GL_SOURCE_SDL;device.connected=1;
    device.caps={0x7fff,3,3,3,3,1,1};std::strcpy(device.name,"Fresh input fixture");
    CHECK(gl_register_endpoint(c,&device)==GL_OK);CHECK(gl_select_device(c,42)==GL_OK);
    auto* overlay=gl_overlay_create(c,GL_OVERLAY_ABI_VERSION);CHECK(overlay);
    const gl_overlay_dx12_desc desc{sizeof(desc),GL_OVERLAY_ABI_VERSION,0,0,g.window,g.swap.Get(),g.queue.Get()};
    render.run([&]{CHECK(gl_overlay_dx12_init(overlay,&desc)==GL_OK);});
    CHECK(gl_overlay_set_open(overlay,1)==GL_OK);
    uint64_t now=1000000000;bool streaming=false;
    auto frame=[&]{
        now+=16000000;
        if(streaming){
            gl_controls controls{};controls.timestamp_ns=now;
            CHECK(gl_submit_controls(c,42,&controls)==GL_OK);
            const gl_trigger_input triggers{now,GL_LEFT|GL_RIGHT,0,0};
            CHECK(gl_submit_trigger_input(c,42,&triggers)==GL_OK);
            const gl_sample sample{now,now,{0,10,0},{0,-1,0}};
            CHECK(gl_submit_sample(c,42,&sample)==GL_OK);
        }
        CHECK(gl_overlay_process(overlay)==GL_OK); // Exactly one call per update.
        CHECK(gl_set_gameplay_context_state(c,1,1,1)==GL_OK);
        gl_host_state host{};host.focused=host.camera_allowed=1;gl_output output{};
        CHECK(gl_update(c,now,&host,&output)==GL_OK);
        CHECK(output.yaw_degrees==0 && output.pitch_degrees==0); // UI capture still gates motion.
        render.run([&]{g.clear();CHECK(gl_overlay_dx12_render(overlay,.016,1)==GL_OK);});
    };
    auto key=[&](unsigned k){gl_overlay_win32_message(overlay,g.window,WM_KEYDOWN,k,0);frame();
        gl_overlay_win32_message(overlay,g.window,WM_KEYUP,k,0);frame();};
    auto choose_space=[&](unsigned index){
        const auto position=MAKELPARAM(700,238);
        gl_overlay_win32_message(overlay,g.window,WM_MOUSEMOVE,0,position);
        gl_overlay_win32_message(overlay,g.window,WM_LBUTTONDOWN,0,position);frame();
        gl_overlay_win32_message(overlay,g.window,WM_LBUTTONUP,0,position);frame();frame();
        key(VK_HOME);for(unsigned n=0;n<index;++n)key(VK_DOWN);key(VK_RETURN);frame();
        double value{};CHECK(gl_setting_get(c,"context.1.gyro.space",&value)==GL_OK);return int(value);
    };
    frame();frame();const auto absent=g.pixels();streaming=true;frame();frame();frame();const auto present=g.pixels();
    bool activators=false;for(unsigned y=400;y<650;++y)for(unsigned x=100;x<900;++x)
        activators|=absent[y*1024+x]!=present[y*1024+x];
    CHECK(activators);
    const unsigned spaces[]={GL_SPACE_PLAYER,GL_SPACE_WORLD,GL_SPACE_PLAYER_LEAN,GL_SPACE_WORLD_LEAN,
        GL_SPACE_LASER_POINTER,GL_SPACE_LOCAL_YAW,GL_SPACE_LOCAL_ROLL,GL_SPACE_LOCAL_YAW_ROLL,GL_SPACE_LOCAL_ADVANCED};
    for(unsigned n=0;n<9;++n){const auto actual=choose_space(n);
        if(actual!=int(spaces[n])){
            std::fprintf(stderr,"Space choice %u: got %d, expected %u\n",n,actual,spaces[n]);
            bmp("overlay-fresh-options-failure.bmp",g.pixels(),1024,720);
        }
        CHECK(actual==int(spaces[n]));}
    // Real absence must remain unavailable: do not solve freshness by forcing
    // every capability on or by moving the snapshot clock into the future.
    streaming=false;for(unsigned n=0;n<12;++n)frame();
    CHECK(choose_space(0)==GL_SPACE_LOCAL_YAW);
    streaming=true;frame();frame();frame();CHECK(choose_space(0)==GL_SPACE_PLAYER);
    device.caps.accelerometer=0;CHECK(gl_register_endpoint(c,&device)==GL_OK);
    frame();frame();CHECK(choose_space(0)==GL_SPACE_LOCAL_YAW);
    CHECK(gl_overlay_detach(overlay)==GL_OK);
    render.run([&]{CHECK(gl_overlay_dx12_shutdown(overlay)==GL_OK);});
    CHECK(gl_overlay_destroy(overlay)==GL_OK);
    // A detached/destroyed overlay must not leave a callback in its owner.
    gl_host_state host{};gl_output output{};CHECK(gl_update(c,now+16000000,&host,&output)==GL_OK);
    gl_destroy(c);
}
// Verify the selected view through a real slider edit, across immutable
// snapshots and three opening paths. No access to the private ImGui context.
void opening_view(){
    Graphics g;Worker render;auto* c=gl_create(GL_ABI_VERSION);CHECK(c);
    for(uint32_t id:{1u,2u}){
        const gl_gameplay_context view{id,id==1?"Explore":"Aim","",int32_t(id)};
        CHECK(gl_register_gameplay_context(c,&view)==GL_OK);
        const auto key="context."+std::to_string(id)+".gyro.space";
        CHECK(gl_setting_set(c,key.c_str(),GL_SPACE_LOCAL_YAW)==GL_OK);
    }
    gl_endpoint e{};e.id=e.physical_id=1;e.source=GL_SOURCE_SDL;e.connected=1;e.caps.buttons=(1u<<4)|(1u<<6);e.caps.gyro=1;
    CHECK(gl_register_endpoint(c,&e)==GL_OK);CHECK(gl_select_device(c,1)==GL_OK);
    auto* o=gl_overlay_create(c,GL_OVERLAY_ABI_VERSION);CHECK(o);
    const gl_overlay_dx12_desc desc{sizeof(desc),GL_OVERLAY_ABI_VERSION,0,0,g.window,g.swap.Get(),g.queue.Get()};
    render.run([&]{CHECK(gl_overlay_dx12_init(o,&desc)==GL_OK);});
    uint64_t now=1000000000;uint32_t active=2,buttons=0;
    const auto frame=[&]{
        now+=16000000;gl_controls controls{};controls.timestamp_ns=now;controls.buttons=buttons;
        CHECK(gl_submit_controls(c,1,&controls)==GL_OK);
        CHECK(gl_set_gameplay_context_state(c,1,active==1,1)==GL_OK);CHECK(gl_set_gameplay_context_state(c,2,active==2,1)==GL_OK);
        CHECK(gl_overlay_process(o)==GL_OK);gl_host_state host{};host.focused=1;gl_output out{};CHECK(gl_update(c,now,&host,&out)==GL_OK);
        render.run([&]{g.clear();CHECK(gl_overlay_dx12_render(o,.016,1)==GL_OK);});
    };
    const auto click=[&](int x,int y){auto p=MAKELPARAM(x,y);gl_overlay_win32_message(o,g.window,WM_MOUSEMOVE,0,p);
        gl_overlay_win32_message(o,g.window,WM_LBUTTONDOWN,0,p);frame();
        gl_overlay_win32_message(o,g.window,WM_LBUTTONUP,0,p);frame();frame();};
    const auto edit=[&](uint32_t expected){
        CHECK(gl_setting_set(c,"context.1.sensitivity_x",1)==GL_OK);CHECK(gl_setting_set(c,"context.2.sensitivity_x",2)==GL_OK);
        for(int n=0;n<4;++n)frame();click(600,317);
        double first{},second{};CHECK(gl_setting_get(c,"context.1.sensitivity_x",&first)==GL_OK);CHECK(gl_setting_get(c,"context.2.sensitivity_x",&second)==GL_OK);
        if(expected==2?!(first==1&&second!=2):!(first!=1&&second==2)){
            bmp("overlay-opening-failure.bmp",g.pixels(),1024,720);
            std::fprintf(stderr,"Opening view expected %u, values %.3f %.3f\n",expected,first,second);
        }
        CHECK(expected==2?(first==1&&second!=2):(first!=1&&second==2));
    };
    frame();buttons=(1u<<4)|(1u<<6);frame();CHECK(gl_panel_open(c));edit(2);
    gl_set_panel_open(c,0);buttons=0;frame();gl_overlay_win32_message(o,g.window,WM_KEYDOWN,VK_F10,0);frame();edit(2);
    gl_set_panel_open(c,0);active=1;frame();CHECK(gl_overlay_set_open(o,1)==GL_OK);frame();edit(1);
    CHECK(gl_overlay_detach(o)==GL_OK);render.run([&]{CHECK(gl_overlay_dx12_shutdown(o)==GL_OK);});CHECK(gl_overlay_destroy(o)==GL_OK);gl_destroy(c);
}
void hdr_lifecycle(){
    for(auto format:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R10G10B10A2_UNORM}){
        Graphics g(format);Worker render;auto* c=gl_create(GL_ABI_VERSION);CHECK(c);
        auto* o=gl_overlay_create(c,GL_OVERLAY_ABI_VERSION);CHECK(o);
        gl_overlay_dx12_desc desc{sizeof(desc),GL_OVERLAY_ABI_VERSION,format==DXGI_FORMAT_R16G16B16A16_FLOAT?1u:12u,0,g.window,g.swap.Get(),g.queue.Get()};
        render.run([&]{auto invalid=desc;invalid.color_space=2;CHECK(gl_overlay_dx12_init(o,&invalid)==GL_UNAVAILABLE);
            CHECK(gl_overlay_dx12_init(o,&desc)==GL_OK);
            CHECK(gl_overlay_dx12_set_hdr_white_level(o,79)==GL_INVALID);CHECK(gl_overlay_dx12_set_hdr_white_level(o,1001)==GL_INVALID);
            CHECK(gl_overlay_dx12_set_hdr_white_level(o,NAN)==GL_INVALID);CHECK(gl_overlay_dx12_set_hdr_white_level(o,203)==GL_OK);});
        CHECK(gl_overlay_dx12_set_hdr_white_level(o,203)==GL_INVALID);
        render.run([&]{CHECK(gl_overlay_dx12_shutdown(o)==GL_OK);desc.color_space=GL_OVERLAY_COLOR_SPACE_AUTO;
            CHECK(gl_overlay_dx12_init(o,&desc)==GL_OK);});
        uint64_t now=1000000000;
        const auto frame=[&]{CHECK(gl_overlay_process(o)==GL_OK);gl_host_state host{};host.focused=1;gl_output out{};
            CHECK(gl_update(c,now+=16000000,&host,&out)==GL_OK);
            render.run([&]{g.clear();CHECK(gl_overlay_dx12_render(o,.016,1)==GL_OK);});};
        CHECK(gl_overlay_set_open(o,1)==GL_OK);for(int i=0;i<4;++i)frame();
        render.run([&]{CHECK(gl_overlay_dx12_before_resize(o)==GL_OK);CHECK(SUCCEEDED(g.swap->ResizeBuffers(2,800,600,format,0)));});
        for(int i=0;i<3;++i)frame();
        CHECK(gl_overlay_set_open(o,0)==GL_OK);frame();CHECK(gl_overlay_capture(o)==0);
        CHECK(gl_overlay_set_open(o,1)==GL_OK);frame();
        // Format/color-space switches reuse the overlay with a new renderer.
        render.run([&]{CHECK(gl_overlay_dx12_shutdown(o)==GL_OK);CHECK(SUCCEEDED(g.swap->ResizeBuffers(2,1024,720,DXGI_FORMAT_R8G8B8A8_UNORM,0)));
            desc.color_space=GL_OVERLAY_COLOR_SPACE_AUTO;CHECK(gl_overlay_dx12_init(o,&desc)==GL_OK);CHECK(gl_overlay_dx12_set_hdr_white_level(o,203)==GL_UNAVAILABLE);});
        frame();CHECK(gl_overlay_detach(o)==GL_OK);render.run([&]{CHECK(gl_overlay_dx12_shutdown(o)==GL_OK);});CHECK(gl_overlay_destroy(o)==GL_OK);gl_destroy(c);
    }
}
int main(int argc,char** argv)try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    const bool capture=argc>1&&std::strcmp(argv[1],"--capture")==0;Graphics g;Worker render;
    auto* c=gl_create(GL_ABI_VERSION);CHECK(c);gl_gameplay_context view{1,"Exploration","Look around while exploring.",1};
    CHECK(gl_register_gameplay_context(c,&view)==GL_OK);CHECK(gl_set_gameplay_context_output_target(c,1,GL_OUTPUT_CAMERA)==GL_OK);
    const auto settings=std::filesystem::current_path()/"overlay-test-settings.ini";
    CHECK(gl_set_settings_path(c,settings.string().c_str())==GL_OK);
    auto* o=gl_overlay_create(c,GL_OVERLAY_ABI_VERSION);CHECK(o);CHECK(!gl_overlay_create(c,GL_OVERLAY_ABI_VERSION));CHECK(!gl_overlay_create(c,999));
    CHECK(gl_overlay_destroy(o)==GL_INVALID);CHECK(gl_overlay_capture(o)==0);
    gl_overlay_dx12_desc desc{sizeof(desc),GL_OVERLAY_ABI_VERSION,0,0,g.window,g.swap.Get(),g.queue.Get()};
#ifdef GL_OVERLAY_TEST_HOST_IMGUI
    ImGuiContext* host_imgui{};render.run([&]{host_imgui=ImGui::CreateContext();});
#endif
    render.run([&]{auto invalid=desc;invalid.size=0;CHECK(gl_overlay_dx12_init(o,&invalid)==GL_INVALID);
        invalid=desc;invalid.color_space=12;CHECK(gl_overlay_dx12_init(o,&invalid)==GL_UNAVAILABLE);
        CHECK(gl_overlay_dx12_init(o,&desc)==GL_OK);CHECK(gl_overlay_process(o)==GL_INVALID);});
#ifdef GL_OVERLAY_TEST_HOST_IMGUI
    render.run([&]{CHECK(ImGui::GetCurrentContext()==host_imgui);});
#endif
    CHECK(gl_overlay_dx12_render(o,.016,1)==GL_INVALID);uint64_t now=1000000000;
    int view_state=0; // Explicitly unavailable, and no controller connected.
    auto frame=[&]{CHECK(gl_overlay_process(o)==GL_OK);gl_host_state host{};host.focused=host.camera_allowed=1;
        if(view_state>=0)CHECK(gl_set_gameplay_context_state(c,1,1,view_state)==GL_OK);
        gl_output output{};CHECK(gl_update(c,now+=16000000,&host,&output)==GL_OK);
        if(view_state!=1)CHECK(gl_get_active_gameplay_context(c)==0&&!output.gyro_active&&output.yaw_degrees==0&&output.pitch_degrees==0);
        render.run([&]{g.clear();CHECK(gl_overlay_dx12_render(o,.016,1)==GL_OK);
#ifdef GL_OVERLAY_TEST_HOST_IMGUI
            CHECK(ImGui::GetCurrentContext()==host_imgui);
#endif
        });};
    frame();auto closed=g.pixels();CHECK(closed[100*1024+100]==closed[500*1024+500]);
    CHECK(gl_overlay_win32_message(o,g.window,WM_KEYDOWN,VK_F10,0)&GL_OVERLAY_CAPTURE_KEYBOARD);frame();CHECK(gl_panel_open(c));
    CHECK(gl_overlay_capture(o)==7);for(int i=0;i<3;++i)frame();auto opened=g.pixels();CHECK(opened[100*1024+100]!=closed[100*1024+100]);
    if(capture)bmp("overlay-en-1024.bmp",opened,1024,720);
    // Unchanged UI state must not move, hide or redraw different setting rows
    // as owner snapshots are replaced between rendered frames.
    for(int n=0;n<8;++n){frame();const auto stable=g.pixels();
        size_t changed=0;for(unsigned y=195;y<640;++y)for(unsigned x=100;x<900;++x)
            changed+=stable[y*1024+x]!=opened[y*1024+x];
        if(changed){std::fprintf(stderr,"Unchanged menu frame %d: %zu changed setting pixels\n",n,changed);
            bmp("overlay-unstable-frame.bmp",stable,1024,720);}
        CHECK(changed==0);
    }
    CHECK(gl_overlay_win32_message(o,g.window,WM_KEYDOWN,VK_F10,1ll<<30)&GL_OVERLAY_CAPTURE_KEYBOARD);frame();CHECK(gl_panel_open(c));
    // Real mouse edits: slider starts after header/controller/tab/activation/space.
    auto click=[&](int x,int y){auto pos=MAKELPARAM(x,y);gl_overlay_win32_message(o,g.window,WM_MOUSEMOVE,0,pos);
        gl_overlay_win32_message(o,g.window,WM_LBUTTONDOWN,0,pos);frame();gl_overlay_win32_message(o,g.window,WM_LBUTTONUP,0,pos);frame();frame();};
    double before{};CHECK(gl_setting_get(c,"context.1.sensitivity_x",&before)==GL_OK);
    click(700,282);double after{};CHECK(gl_setting_get(c,"context.1.sensitivity_x",&after)==GL_OK);CHECK(after==before);
    gl_endpoint endpoint{};endpoint.id=endpoint.physical_id=42;endpoint.source=GL_SOURCE_SDL;endpoint.connected=1;
    endpoint.caps.buttons=0x7fff;endpoint.caps.sticks=GL_LEFT;endpoint.caps.gyro=1;
    std::strcpy(endpoint.name,"Test physical controller");CHECK(gl_register_endpoint(c,&endpoint)==GL_OK);CHECK(gl_select_device(c,42)==GL_OK);frame();frame();
    click(700,282);CHECK(gl_setting_get(c,"context.1.sensitivity_x",&after)==GL_OK);CHECK(after!=before);
    CHECK(gl_get_settings_save_result(c)==GL_OK);auto* loaded=gl_create(GL_ABI_VERSION);CHECK(loaded);
    CHECK(gl_register_gameplay_context(loaded,&view)==GL_OK);CHECK(gl_load_settings(loaded,settings.string().c_str())==GL_OK);
    double saved{};CHECK(gl_setting_get(loaded,"context.1.sensitivity_x",&saved)==GL_OK&&saved==after);gl_destroy(loaded);
    // Keyboard selects a different gyro space from the actual dropdown.
    double space_before{};CHECK(gl_setting_get(c,"context.1.gyro.space",&space_before)==GL_OK);
    click(700,238);
    gl_overlay_win32_message(o,g.window,WM_KEYDOWN,VK_END,0);frame();
    gl_overlay_win32_message(o,g.window,WM_KEYUP,VK_END,0);frame();
    gl_overlay_win32_message(o,g.window,WM_KEYDOWN,VK_RETURN,0);frame();
    gl_overlay_win32_message(o,g.window,WM_KEYUP,VK_RETURN,0);frame();frame();
    double space_after{};CHECK(gl_setting_get(c,"context.1.gyro.space",&space_after)==GL_OK);
    CHECK(space_after!=space_before);    // Action is queued to the real owner, not applied to a private settings copy.
    click(835,674);if(capture)bmp("overlay-reset.bmp",g.pixels(),1024,720);
    CHECK(gl_setting_get(c,"context.1.sensitivity_x",&after)==GL_OK);CHECK(after==before);
    // Missing host observations must also leave the same real widgets editable.
    view_state=-1;frame();frame();click(600,282);
    CHECK(gl_setting_get(c,"context.1.sensitivity_x",&after)==GL_OK&&after!=before);
    loaded=gl_create(GL_ABI_VERSION);CHECK(loaded);CHECK(gl_register_gameplay_context(loaded,&view)==GL_OK);
    CHECK(gl_load_settings(loaded,settings.string().c_str())==GL_OK);
    CHECK(gl_setting_get(loaded,"context.1.sensitivity_x",&saved)==GL_OK&&saved==after);gl_destroy(loaded);
    view_state=1;frame();
    // Keyboard and gamepad messages navigate the real independent frontend.
    for(int i=0;i<4;++i){gl_overlay_win32_message(o,g.window,WM_KEYDOWN,VK_TAB,0);frame();gl_overlay_win32_message(o,g.window,WM_KEYUP,VK_TAB,0);frame();}
    click(700,238);double pad_before{};CHECK(gl_setting_get(c,"context.1.gyro.space",&pad_before)==GL_OK);
    for(uint32_t buttons:{1u<<12,0u,1u<<0,0u}){gl_controls controls{};controls.timestamp_ns=now;controls.buttons=buttons;CHECK(gl_submit_controls(c,42,&controls)==GL_OK);frame();}
    frame();double pad_after{};CHECK(gl_setting_get(c,"context.1.gyro.space",&pad_after)==GL_OK&&pad_after!=pad_before);    CHECK(gl_set_language(c,"fr")==GL_OK);frame();if(capture)bmp("overlay-fr-1024.bmp",g.pixels(),1024,720);
    render.run([&]{CHECK(gl_overlay_dx12_before_resize(o)==GL_OK);CHECK(SUCCEEDED(g.swap->ResizeBuffers(2,3840,2160,DXGI_FORMAT_R8G8B8A8_UNORM,0)));});
    SetWindowPos(g.window,nullptr,0,0,3840,2160,SWP_NOZORDER|SWP_NOACTIVATE);for(int i=0;i<3;++i)frame();
    auto large=g.pixels();CHECK(large[1000*3840+1000]!=large[0]);if(capture)bmp("overlay-fr-4k.bmp",large,3840,2160);
    // One real selected controller supplies UI navigation without XInput polling.
    for(uint32_t buttons:{1u<<12,0u,1u<<0,0u}){gl_controls controls{};controls.timestamp_ns=now;controls.buttons=buttons;CHECK(gl_submit_controls(c,42,&controls)==GL_OK);frame();}
    CHECK(gl_set_menu_key(c,8)==GL_OK);frame();gl_overlay_win32_message(o,g.window,WM_KEYDOWN,VK_F10,0);frame();CHECK(gl_panel_open(c));
    gl_overlay_win32_message(o,g.window,WM_KEYDOWN,VK_F8,0);frame();CHECK(!gl_panel_open(c));CHECK(gl_overlay_capture(o)==0);
    // Owner-thread controller chord reaches the independent DX12 renderer and
    // survives the next process() without being overwritten by its open state.
    const auto chord_frame=[&](uint32_t buttons){gl_controls controls{};controls.timestamp_ns=now;controls.buttons=buttons;
        CHECK(gl_submit_controls(c,42,&controls)==GL_OK);frame();};
    chord_frame(0);chord_frame((1u<<4)|(1u<<6));CHECK(gl_panel_open(c)&&gl_overlay_capture(o)==7);
    chord_frame((1u<<4)|(1u<<6));CHECK(gl_panel_open(c));
    chord_frame(0);chord_frame((1u<<4)|(1u<<6));CHECK(!gl_panel_open(c)&&gl_overlay_capture(o)==0);
    CHECK(gl_set_gamepad_menu_shortcut(c,0)==GL_OK);chord_frame(0);chord_frame((1u<<4)|(1u<<6));CHECK(!gl_panel_open(c));
    CHECK(gl_set_menu_key(c,0)==GL_OK);frame();gl_overlay_win32_message(o,g.window,WM_KEYDOWN,VK_F8,0);frame();CHECK(!gl_panel_open(c));
    CHECK(gl_overlay_set_open(o,1)==GL_OK);frame();Sleep(300);render.run([&]{CHECK(gl_overlay_dx12_render(o,.016,1)==GL_OK);});CHECK(gl_overlay_capture(o)==0);frame();CHECK(!gl_panel_open(c));
    // Inheritance and recommendations cross the render -> owner command queue.
    const gl_gameplay_context parent_view{2,"Standard aim","",0};
    CHECK(gl_register_gameplay_context(c,&parent_view)==GL_OK);
    CHECK(gl_setting_set(c,"context.2.sensitivity_x",6)==GL_OK);
    CHECK(gl_set_context_parent(c,1,2)==GL_OK);CHECK(gl_setting_set(c,"context.1.sensitivity_x",1.5)==GL_OK);
    CHECK(gl_capture_recommended_settings(c)==GL_OK);CHECK(gl_setting_set(c,"context.1.sensitivity_x",2.1)==GL_OK);
    CHECK(gl_set_language(c,"en")==GL_OK);
    render.run([&]{CHECK(gl_overlay_dx12_before_resize(o)==GL_OK);CHECK(SUCCEEDED(g.swap->ResizeBuffers(2,1024,720,DXGI_FORMAT_R8G8B8A8_UNORM,0)));});
    SetWindowPos(g.window,nullptr,0,0,1024,720,SWP_NOZORDER|SWP_NOACTIVATE);
    CHECK(gl_overlay_set_open(o,1)==GL_OK);for(int i=0;i<4;++i)frame();
    // Finish the earlier gamepad slider edit before testing independent mouse actions.
    gl_overlay_win32_message(o,g.window,WM_KEYDOWN,VK_ESCAPE,0);frame();
    gl_overlay_win32_message(o,g.window,WM_KEYUP,VK_ESCAPE,0);frame();
    if(capture)bmp("overlay-inheritance.bmp",g.pixels(),1024,720);
    click(778,317);double inherited_value{};CHECK(gl_setting_get(c,"context.1.sensitivity_x",&inherited_value)==GL_OK);
    if(capture)bmp("overlay-inheritance-click.bmp",g.pixels(),1024,720);
    CHECK(inherited_value==6);
    click(820,674);CHECK(gl_setting_get(c,"context.1.sensitivity_x",&inherited_value)==GL_OK);CHECK(inherited_value==1.5);
    click(700,202);
    for(auto key:{VK_HOME,VK_RETURN}){gl_overlay_win32_message(o,g.window,WM_KEYDOWN,key,0);frame();gl_overlay_win32_message(o,g.window,WM_KEYUP,key,0);frame();}
    frame();CHECK(gl_get_context_parent(c,1)==0);CHECK(gl_setting_get(c,"context.1.sensitivity_x",&inherited_value)==GL_OK&&inherited_value==1.5);
    click(820,674);CHECK(gl_get_context_parent(c,1)==2);
    // Independent ImGui is never supplied to the mod; no writes beyond its INI.
    gl_event event{};unsigned events=0;while(gl_poll_event(c,&event)==1)++events;CHECK(events>0);
    CHECK(gl_overlay_detach(o)==GL_OK);CHECK(gl_overlay_process(o)==GL_UNAVAILABLE);
    render.run([&]{CHECK(gl_overlay_dx12_shutdown(o)==GL_OK);
#ifdef GL_OVERLAY_TEST_HOST_IMGUI
        CHECK(ImGui::GetCurrentContext()==host_imgui);ImGui::DestroyContext(host_imgui);
#endif
    });CHECK(gl_overlay_destroy(o)==GL_OK);gl_destroy(c);
    std::filesystem::remove(settings);fresh_input_options();opening_view();hdr_lifecycle();
    std::puts("Autonomous DX12 panel: fresh activators/all gyro spaces, WARP pixels, independent threads, input, resize, gating and lifecycle passed.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
