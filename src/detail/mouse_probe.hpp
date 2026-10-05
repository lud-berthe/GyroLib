#pragma once
// Observation only, opt-in experimental build. Does not register raw devices,
// install hooks, suppress messages, synthesize input or access keyboard text.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <chrono>

namespace gyrolib_detail {
class MouseProbe {
    std::mutex mutex_;
    std::ofstream file_;
    std::unordered_set<uintptr_t> devices_;
    uint64_t start_{},flushed_{};
    unsigned rows_{},phase_{};
    bool configured_{};
    static uint64_t clock_ms(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());}
public:
    void configure(const std::string& settings_path) noexcept {
        try {
            std::lock_guard lock(mutex_);
            if(configured_||settings_path.empty())return;
            configured_=true;
            const auto directory=std::filesystem::u8path(settings_path).parent_path();
            if(!std::filesystem::is_regular_file(directory/"gyrolib-mouse-probe.enable"))return;
            file_.open(directory/("gyrolib-mouse-probe-"+std::to_string(GetCurrentProcessId())+".csv"),std::ios::trunc);
            file_<<"# Mouse experiment. F7 starts/restarts 60 seconds; F8 changes phase. Keys still reach the host.\n"
                <<"phase,elapsed_ms,message,source_ok,device_type,origin,device,flags,x,y,raw_extra,message_extra,raw_result,routed\n";
            file_.flush();
        }catch(...){}
    }
    void camera(uint32_t view,double yaw,double pitch) noexcept {
        try{std::lock_guard lock(mutex_);const auto now=clock_ms();
            if(file_.is_open()&&start_&&now-start_<=60000&&rows_<50000&&(yaw||pitch)){
                file_<<"# CAMERA "<<phase_<<' '<<(now-start_)<<' '<<view<<' '<<yaw<<' '<<pitch<<'\n';++rows_;
            }
        }catch(...){}
    }
    void message(uint32_t message,uint64_t wp,int64_t lp,bool routed=false) noexcept {
        try {
            std::lock_guard lock(mutex_);if(!file_.is_open())return;
            const auto now=clock_ms();
            if(message==WM_KEYDOWN&&!(lp&(1ll<<30))){
                if(wp==VK_F7){start_=flushed_=now;rows_=0;phase_=1;devices_.clear();file_<<"# START\n";file_.flush();}
                if(wp==VK_F8&&start_){++phase_;file_<<"# PHASE "<<phase_<<'\n';file_.flush();}
                return;
            }
            if(!start_||now-start_>60000||rows_>=50000)return;
            if(message!=WM_INPUT&&message!=WM_MOUSEMOVE)return;
            INPUT_MESSAGE_SOURCE source{};const auto source_ok=GetCurrentInputMessageSource(&source);
            RAWINPUT input{};UINT raw_result=0;
            if(message==WM_INPUT){
                RAWINPUTHEADER header{};UINT size=sizeof(header);
                const auto result=GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_HEADER,&header,&size,sizeof(header));
                if(result==UINT(-1)||header.dwType!=RIM_TYPEMOUSE)return;
                size=sizeof(input);raw_result=GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_INPUT,&input,&size,sizeof(header));
                if(raw_result==UINT(-1))input={};
            }
            const auto device=reinterpret_cast<uintptr_t>(input.header.hDevice);
            if(device&&devices_.size()<32&&devices_.insert(device).second){
                wchar_t name[1024]{};UINT length=1024;
                if(GetRawInputDeviceInfoW(input.header.hDevice,RIDI_DEVICENAME,name,&length)!=UINT(-1)){
                    name[1023]=0;char utf8[4096]{};
                    if(WideCharToMultiByte(CP_UTF8,0,name,-1,utf8,sizeof(utf8),nullptr,nullptr))file_<<"# DEVICE "<<device<<' '<<utf8<<'\n';
                }
            }
            const auto x=message==WM_INPUT?input.data.mouse.lLastX:LONG(short(LOWORD(lp)));
            const auto y=message==WM_INPUT?input.data.mouse.lLastY:LONG(short(HIWORD(lp)));
            file_<<phase_<<','<<(now-start_)<<','<<message<<','<<source_ok<<','<<source.deviceType<<','<<source.originId<<','
                <<device<<','<<input.data.mouse.usFlags<<','<<x<<','<<y<<','<<input.data.mouse.ulExtraInformation<<','
                <<uintptr_t(GetMessageExtraInfo())<<','<<raw_result<<','<<routed<<'\n';
            ++rows_;if(now-flushed_>=250){file_.flush();flushed_=now;}
        }catch(...){}
    }
};
}
