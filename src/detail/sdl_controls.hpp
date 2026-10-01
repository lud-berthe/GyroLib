#pragma once
#include <gyrolib/gyrolib.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>
namespace gyrolib_sdl_detail {
// Generic provider metadata, supplied by drivers that know physical origins.
// The SDL backport publishes it through joystick properties; other providers
// can describe the same families through gl_set_endpoint_control_authority.
struct ControlLayout {
    gl_capabilities caps{};
    std::array<uint32_t,32> stick{},grip{};
    // SDL property strings may be replaced between polls. Own the cached text.
    std::array<std::string,32> labels{};
    std::array<const char*,2> trigger_labels{};
    std::array<int,2> fingers{};
    int physical_sticks{-1};std::array<int,4> stick_axes{-1,-1,-1,-1};
    uint32_t authority{},triggers{};
    int touchpads{};
};
inline const char* button_name(SDL_Gamepad*,SDL_GamepadButton,const ControlLayout&);
inline ControlLayout control_layout(SDL_Gamepad* pad) {
    ControlLayout result;
    // Origins belong to this joystick's driver, not its name, mapping or Steam
    // handle. Steam virtual output does not inherit another joystick's properties.
    const auto props=SDL_GetJoystickProperties(SDL_GetGamepadJoystick(pad));
    result.physical_sticks=static_cast<int>(SDL_GetNumberProperty(props,"gyrolib.controls.sticks",-1));
    result.authority=static_cast<uint32_t>(SDL_GetNumberProperty(props,"gyrolib.controls.authority",0))&GL_CONTROL_ALL;
    const char* axes[]={"gyrolib.controls.left.x_axis","gyrolib.controls.left.y_axis","gyrolib.controls.right.x_axis","gyrolib.controls.right.y_axis"};
    for(int a=0;a<4;++a)result.stick_axes[a]=static_cast<int>(SDL_GetNumberProperty(props,axes[a],-1));
    int count=0;auto** bindings=SDL_GetGamepadBindings(pad,&count);
    std::unique_ptr<SDL_GamepadBinding*,decltype(&SDL_free)> owned_bindings(bindings,SDL_free);
    std::array<int,4> mapped_axes{-1,-1,-1,-1};
    for(int i=0;i<count;++i){const auto& b=*bindings[i];
        if(b.output_type==SDL_GAMEPAD_BINDTYPE_AXIS&&b.input_type==SDL_GAMEPAD_BINDTYPE_AXIS&&
            b.output.axis.axis>=SDL_GAMEPAD_AXIS_LEFTX&&b.output.axis.axis<=SDL_GAMEPAD_AXIS_RIGHTY)
            mapped_axes[b.output.axis.axis]=b.input.axis.axis;
        if(b.output_type!=SDL_GAMEPAD_BINDTYPE_BUTTON||b.input_type!=SDL_GAMEPAD_BINDTYPE_BUTTON||b.output.button<0||b.output.button>=32)continue;
        char key[96];std::snprintf(key,sizeof(key),"gyrolib.controls.button.%d.name",b.input.button);
        result.labels[b.output.button]=SDL_GetStringProperty(props,key,"");
        std::snprintf(key,sizeof(key),"gyrolib.controls.button.%d.stick_touch",b.input.button);
        result.stick[b.output.button]=static_cast<uint32_t>(SDL_GetNumberProperty(props,key,0));
        std::snprintf(key,sizeof(key),"gyrolib.controls.button.%d.grip_touch",b.input.button);
        result.grip[b.output.button]=static_cast<uint32_t>(SDL_GetNumberProperty(props,key,0));
    }
    if(mapped_axes[0]>=0&&mapped_axes[1]>=0&&mapped_axes[0]!=mapped_axes[1])result.caps.sticks|=GL_LEFT;
    if(mapped_axes[2]>=0&&mapped_axes[3]>=0&&mapped_axes[2]!=mapped_axes[3])result.caps.sticks|=GL_RIGHT;
    if(result.physical_sticks>=0){
        result.caps.sticks=static_cast<uint32_t>(result.physical_sticks)&(GL_LEFT|GL_RIGHT);
        const int axis_count=SDL_GetNumJoystickAxes(SDL_GetGamepadJoystick(pad));
        for(int side=0;side<2;++side){const int a=side*2;
            if(result.stick_axes[a]<0||result.stick_axes[a+1]<0||result.stick_axes[a]>=axis_count||
               result.stick_axes[a+1]>=axis_count||result.stick_axes[a]==result.stick_axes[a+1])
                result.caps.sticks&=~(side?GL_RIGHT:GL_LEFT);
        }
    }
    for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT&&b<32;++b){
        const auto button=static_cast<SDL_GamepadButton>(b);
        if(!SDL_GamepadHasButton(pad,button)){result.stick[b]=result.grip[b]=0;result.labels[b].clear();continue;}
        result.caps.buttons|=1u<<b;result.caps.stick_touch|=result.stick[b];result.caps.grip_touch|=result.grip[b];
        if(result.labels[b].empty())if(const auto* name=button_name(pad,button,result))result.labels[b]=name;
    }
    result.touchpads=SDL_GetNumGamepadTouchpads(pad);
    result.caps.touchpads=result.touchpads==1?GL_SINGLE:result.touchpads>=2?GL_LEFT|GL_RIGHT:0;
    for(int t=0;t<std::min(result.touchpads,2);++t)result.fingers[t]=SDL_GetNumGamepadTouchpadFingers(pad,t);
    result.caps.gyro=SDL_GamepadHasSensor(pad,SDL_SENSOR_GYRO);
    result.caps.accelerometer=SDL_GamepadHasSensor(pad,SDL_SENSOR_ACCEL);
    const auto type=SDL_GetRealGamepadType(pad);
    const bool sony=type==SDL_GAMEPAD_TYPE_PS3||type==SDL_GAMEPAD_TYPE_PS4||type==SDL_GAMEPAD_TYPE_PS5;
    const bool nintendo=type>=SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO&&type<=SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
    for(int side=0;side<2;++side){
        if(!SDL_GamepadHasAxis(pad,side?SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:SDL_GAMEPAD_AXIS_LEFT_TRIGGER))continue;
        result.triggers|=side?GL_RIGHT:GL_LEFT;
        result.trigger_labels[side]=sony?(side?"R2":"L2"):nintendo?(side?"ZR":"ZL"):(side?"RT":"LT");
    }
    return result;
}
inline float axis(Sint16 value){return value<0?value/32768.0f:value/32767.0f;}
// Labels are metadata only. Capability detection still uses SDL_GamepadHasButton.
inline const char* button_name(SDL_Gamepad* pad,SDL_GamepadButton button,const ControlLayout& layout) {
    if(layout.stick[button])return layout.stick[button]==GL_LEFT?"L3 touch":"R3 touch";
    if(layout.grip[button])return layout.grip[button]==GL_LEFT?"Grip Sense L":"Grip Sense R";
    switch(SDL_GetGamepadButtonLabel(pad,button)) {
        case SDL_GAMEPAD_BUTTON_LABEL_A:return "A";case SDL_GAMEPAD_BUTTON_LABEL_B:return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X:return "X";case SDL_GAMEPAD_BUTTON_LABEL_Y:return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS:return "Cross";case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:return "Circle";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:return "Square";case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:return "Triangle";
        default:break;
    }
    auto type=SDL_GetRealGamepadType(pad);
    bool sony=type==SDL_GAMEPAD_TYPE_PS3||type==SDL_GAMEPAD_TYPE_PS4||type==SDL_GAMEPAD_TYPE_PS5;
    bool xbox=type==SDL_GAMEPAD_TYPE_XBOXONE||type==SDL_GAMEPAD_TYPE_XBOX360;
    bool nintendo=type>=SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO&&type<=SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
    if(sony) {
        switch(button) {
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:return "L1";case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:return "R1";
            case SDL_GAMEPAD_BUTTON_LEFT_STICK:return "L3";case SDL_GAMEPAD_BUTTON_RIGHT_STICK:return "R3";
            case SDL_GAMEPAD_BUTTON_GUIDE:return "PS";
            case SDL_GAMEPAD_BUTTON_BACK:return type==SDL_GAMEPAD_TYPE_PS5?"Create":type==SDL_GAMEPAD_TYPE_PS4?"Share":"Select";
            case SDL_GAMEPAD_BUTTON_START:return type==SDL_GAMEPAD_TYPE_PS3?"Start":"Options";
            case SDL_GAMEPAD_BUTTON_MISC1:if(type==SDL_GAMEPAD_TYPE_PS5)return "Mute";break;
            // SDL's PS5 family does not itself prove Edge hardware. Exact extra
            // labels require its verified product ID; ordinary pads stay generic.
            case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:if(SDL_GetGamepadVendor(pad)==0x054c&&SDL_GetGamepadProduct(pad)==0x0df2)return "RB";break;
            case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:if(SDL_GetGamepadVendor(pad)==0x054c&&SDL_GetGamepadProduct(pad)==0x0df2)return "LB";break;
            case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:if(SDL_GetGamepadVendor(pad)==0x054c&&SDL_GetGamepadProduct(pad)==0x0df2)return "Right Fn";break;
            case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:if(SDL_GetGamepadVendor(pad)==0x054c&&SDL_GetGamepadProduct(pad)==0x0df2)return "Left Fn";break;
            default:break;
        }
    }
    if(xbox) {
        switch(button) {
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:return "LB";case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:return "RB";
            case SDL_GAMEPAD_BUTTON_LEFT_STICK:return "LS";case SDL_GAMEPAD_BUTTON_RIGHT_STICK:return "RS";
            case SDL_GAMEPAD_BUTTON_GUIDE:return "Xbox";
            case SDL_GAMEPAD_BUTTON_BACK:return type==SDL_GAMEPAD_TYPE_XBOX360?"Back":"View";
            case SDL_GAMEPAD_BUTTON_START:return type==SDL_GAMEPAD_TYPE_XBOX360?"Start":"Menu";
            // Extra buttons on third-party Xbox-compatible pads need origin metadata.
            default:break;
        }
    }
    if(nintendo) {
        switch(button) {
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:return "L";case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:return "R";
            case SDL_GAMEPAD_BUTTON_BACK:return "-";case SDL_GAMEPAD_BUTTON_START:return "+";
            case SDL_GAMEPAD_BUTTON_GUIDE:return "HOME";case SDL_GAMEPAD_BUTTON_MISC1:return "Capture";
            case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
                if(type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT||type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR)return "Right SR";break;
            case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
                if(type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT||type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR)return "Left SL";break;
            case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
                if(type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT||type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR)return "Right SL";break;
            case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
                if(type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT||type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR)return "Left SR";break;
            default:break;
        }
    }
    return nullptr;
}
// Only live values travel through the per-poll path. Refresh ControlLayout on
// hotplug/remapping, plus the 500 ms fallback for driver property changes.
struct ControlSnapshot {
    gl_controls controls{};
    gl_flick_input flick{};
    gl_trigger_input triggers{};
};
inline ControlSnapshot read_controls(SDL_Gamepad* pad,const ControlLayout& layout) {
    ControlSnapshot r;
    for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT&&b<32;++b){
        if(!(layout.caps.buttons&(1u<<b)))continue;
        if(SDL_GetGamepadButton(pad,static_cast<SDL_GamepadButton>(b))){
            // Keep the stable button aliases for existing saved bindings.
            r.controls.buttons|=1u<<b;r.controls.stick_touch|=layout.stick[b];r.controls.grip_touch|=layout.grip[b];
        }
    }
    if(layout.physical_sticks>=0){
        auto* joy=SDL_GetGamepadJoystick(pad);
        if(layout.caps.sticks&GL_LEFT){r.controls.left_x=axis(SDL_GetJoystickAxis(joy,layout.stick_axes[0]));r.controls.left_y=-axis(SDL_GetJoystickAxis(joy,layout.stick_axes[1]));}
        if(layout.caps.sticks&GL_RIGHT){r.controls.right_x=axis(SDL_GetJoystickAxis(joy,layout.stick_axes[2]));r.controls.right_y=-axis(SDL_GetJoystickAxis(joy,layout.stick_axes[3]));}
    }else{
        r.controls.left_x=axis(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX));r.controls.left_y=-axis(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTY));
        r.controls.right_x=axis(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTX));r.controls.right_y=-axis(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTY));
    }
    if(layout.caps.sticks&GL_RIGHT){r.flick.available|=GL_FLICK_INPUT_STICK;r.flick.stick_x=r.controls.right_x;r.flick.stick_y=r.controls.right_y;}
    if(layout.touchpads>=2)r.flick.available|=GL_FLICK_INPUT_TOUCHPAD;
    for(int t=0;t<std::min(layout.touchpads,2);++t)for(int f=0;f<layout.fingers[t];++f){
        bool down=false;float x=.5f,y=.5f;
        if(!SDL_GetGamepadTouchpadFinger(pad,t,f,&down,&x,&y,nullptr)||!down)continue;
        r.controls.touchpads|=layout.touchpads==1?GL_SINGLE:t==0?GL_LEFT:GL_RIGHT;
        if(t==1&&f==0){
            r.flick.touching=1;r.flick.touchpad_x=std::clamp(x*2-1,-1.f,1.f);r.flick.touchpad_y=std::clamp(1-y*2,-1.f,1.f);
        }
    }
    r.triggers.available=layout.triggers;
    if(layout.triggers&GL_LEFT)r.triggers.left=std::clamp(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)/32767.f,0.f,1.f);
    if(layout.triggers&GL_RIGHT)r.triggers.right=std::clamp(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)/32767.f,0.f,1.f);
    return r;
}
}
