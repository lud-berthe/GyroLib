#pragma once
#include "mouse_route.hpp"
#include <atomic>

namespace gyrolib_detail {
// The hook thread never locks the bridge or calls the host. Short-lived leases
// let it stop intercepting even if the host stops updating without detaching.
struct MouseHookGate {
    std::atomic<uint64_t> observe_until{},block_until{},injected_at{};
    void clear(){block_until=0;observe_until=0;injected_at=0;}
    void publish(const MouseRoute& route,uint64_t now){
        uint64_t until=0;
        if(route.candidate(now)){
            until=route.updated+100;
            if(!route.steam_input&&!route.touching&&route.last_packet+150<until)until=route.last_packet+150;
        }
        block_until=route.armed(now)?until:0;observe_until=until;
    }
    bool movement(uint64_t now,bool injected,bool foreground){
        if(!injected||!foreground)return false;
        const auto observing=observe_until.load();
        if(!observing||now>observing)return false;
        injected_at=now;
        const auto blocking=block_until.load();
        return blocking&&now<=blocking;
    }
    void corroborate(MouseRoute& route,uint64_t now)const{
        const auto stamp=injected_at.load();
        if(route.candidate(now)&&MouseRoute::fresh(now,stamp,100)&&stamp>route.injected_at)route.injected_at=stamp;
    }
};
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <chrono>
#include <future>
#include <thread>

namespace gyrolib_detail {
class MouseHook {
    std::thread thread;
    DWORD thread_id{};
    inline static thread_local MouseHook* current{};
    static LRESULT CALLBACK callback(int code,WPARAM wp,LPARAM lp){
        auto* self=current;
        if(code==HC_ACTION&&wp==WM_MOUSEMOVE&&self){
            const auto* event=reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
            if(event->flags&LLMHF_INJECTED){
                const auto window=self->window.load();
                const bool foreground=window&&GetForegroundWindow()==GetAncestor(window,GA_ROOT)&&!IsIconic(window);
                const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                if(self->gate.movement(now,true,foreground))return 1;
            }
        }
        return CallNextHookEx(nullptr,code,wp,lp);
    }
public:
    MouseHookGate gate;
    std::atomic<HWND> window{};
    bool start()noexcept{
        if(thread.joinable())return true;
        try{
            std::promise<bool> ready;auto result=ready.get_future();
            thread=std::thread([this,ready=std::move(ready)]()mutable{
                current=this;thread_id=GetCurrentThreadId();MSG message{};
                PeekMessageW(&message,nullptr,WM_USER,WM_USER,PM_NOREMOVE); // create the stop-message queue
                HMODULE module{};
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&callback),&module);
                const auto hook=SetWindowsHookExW(WH_MOUSE_LL,callback,module,0);
                ready.set_value(hook!=nullptr);
                if(hook){while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}UnhookWindowsHookEx(hook);}
                current=nullptr;
            });
            if(result.get())return true;
            thread.join();return false;
        }catch(...){return false;}
    }
    void stop(){
        gate.clear();window=nullptr;
        if(thread.joinable()){PostThreadMessageW(thread_id,WM_QUIT,0,0);thread.join();}
    }
    ~MouseHook(){stop();}
};
}
#endif
