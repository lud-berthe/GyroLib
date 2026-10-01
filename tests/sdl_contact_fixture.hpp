#pragma once
#include <SDL3/SDL.h>
#include "../third_party/SDL/gyrolib_controls.h"
// Reproduce SDL 3.4's physical Triton layout, including the raw joystick
// contact indices. All data still traverses the real SDL gamepad API.
inline bool contact_fixture_mapping(SDL_JoystickID id){
    const bool mapped=SDL_SetGamepadMapping(id,"a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,"
        "leftshoulder:b9,rightshoulder:b10,misc1:b11,paddle1:b12,paddle2:b13,paddle3:b14,paddle4:b15,"
        "touchpad:b17,misc2:b16,misc3:b19,misc4:b18,misc5:b21,misc6:b20,"
        "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,");
    auto* joy=SDL_GetJoystickFromID(id);const auto product=SDL_GetJoystickProductForID(id);
    if(joy&&SDL_GetJoystickVendorForID(id)==0x28de&&(product==0x1302||product==0x1304||product==0x1305)){
        GyroLibControlTopology(joy,3);
        GyroLibButtonOrigin(joy,19,"L3 touch",GL_LEFT,0);GyroLibButtonOrigin(joy,18,"R3 touch",GL_RIGHT,0);
        GyroLibButtonOrigin(joy,21,"Grip Sense L",0,GL_LEFT);GyroLibButtonOrigin(joy,20,"Grip Sense R",0,GL_RIGHT);
        GyroLibButtonOrigin(joy,4,"Menu",0,0);GyroLibButtonOrigin(joy,6,"View",0,0);
        GyroLibButtonOrigin(joy,13,"L4",0,0);GyroLibButtonOrigin(joy,12,"R4",0,0);
        GyroLibButtonOrigin(joy,16,"Right touchpad click",0,0);GyroLibButtonOrigin(joy,17,"Left touchpad click",0,0);
        SDL_SetBooleanProperty(SDL_GetJoystickProperties(joy),"gyrolib.output.right_pad_pulse",true);
    }
    return mapped;
}
inline SDL_VirtualJoystickDesc contact_fixture_desc(Uint16 vendor=0x28de,Uint16 product=0x1304){
    static const SDL_VirtualJoystickTouchpadDesc pads[]={{1,{}},{1,{}}};
    static const SDL_VirtualJoystickSensorDesc sensors[]={{SDL_SENSOR_ACCEL,250},{SDL_SENSOR_GYRO,250}};
    SDL_VirtualJoystickDesc d{};SDL_INIT_INTERFACE(&d);d.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    d.vendor_id=vendor;d.product_id=product;d.name="Contact fixture";
    d.naxes=6;d.nbuttons=26;d.axis_mask=63;d.button_mask=(1u<<26)-1;
    d.ntouchpads=2;d.touchpads=pads;d.nsensors=2;d.sensors=sensors;return d;
}
