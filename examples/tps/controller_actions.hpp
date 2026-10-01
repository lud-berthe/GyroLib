#pragma once
#include <gyrolib/gyrolib.h>
#include <string>

namespace tps {
// The demo uses SDL's normalized button indices. A real mod maps its own
// command events to these bindings instead; no system input is injected.
enum { ButtonSouth=0,ButtonEast=1,ButtonWest=2,ButtonNorth=3,ButtonStart=6 };
class ControllerActions {
    uint64_t device_{};
    uint32_t held_{};
public:
    uint32_t update(gl_context* gyro,uint64_t now,uint64_t device,uint32_t buttons,bool focused,bool native_hold){
        if(device!=device_||!focused){
            // Finish old cycles without emitting taps; a newly connected or
            // refocused controller's already-held buttons are not new presses.
            for(unsigned n=0;n<32;++n)if(held_&(1u<<n))gl_filter_event(gyro,n,GL_RELEASE,now,0,1);
            device_=device;held_=buttons;return 0;
        }
        double binding=0;
        const auto key="context."+std::to_string(gl_get_active_gameplay_context(gyro))+".activation.button";
        gl_setting_get(gyro,key.c_str(),&binding);
        uint32_t actions=0;
        for(unsigned n=0;n<32;++n){
            const uint32_t bit=1u<<n;const bool down=(buttons&bit)!=0,was_down=(held_&bit)!=0;
            if(!down&&!was_down)continue;
            const auto event=down?(was_down?GL_REPEAT:GL_PRESS):GL_RELEASE;
            const auto result=gl_filter_event(gyro,n,event,now,binding==double(n+1),native_hold);
            if(result==GL_EMIT_TAP||(result==GL_FORWARD&&event==GL_PRESS))actions|=bit;
        }
        held_=buttons;return actions;
    }
};
}
