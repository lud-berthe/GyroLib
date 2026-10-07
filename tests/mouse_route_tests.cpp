#include "../src/detail/internal.hpp"
#include "../src/detail/mouse_route.hpp"
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
static void near(double x,double y){CHECK(std::abs(x-y)<1e-6);}
int main()try{
    using gyrolib_detail::MouseRoute;
    MouseRoute route;double yaw=0,pitch=0;
    auto update=[&](uint64_t t,uint32_t mode=1,bool safe=true,uint32_t view=1,uint64_t device=1,bool touch=true,bool observe=true,bool steam_source=false){
        route.consume(t,safe,view,device,touch,mode,observe,steam_source,yaw,pitch);
    };
    update(100);
    CHECK(!route.raw(101,65763,false,0,20,-10));
    CHECK(!route.raw(101,0,true,0,20,-10));CHECK(!route.raw(101,0,false,1,20,0));
    CHECK(!route.raw(101,0,false,0,20,0));CHECK(!route.raw(102,0,false,0,20,0));
    CHECK(!route.raw(103,0,false,0,20,0));CHECK(!route.detected); // raw alone is not proof
    CHECK(route.injected(104));CHECK(route.detected); // corroborated, default Block
    CHECK(route.raw(105,0,false,0,20,-10));update(110);near(yaw,0);near(pitch,0);
    update(120,0);CHECK(!route.raw(121,0,false,0,20,0));CHECK(!route.injected(122));update(130,0);near(yaw,0);
    update(140,2);CHECK(route.raw(141,0,false,0,20,-10));update(150,2);near(yaw,1);near(pitch,.5);
    update(160,2);near(yaw,1); // consume once
    CHECK(route.raw(161,0,false,0,20,0));update(170,1);near(yaw,1); // drop pending conversion on Block
    update(180,2);CHECK(route.raw(181,0,false,0,20,0));update(190,0);near(yaw,1); // and Pass through
    update(200,2);CHECK(route.raw(201,0,false,0,20,0));update(210,2,true,2);near(yaw,1); // view transition
    CHECK(route.raw(211,0,false,0,20,0));update(220,2,false,2);near(yaw,1);CHECK(route.armed(221));CHECK(route.raw(222,0,false,0,20,0));
    update(230,2,true,2);CHECK(route.raw(231,0,false,0,20,0));update(240,2,true,2,1,false);near(yaw,2);
    CHECK(route.raw(241,0,false,0,20,0));update(250,2,true,2,1,false);near(yaw,3); // inertia
    update(410,2,true,2,1,false);CHECK(!route.armed(411)); // inactivity
    update(420,2);CHECK(!route.raw(521,0,false,0,20,0)); // stale host update
    CHECK(!route.raw(421,0,false,0,8193,0));
    CHECK(route.raw(421,0,false,0,20,0));update(430,2,true,1,2);near(yaw,3);CHECK(!route.detected);
    CHECK(!route.injected(431));CHECK(!route.raw(432,0,false,0,20,0));CHECK(!route.raw(433,0,false,0,20,0));
    CHECK(route.raw(434,0,false,0,20,0));update(440,2,true,1,2,true,true,false);near(yaw,4);
    CHECK(route.raw(441,0,false,0,20,0));update(450,2,true,1,2,true,true,false);near(yaw,5);
    update(460,2,true,1,2,true,false);CHECK(!route.raw(461,0,false,0,20,0)); // focus loss
    MouseRoute no_contact;no_contact.consume(100,true,1,1,false,2,true,false,yaw,pitch);
    CHECK(!no_contact.injected(101));for(int i=0;i<5;++i)CHECK(!no_contact.raw(102+i,0,false,0,1,1));CHECK(!no_contact.detected);
    MouseRoute uncorrelated;uncorrelated.consume(100,true,1,1,true,1,true,false,yaw,pitch);
    CHECK(!uncorrelated.injected(101));uncorrelated.consume(300,true,1,1,true,1,true,false,yaw,pitch);
    for(int i=0;i<4;++i)CHECK(!uncorrelated.raw(301+i,0,false,0,1,1));CHECK(!uncorrelated.detected);

    // Steam joystick/gyro/button mouse mappings have no pad-contact signal.
    // Corroborate their stream, release active routing on inactivity, and
    // resume without ever touching a pad. All policies keep physical HID input.
    for(uint32_t mode:{0u,1u,2u}){
        MouseRoute generic;double y=0,p=0;
        generic.consume(100,true,1,7,false,mode,true,true,y,p);
        CHECK(!generic.injected(101));
        CHECK(!generic.raw(102,0,false,0,20,-10));
        CHECK(!generic.raw(103,0,false,0,20,-10));
        CHECK(generic.raw(104,0,false,0,20,-10)==(mode!=0));
        CHECK(generic.detected);
        CHECK(!generic.raw(105,9876,false,0,20,-10));
        CHECK(!generic.raw(105,0,false,1,20,-10));
        generic.consume(110,true,1,7,false,mode,true,true,y,p);
        near(y,mode==2?1:0);near(p,mode==2?.5:0);
        generic.consume(300,true,1,7,false,mode,true,true,y,p);
        CHECK(!generic.active(301)); // no recent active stream while idle
        CHECK(generic.raw(302,0,false,0,20,-10)==(mode!=0));
        CHECK(generic.active(303)==(mode!=0));
        generic.consume(310,true,1,7,false,mode,true,true,y,p);
        near(y,mode==2?2:0);
        generic.consume(320,true,1,8,false,mode,true,true,y,p);
        CHECK(!generic.detected);CHECK(!generic.raw(321,0,false,0,20,0));
    }

    // Blocking applies in a known menu even with camera output disallowed.
    MouseRoute menu;double menu_y=0,menu_p=0;
    menu.consume(100,true,1,7,false,0,true,true,menu_y,menu_p);
    menu.injected(101);for(int i=0;i<3;++i)menu.raw(102+i,0,false,0,20,0);
    CHECK(menu.detected);
    menu.consume(110,false,2,7,false,1,true,true,menu_y,menu_p);
    CHECK(menu.raw(111,0,false,0,20,0));CHECK(menu.injected(112));
    CHECK(!menu.raw(113,1234,false,0,20,0));
    menu.consume(120,false,2,7,false,1,true,true,menu_y,menu_p);
    near(menu_y,0);near(menu_p,0);
    // Convert swallows motion while output is unavailable but queues nothing
    // to replay later. Pass-through, focus loss and the library panel stay free.
    menu.consume(130,false,2,7,false,2,true,true,menu_y,menu_p);
    CHECK(menu.raw(131,0,false,0,20,10));
    menu.consume(140,true,2,7,false,2,true,true,menu_y,menu_p);
    near(menu_y,0);near(menu_p,0);
    menu.consume(150,false,2,7,false,0,true,true,menu_y,menu_p);
    CHECK(!menu.raw(151,0,false,0,20,10));
    menu.consume(160,false,2,7,false,1,true,true,menu_y,menu_p,false);
    CHECK(!menu.raw(161,0,false,0,20,10));
    menu.consume(170,false,2,7,false,1,false,true,menu_y,menu_p);
    CHECK(!menu.raw(171,0,false,0,20,10));
    menu.consume(180,false,0,7,false,1,true,true,menu_y,menu_p);
    CHECK(!menu.raw(181,0,false,0,20,10)); // no reported active view

    auto* c=gl_create(GL_ABI_VERSION);CHECK(c);
    struct State {MouseRoute route;gl_context* context{};uint64_t now=1000;double callback_yaw{},callback_pitch{};} state;
    state.context=c;c->virtual_mouse_user=&state;
    c->virtual_mouse=[](void* p,gl_output* output,bool safe,uint32_t view,uint32_t contacts){
        auto& s=*static_cast<State*>(p);auto* c=s.context;double mode=1;const auto key="context."+std::to_string(view)+".input.steam_mouse";gl_setting_get_effective(c,key.c_str(),&mode);
        s.route.consume(s.now,safe,view,c->selected,contacts&GL_RIGHT,static_cast<uint32_t>(mode),
            c->host.focused,c->steam_input_available(),output->yaw_degrees,output->pitch_degrees,!c->panel);
        c->steam_mouse_device=s.route.detected?c->selected:0;
    };
    gl_set_camera_callback(c,[](void* p,double y,double x){auto& s=*static_cast<State*>(p);s.callback_yaw+=y;s.callback_pitch+=x;},&state);
    for(uint32_t id:{1,2}){gl_gameplay_context view{id,"Camera","",0};CHECK(gl_register_gameplay_context(c,&view)==GL_OK);}
    gl_endpoint ep{};ep.id=ep.physical_id=1;ep.source=GL_SOURCE_SDL;ep.connected=1;ep.caps.gyro=ep.caps.accelerometer=1;ep.caps.touchpads=GL_RIGHT;
    CHECK(gl_register_endpoint(c,&ep)==GL_OK);gl_host_state host{};host.focused=host.camera_allowed=1;gl_output output{};
    auto set=[&](const char* key,double value){CHECK(gl_setting_set(c,key,value)==GL_OK);};
    auto get=[&](const char* key){double v=-1;CHECK(gl_setting_get(c,key,&v)==GL_OK);return v;};
    const char* key="context.1.input.steam_mouse";
    auto metadata=[&]{for(uint32_t i=0;i<gl_menu_setting_count(c);++i){gl_setting_info s{};CHECK(gl_setting_at(c,i,&s)==GL_OK);if(!std::strcmp(s.id,key))return s;}throw std::runtime_error("metadata");};
    CHECK(!metadata().visible);near(get(key),1);CHECK(gl_setting_set(c,key,3)==GL_INVALID);
    set("context.1.gyro.space",GL_SPACE_LOCAL_YAW);set("context.1.sensitivity_x",1);
    uint32_t view=1;
    auto tick=[&]{state.now+=10;const auto ns=state.now*1000000;
        gl_sample sample{ns,ns,{0,-100,0},{0,1,0}};CHECK(gl_submit_sample(c,1,&sample)==GL_OK);
        gl_controls controls{};controls.timestamp_ns=ns;controls.touchpads=ep.caps.touchpads;CHECK(gl_submit_controls(c,1,&controls)==GL_OK);
        for(uint32_t id:{1,2})gl_set_gameplay_context_state(c,id,id==view,1);
        CHECK(gl_update(c,ns,&host,&output)==GL_OK);
    };
    for(int i=0;i<8;++i)tick();near(output.yaw_degrees,1);
    CHECK(!metadata().visible); // no prior mouse movement
    CHECK(gl_set_endpoint_pairing_hint(c,1,0,0,1)==GL_OK);CHECK(!metadata().visible); // generic virtual is not Steam
    CHECK(gl_set_endpoint_steam_input(c,1,2)==GL_INVALID);CHECK(gl_set_endpoint_steam_input(c,999,1)==GL_INVALID);
    CHECK(gl_set_endpoint_steam_input(c,1,1)==GL_OK);CHECK(metadata().visible); // visible before touching/moving the mouse
    CHECK(!state.route.detected);CHECK(!state.route.armed(state.now)); // visibility does not authorize interception
    CHECK(gl_set_endpoint_steam_input(c,1,0)==GL_OK);CHECK(!metadata().visible);
    gl_endpoint other=ep;other.id=other.physical_id=9;other.source=GL_SOURCE_STEAM;
    CHECK(gl_register_endpoint(c,&other)==GL_OK);CHECK(!metadata().visible); // a different controller cannot expose it
    CHECK(gl_forget_endpoint(c,9)==GL_OK);
    other.physical_id=1;CHECK(gl_register_endpoint(c,&other)==GL_OK);CHECK(metadata().visible);
    CHECK(gl_forget_endpoint(c,9)==GL_OK);CHECK(!metadata().visible);
    CHECK(!state.route.injected(state.now+1));
    CHECK(!state.route.raw(state.now+2,0,false,0,20,0));CHECK(!state.route.raw(state.now+3,0,false,0,20,0));
    CHECK(state.route.raw(state.now+4,0,false,0,20,0));tick();CHECK(metadata().visible);near(output.yaw_degrees,1); // Block keeps gyro
    gl_setting_info last{};CHECK(gl_menu_tab_setting_at(c,2,gl_menu_tab_setting_count(c,2)-1,&last)==GL_OK);CHECK(!std::strcmp(last.id,key));
    for(const char* lang:{"en","fr","de","es","it","pt"}){CHECK(gl_set_language(c,lang)==GL_OK);
        CHECK(std::strcmp(metadata().label,"?")!=0);CHECK(gl_choice_count(key)==3);
        for(unsigned i=0;i<3;++i){gl_choice choice{};CHECK(gl_choice_at(c,key,i,&choice)==GL_OK);CHECK(choice.available);CHECK(std::strcmp(choice.label,"?")!=0);CHECK(std::strlen(gl_choice_description(c,key,i))>5);}
    }
    set(key,2);tick();CHECK(state.route.raw(state.now+1,0,false,0,20,-10));state.callback_yaw=state.callback_pitch=0;tick();
    near(output.yaw_degrees,2);near(output.pitch_degrees,.5);near(state.callback_yaw,2);near(state.callback_pitch,.5);
    tick();near(output.yaw_degrees,1);
    set(key,0);tick();CHECK(!state.route.raw(state.now+1,0,false,0,20,0));tick();near(output.yaw_degrees,1);
    // A Steam-managed controller needs neither touchpad hardware nor contact.
    ep.caps.touchpads=0;CHECK(gl_register_endpoint(c,&ep)==GL_OK);
    CHECK(gl_set_endpoint_steam_input(c,1,1)==GL_OK);CHECK(c->steam_input_available());CHECK(metadata().visible);
    // Flick on another input must not discard this indistinguishable mouse stream.
    set("context.1.flick.mode",GL_FLICK_TOUCHPAD);
    set(key,2);tick();CHECK(state.route.raw(state.now+1,0,false,0,20,-10));tick();
    near(output.yaw_degrees,2);near(output.pitch_degrees,.5);
    set("context.1.flick.mode",GL_FLICK_OFF);
    // Inheritance, explicit override, reset and save/load all use the shared model.
    set(key,2);CHECK(gl_set_context_parent(c,2,1)==GL_OK);near(get("context.2.input.steam_mouse"),2);
    set("context.2.input.steam_mouse",0);near(get(key),2);near(get("context.2.input.steam_mouse"),0);
    CHECK(gl_setting_inherit(c,"context.2.input.steam_mouse")==GL_OK);near(get("context.2.input.steam_mouse"),2);
    const auto path=std::filesystem::temp_directory_path()/"gyrolib-mouse-settings-test.ini";
    CHECK(gl_save_settings(c,path.string().c_str())==GL_OK);
    auto* restored=gl_create(GL_ABI_VERSION);for(uint32_t id:{1,2}){gl_gameplay_context v{id,"View","",0};CHECK(gl_register_gameplay_context(restored,&v)==GL_OK);}
    CHECK(gl_load_settings(restored,path.string().c_str())==GL_OK);double value=0;CHECK(gl_setting_get(restored,"context.2.input.steam_mouse",&value)==GL_OK);near(value,2);CHECK(restored->steam_mouse_device==0);
    gl_reset_settings(restored);CHECK(gl_setting_get(restored,key,&value)==GL_OK);near(value,1);
    gl_destroy(restored);std::filesystem::remove(path);
    set("context.1.gyro.activation",GL_GYRO_OFF);tick();CHECK(metadata().visible);CHECK(state.route.raw(state.now+1,0,false,0,20,0));tick();near(output.yaw_degrees,1);
    CHECK(state.route.raw(state.now+1,0,false,0,20,0));host.paused=1;tick();near(output.yaw_degrees,0);CHECK(state.route.armed(state.now));CHECK(state.route.raw(state.now+1,0,false,0,20,0));
    host.paused=0;tick();CHECK(state.route.raw(state.now+1,0,false,0,20,0));gl_set_panel_open(c,1);tick();near(output.yaw_degrees,0);CHECK(!state.route.raw(state.now+1,0,false,0,20,0));
    gl_set_panel_open(c,0);tick();CHECK(state.route.raw(state.now+1,0,false,0,20,0));host.focused=0;tick();near(output.yaw_degrees,0);
    host.focused=1;host.menu_open=1;tick();CHECK(state.route.armed(state.now));CHECK(state.route.raw(state.now+1,0,false,0,20,0));
    host.menu_open=1;gl_set_host_capabilities(c,GL_HOST_MENU_STATE);gl_set_gameplay_context_output_target(c,1,GL_OUTPUT_CURSOR);tick();
    gl_choice converted{};CHECK(gl_choice_at(c,key,2,&converted)==GL_OK);
    const std::string cursor_label=converted.label,cursor_help=gl_choice_description(c,key,2);
    CHECK(gl_set_gameplay_context_output_target(c,1,GL_OUTPUT_CAMERA)==GL_OK);
    CHECK(gl_choice_at(c,key,2,&converted)==GL_OK);CHECK(cursor_label==converted.label);CHECK(cursor_help==gl_choice_description(c,key,2));
    CHECK(gl_set_gameplay_context_output_target(c,1,GL_OUTPUT_CURSOR)==GL_OK);
    state.callback_yaw=0;CHECK(state.route.raw(state.now+1,0,false,0,20,0));tick();near(output.yaw_degrees,1);near(state.callback_yaw,0);
    CHECK(gl_forget_endpoint(c,1)==GL_OK);state.now+=10;gl_set_gameplay_context_state(c,1,1,1);CHECK(gl_update(c,state.now*1000000,&host,&output)==GL_OK);CHECK(!metadata().visible);CHECK(c->steam_mouse_device==0);
    c->virtual_mouse_user=nullptr;c->virtual_mouse=nullptr;gl_destroy(c);
    std::cout<<"Steam mouse: detection, three modes, per-view settings, inheritance, persistence, camera/cursor output passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
