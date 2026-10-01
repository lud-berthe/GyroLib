#include "declared_view.hpp"
#include <gyrolib/gyrolib.hpp>
#include <gyrolib/sdl.h>
#include <SDL3/SDL.h>
#include "../src/sdl_identity.hpp"
#include "sdl_contact_fixture.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#define REQUIRE(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x " "<<SDL_GetError()<<'\n';return 1;}}while(0)
static bool SDLCALL enable(void*,bool){return true;}
static void GL_CALL count_camera(void* user,double,double){++*static_cast<unsigned*>(user);}
static unsigned feedback_count{};static bool feedback_packet_ok=true;
static bool SDLCALL feedback_effect(void*,const void* bytes,int size){
    const unsigned char expected[]={0x81,0,0x90,1,0,0,1,0};
    feedback_packet_ok&=size==sizeof(expected)&&std::memcmp(bytes,expected,sizeof(expected))==0;
    ++feedback_count;return feedback_packet_ok;
}
int main(){
    REQUIRE(!gl_sdl_create(nullptr,0));
    REQUIRE(std::strstr(gl_sdl_error(nullptr),"context"));
    // Inject delayed SDL Steam metadata into the production association helper.
    // The chosen pad follows its exact handle, and a host override is retained.
    {
        gyrolib::Context identity_context;gl_endpoint e{};
        e.id=901;e.physical_id=9001;e.connected=1;e.source=GL_SOURCE_SDL;
        REQUIRE(gl_register_endpoint(identity_context.get(),&e)==GL_OK);
        REQUIRE(gl_select_device(identity_context.get(),9001)==GL_OK);
        gyrolib_sdl_detail::refresh_steam_identity(identity_context.get(),e,9001,0,7001);
        REQUIRE(e.physical_id==7001);REQUIRE(gl_get_selected_device(identity_context.get())==7001);
        gyrolib_sdl_detail::refresh_steam_identity(identity_context.get(),e,9001,7001,0);
        REQUIRE(e.physical_id==9001);REQUIRE(gl_get_selected_device(identity_context.get())==9001);
        REQUIRE(gl_associate_endpoint(identity_context.get(),e.id,8001)==GL_OK);e.physical_id=8001;
        gyrolib_sdl_detail::refresh_steam_identity(identity_context.get(),e,9001,0,7002);
        REQUIRE(e.physical_id==8001);REQUIRE(gl_get_selected_device(identity_context.get())==8001);
    }
    REQUIRE(SDL_Init(SDL_INIT_GAMEPAD));
    gyrolib::Context c;REQUIRE(declare_test_view(c.get()));auto* reader=gl_sdl_create(c.get(),1);REQUIRE(reader);
    uint64_t now=1000000000;REQUIRE(gl_sdl_poll(reader,now)==GL_OK);
    SDL_VirtualJoystickSensorDesc sensors[]={{SDL_SENSOR_ACCEL,100},{SDL_SENSOR_GYRO,100}};
    SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=6;desc.nbuttons=21;desc.button_mask=(1u<<21)-1;
    desc.axis_mask=(1u<<6)-1;desc.nsensors=2;desc.sensors=sensors;desc.SetSensorsEnabled=enable;desc.name="GyroLib virtual test";
    auto id=SDL_AttachVirtualJoystick(&desc);REQUIRE(id);
    auto* joy=SDL_OpenJoystick(id);REQUIRE(joy);
    // A pad arriving inside the periodic scan interval is discovered on this poll.
    now+=10000000;SDL_PumpEvents();REQUIRE(gl_sdl_poll(reader,now)==GL_OK);
    auto endpoint=gl_sdl_endpoint_for_instance(reader,id);REQUIRE(endpoint);
    gl_endpoint info{};bool found=false;
    for(uint32_t i=0;i<gl_endpoint_count(c.get());++i){gl_get_endpoint(c.get(),i,&info);if(info.id==endpoint){found=true;break;}}
    REQUIRE(found);REQUIRE(gl_select_device(c.get(),info.physical_id)==GL_OK);REQUIRE(info.caps.gyro&&info.caps.accelerometer);
    gl_setting_set(c.get(),"context.1.sensitivity_x",1);gl_setting_set(c.get(),"context.1.gyro.space",GL_SPACE_LOCAL_YAW);
    gl_host_state host{};host.focused=host.camera_allowed=1;gl_output out{};
    out.yaw_degrees=5;
    REQUIRE(gl_sdl_update(nullptr,now,&host,&out)==GL_INVALID&&out.yaw_degrees==0);
    out.yaw_degrees=5;
    REQUIRE(gl_sdl_update(reader,0,&host,&out)==GL_INVALID&&out.yaw_degrees==0);
    REQUIRE(gl_sdl_update(reader,now,nullptr,&out)==GL_INVALID);
    REQUIRE(gl_sdl_update(reader,now,&host,nullptr)==GL_INVALID);
    unsigned camera_calls=0;gl_set_camera_callback(c.get(),count_camera,&camera_calls);
    float a[]={0,SDL_STANDARD_GRAVITY,0},g[]={0,1,0};
    for(int i=0;i<20;++i){
        now+=10000000;
        REQUIRE(SDL_SendJoystickVirtualSensorData(joy,SDL_SENSOR_ACCEL,now,a,3));
        REQUIRE(SDL_SendJoystickVirtualSensorData(joy,SDL_SENSOR_GYRO,now,g,3));
        REQUIRE(gl_set_gameplay_context_state(c.get(),1,1,1)==GL_OK);
        const auto before=camera_calls;
        REQUIRE(gl_sdl_update(reader,now,&host,&out)==GL_OK);
        REQUIRE(camera_calls-before==(out.yaw_degrees!=0||out.pitch_degrees!=0?1u:0u));
    }
    REQUIRE(camera_calls>0);gl_set_camera_callback(c.get(),nullptr,nullptr);
    REQUIRE(out.source==GL_SOURCE_SDL);REQUIRE(std::abs(out.yaw_degrees+180/3.141592653589793*.01)<.001);
    REQUIRE(gl_sdl_poll(reader,now-1)==GL_INVALID);
    out.yaw_degrees=5;
    REQUIRE(gl_sdl_update(reader,now-1,&host,&out)==GL_INVALID&&out.yaw_degrees==0);
    // Provider metadata refresh must preserve an explicit physical association.
    REQUIRE(gl_associate_endpoint(c.get(),endpoint,0x123456789ull)==GL_OK);
    for(unsigned n=0;n<2;++n){
        now+=n?500000001:10000000;REQUIRE(gl_sdl_poll(reader,now)==GL_OK);
        for(uint32_t i=0;i<gl_endpoint_count(c.get());++i){gl_get_endpoint(c.get(),i,&info);if(info.id==endpoint)break;}
        REQUIRE(info.physical_id==0x123456789ull);
    }
    REQUIRE(SDL_SetJoystickVirtualAxis(joy,4,32767));SDL_PumpEvents();now+=10000000;
    REQUIRE(gl_sdl_poll(reader,now)==GL_OK);REQUIRE(update_test_view(c.get(),now,&host,&out)==GL_OK);
    gl_trigger_input triggers{};REQUIRE(gl_get_trigger_input(c.get(),&triggers)==GL_OK);
    REQUIRE(triggers.available==3&&std::abs(triggers.left-1)<.001);
    // The adapter watched the queue, it did not consume host controller events.
    REQUIRE(SDL_HasEvent(SDL_EVENT_GAMEPAD_SENSOR_UPDATE));
    SDL_Event marker{};marker.type=SDL_EVENT_USER;REQUIRE(SDL_PushEvent(&marker));
    gl_sdl_poll(reader,now+10000000);REQUIRE(SDL_HasEvent(SDL_EVENT_USER));
    REQUIRE(SDL_DetachVirtualJoystick(id));SDL_PumpEvents();now+=20000000;gl_sdl_poll(reader,now);
    update_test_view(c.get(),now,&host,&out);REQUIRE(out.source==GL_SOURCE_NONE);
    SDL_CloseJoystick(joy);
    // A genuine gyro-only capability is usable in a local space. SDL must not
    // wait for an accelerometer event that this device can never produce.
    auto gyro_only_desc=desc;gyro_only_desc.nsensors=1;gyro_only_desc.sensors=sensors+1;
    auto gyro_only=SDL_AttachVirtualJoystick(&gyro_only_desc);REQUIRE(gyro_only);
    auto* gyro_joy=SDL_OpenJoystick(gyro_only);REQUIRE(gyro_joy);
    now+=10000000;SDL_PumpEvents();REQUIRE(gl_sdl_poll(reader,now)==GL_OK);
    const auto gyro_endpoint=gl_sdl_endpoint_for_instance(reader,gyro_only);REQUIRE(gyro_endpoint);
    for(uint32_t n=0;n<gl_endpoint_count(c.get());++n){gl_get_endpoint(c.get(),n,&info);if(info.id==gyro_endpoint)break;}
    REQUIRE(info.caps.gyro&&!info.caps.accelerometer);REQUIRE(gl_select_device(c.get(),info.physical_id)==GL_OK);
    for(int n=0;n<10;++n){now+=10000000;REQUIRE(SDL_SendJoystickVirtualSensorData(gyro_joy,SDL_SENSOR_GYRO,now,g,3));
        SDL_PumpEvents();REQUIRE(gl_sdl_poll(reader,now)==GL_OK);REQUIRE(update_test_view(c.get(),now,&host,&out)==GL_OK);}
    REQUIRE(out.source==GL_SOURCE_SDL&&std::abs(out.yaw_degrees)>.5);
    REQUIRE(SDL_DetachVirtualJoystick(gyro_only));SDL_PumpEvents();gl_sdl_poll(reader,++now);SDL_CloseJoystick(gyro_joy);
    // Exercise the real SDL identity/label API with virtual hardware descriptors.
    // Names are intentionally identical; VID/PID/type metadata must decide labels.
    struct LabelCase {Uint16 vendor,product;SDL_GamepadType type;const char *south,*back,*shoulder;};
    const LabelCase labels[]={
        {0x054c,0x0ce6,SDL_GAMEPAD_TYPE_PS5,"Cross","Create","L1"},
        {0x054c,0x05c4,SDL_GAMEPAD_TYPE_PS4,"Cross","Share","L1"},
        {0x045e,0x02ea,SDL_GAMEPAD_TYPE_XBOXONE,"A","View","LB"},
        {0x045e,0x028e,SDL_GAMEPAD_TYPE_XBOX360,"A","Back","LB"},
        {0x057e,0x2009,SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO,"B","-","L"},
        {0x054c,0x0df2,SDL_GAMEPAD_TYPE_PS5,"Cross","Create","L1"}
    };
    for(const auto& expected:labels) {
        auto labelled_desc=desc;labelled_desc.vendor_id=expected.vendor;labelled_desc.product_id=expected.product;
        auto labelled=SDL_AttachVirtualJoystick(&labelled_desc);REQUIRE(labelled);
        REQUIRE(SDL_GetRealGamepadTypeForID(labelled)==expected.type);
        now+=10000000;SDL_PumpEvents();REQUIRE(gl_sdl_poll(reader,now)==GL_OK);
        auto eid=gl_sdl_endpoint_for_instance(reader,labelled);REQUIRE(eid);
        bool matched=false;
        for(uint32_t n=0;n<gl_endpoint_count(c.get());++n){gl_get_endpoint(c.get(),n,&info);if(info.id==eid){matched=true;break;}}
        REQUIRE(matched);REQUIRE(gl_select_device(c.get(),info.physical_id)==GL_OK);
        REQUIRE(update_test_view(c.get(),now,&host,&out)==GL_OK);
        REQUIRE(std::strcmp(gl_get_button_label(c.get(),0),expected.south)==0);
        REQUIRE(std::strcmp(gl_get_button_label(c.get(),4),expected.back)==0);
        REQUIRE(std::strcmp(gl_get_button_label(c.get(),9),expected.shoulder)==0);
        if(expected.product==0x0df2){REQUIRE(std::strcmp(gl_get_button_label(c.get(),16),"RB")==0);REQUIRE(std::strcmp(gl_get_button_label(c.get(),19),"Left Fn")==0);}
        REQUIRE(SDL_DetachVirtualJoystick(labelled));SDL_PumpEvents();gl_sdl_poll(reader,++now);
    }
    // Genuine touch contacts, clicks and analogue tilt are independent. Test
    // USB/puck IDs and reject a same-name unknown pad or a generic mapping.
    for(Uint16 product:{Uint16(0x1302),Uint16(0x1304),Uint16(0x1305),Uint16(0xffff)}){
        auto touch_desc=contact_fixture_desc(0x28de,product);touch_desc.SetSensorsEnabled=enable;touch_desc.SendEffect=feedback_effect;
        auto touch_id=SDL_AttachVirtualJoystick(&touch_desc);REQUIRE(touch_id);
        auto* j=SDL_OpenJoystick(touch_id);REQUIRE(j);REQUIRE(contact_fixture_mapping(touch_id));
        auto step=[&](){now+=10000000;
            SDL_SendJoystickVirtualSensorData(j,SDL_SENSOR_ACCEL,now,a,3);
            SDL_SendJoystickVirtualSensorData(j,SDL_SENSOR_GYRO,now,g,3);
            SDL_PumpEvents();gl_sdl_poll(reader,now);update_test_view(c.get(),now,&host,&out);
        };
        step();auto eid=gl_sdl_endpoint_for_instance(reader,touch_id);REQUIRE(eid);
        for(uint32_t n=0;n<gl_endpoint_count(c.get());++n){gl_get_endpoint(c.get(),n,&info);if(info.id==eid)break;}
        REQUIRE(gl_select_device(c.get(),info.physical_id)==GL_OK);
        gl_setting_set(c.get(),"context.1.gyro.activation",GL_HOLD);gl_setting_set(c.get(),"context.1.activation.button",0);
        step();step();
        gl_set_host_capabilities(c.get(),GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION);
        gl_setting_set(c.get(),"context.1.flick.mode",GL_FLICK_TOUCHPAD);step();
        SDL_SetJoystickVirtualTouchpad(j,1,0,true,.75f,.5f,0);step();
        const auto before_feedback=feedback_count;
        REQUIRE(gl_sdl_apply_feedback(reader)==(product==0xffff?GL_UNAVAILABLE:GL_OK));
        REQUIRE(gl_sdl_apply_feedback(reader)==GL_OK); // one dispatch per update
        REQUIRE(feedback_count==before_feedback+(product==0xffff?0:1)&&feedback_packet_ok);
        SDL_SetJoystickVirtualTouchpad(j,1,0,true,.5f,.25f,0);step();gl_sdl_apply_feedback(reader);
        REQUIRE(feedback_count==before_feedback+(product==0xffff?0:1)); // 20 Hz rate limit
        gl_setting_set(c.get(),"context.1.flick.mode",GL_FLICK_OFF);step();gl_sdl_apply_feedback(reader);
        REQUIRE(feedback_count==before_feedback+(product==0xffff?0:1));
        SDL_SetJoystickVirtualTouchpad(j,1,0,false,.5f,.25f,0);step();
        if(product==0xffff){REQUIRE(!info.caps.stick_touch&&!info.caps.grip_touch);}
        else {
            REQUIRE(info.caps.stick_touch==3&&info.caps.grip_touch==3&&info.caps.touchpads==3);
            REQUIRE(std::strcmp(gl_get_button_label(c.get(),25),"Grip Sense R")==0);
            // Driver strings can change without a mapping event. Cached strings
            // remain owned, and the scan fallback publishes the new metadata.
            const auto properties=SDL_GetJoystickProperties(j);
            REQUIRE(SDL_SetStringProperty(properties,"gyrolib.controls.button.20.name","Updated right grip"));
            step();REQUIRE(std::strcmp(gl_get_button_label(c.get(),25),"Grip Sense R")==0);
            now+=500000001;step();REQUIRE(std::strcmp(gl_get_button_label(c.get(),25),"Updated right grip")==0);
            REQUIRE(std::strcmp(gl_get_button_label(c.get(),17),"L4")==0);
            gl_choice choice{};
            for(uint32_t value=23;value<=26;++value){REQUIRE(gl_choice_at(c.get(),"context.1.activation.button",value,&choice)==GL_OK);REQUIRE(!choice.available);}
            for(uint32_t value:{8u,9u,21u,22u}){REQUIRE(gl_choice_at(c.get(),"context.1.activation.button",value,&choice)==GL_OK);REQUIRE(choice.available);}
            gl_flick_input flick{};REQUIRE(gl_get_flick_input(c.get(),&flick)==GL_OK);REQUIRE(flick.available==3&&!flick.touching);
            SDL_SetJoystickVirtualTouchpad(j,0,0,true,1,.5f,0);step();gl_get_flick_input(c.get(),&flick);REQUIRE(!flick.touching);
            SDL_SetJoystickVirtualTouchpad(j,0,0,false,1,.5f,0);
            SDL_SetJoystickVirtualTouchpad(j,1,0,true,1,0,0);step();gl_get_flick_input(c.get(),&flick);
            REQUIRE(flick.touching&&flick.touchpad_x==1&&flick.touchpad_y==1);
            SDL_SetJoystickVirtualTouchpad(j,1,0,false,1,0,0);step();gl_get_flick_input(c.get(),&flick);REQUIRE(!flick.touching);
            struct Contact {const char* setting;int left,right;};
            for(const auto& contact:{Contact{"context.1.activation.stick_touch",19,18},Contact{"context.1.activation.grip_touch",21,20},Contact{"context.1.activation.touchpad",-1,-2}}){
                auto down=[&](int index,bool held){return index>=0?SDL_SetJoystickVirtualButton(j,index,held):SDL_SetJoystickVirtualTouchpad(j,-index-1,0,held,.5f,.5f,0);};
                gl_setting_set(c.get(),contact.setting,GL_SIDE_LEFT);step();REQUIRE(!out.gyro_active);
                REQUIRE(down(contact.right,true));step();REQUIRE(!out.gyro_active);
                REQUIRE(down(contact.left,true));step();REQUIRE(out.gyro_active);
                gl_setting_set(c.get(),contact.setting,GL_SIDE_BOTH);step();REQUIRE(out.gyro_active);
                REQUIRE(down(contact.left,false));step();REQUIRE(!out.gyro_active);
                gl_setting_set(c.get(),contact.setting,GL_SIDE_RIGHT);step();REQUIRE(out.gyro_active);
                REQUIRE(down(contact.right,false));step();REQUIRE(!out.gyro_active);
                // Stick clicks and tilt cannot substitute for capacitive touch.
                SDL_SetJoystickVirtualButton(j,7,true);SDL_SetJoystickVirtualButton(j,8,true);
                SDL_SetJoystickVirtualAxis(j,0,32767);step();REQUIRE(!out.gyro_active);
                SDL_SetJoystickVirtualButton(j,7,false);SDL_SetJoystickVirtualButton(j,8,false);SDL_SetJoystickVirtualAxis(j,0,0);
                gl_setting_set(c.get(),contact.setting,GL_SIDE_OFF);
            }
            // Do not infer contacts when remapped to unrelated raw buttons.
            REQUIRE(SDL_SetGamepadMapping(touch_id,"a:b0,misc3:b2,misc4:b3,misc5:b4,misc6:b5,"));step();
            for(uint32_t n=0;n<gl_endpoint_count(c.get());++n){gl_get_endpoint(c.get(),n,&info);if(info.id==eid)break;}
            REQUIRE(!info.caps.stick_touch&&!info.caps.grip_touch);
        }
        SDL_DetachVirtualJoystick(touch_id);SDL_CloseJoystick(j);SDL_PumpEvents();gl_sdl_poll(reader,++now);
    }
    // Repeated reconnection does not exhaust the bounded core endpoint registry.
    for(int n=0;n<40;++n){
        auto reconnect=SDL_AttachVirtualJoystick(&desc);REQUIRE(reconnect);
        now+=10000000;SDL_PumpEvents();REQUIRE(gl_sdl_poll(reader,now)==GL_OK);
        REQUIRE(gl_sdl_endpoint_for_instance(reader,reconnect));
        REQUIRE(SDL_DetachVirtualJoystick(reconnect));SDL_PumpEvents();gl_sdl_poll(reader,++now);
    }
    // A host-opened SDL handle has shared sensor state. Observing it must not
    // turn sensors on, and destroying our reader must not turn them off later.
    auto shared=SDL_AttachVirtualJoystick(&desc);REQUIRE(shared);
    auto* host_pad=SDL_OpenGamepad(shared);REQUIRE(host_pad);
    REQUIRE(!SDL_GamepadSensorEnabled(host_pad,SDL_SENSOR_GYRO));
    now+=600000000;SDL_PumpEvents();gl_sdl_poll(reader,now);
    REQUIRE(!SDL_GamepadSensorEnabled(host_pad,SDL_SENSOR_GYRO));
    REQUIRE(SDL_SetGamepadSensorEnabled(host_pad,SDL_SENSOR_GYRO,true));
    REQUIRE(SDL_SetGamepadSensorEnabled(host_pad,SDL_SENSOR_ACCEL,true));
    gl_sdl_destroy(reader);REQUIRE(SDL_GamepadSensorEnabled(host_pad,SDL_SENSOR_GYRO));
    SDL_CloseGamepad(host_pad);SDL_DetachVirtualJoystick(shared);
    REQUIRE(SDL_WasInit(SDL_INIT_GAMEPAD)&SDL_INIT_GAMEPAD); // borrowed subsystem retained
    SDL_Quit();REQUIRE(!gl_sdl_create(c.get(),1));
    REQUIRE(std::strstr(gl_sdl_error(nullptr),"not initialized"));
    reader=gl_sdl_create(c.get(),0);REQUIRE(reader);gl_sdl_destroy(reader);
    REQUIRE(!(SDL_WasInit(SDL_INIT_GAMEPAD)&SDL_INIT_GAMEPAD));
    std::cout<<"SDL virtual sensors, labels, contacts/clicks/tilt, remapping, host events, ownership and 40 reconnects passed.\n";return 0;
}

