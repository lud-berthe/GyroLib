#pragma once
#include "sdl_controls.hpp"
#include <memory>
#include <cstring>

namespace gyrolib_sdl_detail {
// Optional output-only backend. Packet layout: SDL release-3.4.16
// steam/controller_structs.h, MsgHapticPulse. Right-pad selector 0 is verified
// by Linux hid-steam.c steam_haptic_pulse (legacy L/R swap). This is an output
// report, NEVER a feature report. No mappings, firmware or controller settings.
class TouchpadHaptic {
    std::unique_ptr<SDL_hid_device,decltype(&SDL_hid_close)> device_{nullptr,SDL_hid_close};
    bool attempted_{};
    uint64_t last_{};
public:
    void reset(){device_.reset();attempted_=false;last_=0;}
    int pulse(SDL_Gamepad* pad,uint64_t now){
        if(!pad||!SDL_GamepadConnected(pad)||SDL_GetGamepadSteamHandle(pad)||
           SDL_GetNumGamepadTouchpads(pad)<2||!SDL_GetBooleanProperty(SDL_GetJoystickProperties(SDL_GetGamepadJoystick(pad)),"gyrolib.output.right_pad_pulse",false))return GL_UNAVAILABLE;
        if(last_&&now>=last_&&now-last_<50000000)return GL_OK; // at most 20 Hz, no catch-up queue
        // One 400 us pulse on the right pad; no ongoing effect to cancel.
        const unsigned char packet[8]={0x81,0,0x90,0x01,0,0,1,0};
        if(SDL_IsJoystickVirtual(SDL_GetGamepadID(pad))){
            last_=now;return SDL_SendGamepadEffect(pad,packet,sizeof(packet))?GL_OK:GL_UNAVAILABLE;
        }
        if(!attempted_){
            attempted_=true;
            const char* path=SDL_GetGamepadPath(pad);
            auto* devices=SDL_hid_enumerate(SDL_GetGamepadVendor(pad),SDL_GetGamepadProduct(pad));
            // The exact already-selected interface, never another receiver slot.
            for(auto* d=devices;path&&d;d=d->next)if(d->path&&std::strcmp(path,d->path)==0&&d->bus_type==SDL_HID_API_BUS_USB){
                device_.reset(SDL_hid_open_path(path));break;
            }
            SDL_hid_free_enumeration(devices);
        }
        if(!device_)return GL_UNAVAILABLE;
        last_=now;
        return SDL_hid_write(device_.get(),packet,sizeof(packet))==sizeof(packet)?GL_OK:GL_IO_ERROR;
    }
};
}
