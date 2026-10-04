#include <gyrolib/steam.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <initializer_list>
#include "declared_view.hpp"
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
int main(int argc,char** argv) {
    CHECK(argc==2);
    auto* ctx=gl_create(GL_ABI_VERSION);CHECK(ctx);
    auto* runtime=gl_steam_runtime_create(ctx);CHECK(runtime);
    CHECK(gl_steam_runtime_poll(runtime,1000000000,1)==GL_UNAVAILABLE);
    CHECK(std::strstr(gl_steam_runtime_error(runtime),"not loaded"));
    auto module=LoadLibraryA(argv[1]);CHECK(module);
    const auto mode=reinterpret_cast<void(*)(int)>(GetProcAddress(module,"FixtureSteamMode"));
    const auto forbidden=reinterpret_cast<int(*)()>(GetProcAddress(module,"FixtureSteamForbiddenCalls"));
    CHECK(mode && forbidden);
    uint64_t time=2000000000,frame=2;
    auto poll=[&] {time+=10000000;return gl_steam_runtime_poll(runtime,time,frame++);};
    mode(1);CHECK(poll()==GL_UNAVAILABLE);CHECK(gl_endpoint_count(ctx)==0);
    mode(2);CHECK(poll()==GL_UNAVAILABLE);CHECK(gl_endpoint_count(ctx)==0);
    mode(0);CHECK(poll()==GL_OK);CHECK(gl_endpoint_count(ctx)==1);
    gl_endpoint endpoint{};CHECK(gl_get_endpoint(ctx,0,&endpoint)==GL_OK);
    CHECK(endpoint.physical_id==42 && endpoint.source==GL_SOURCE_STEAM && endpoint.caps.gyro);
    CHECK(poll()==GL_OK);
    CHECK(gl_steam_runtime_poll(runtime,time,frame-1)==GL_INVALID);
    CHECK(gl_steam_runtime_poll(runtime,time-1,frame)==GL_INVALID);
    mode(4);CHECK(poll()==GL_LIMIT);
    mode(5);CHECK(poll()==GL_INVALID);
    mode(3);CHECK(poll()==GL_OK);CHECK(gl_endpoint_count(ctx)==0);
    CHECK(std::strstr(gl_steam_runtime_error(runtime),"No controllers"));
    mode(0);CHECK(poll()==GL_OK);CHECK(gl_endpoint_count(ctx)==1);
    mode(1);CHECK(poll()==GL_UNAVAILABLE);CHECK(gl_endpoint_count(ctx)==0);
    mode(0);CHECK(poll()==GL_OK);CHECK(gl_endpoint_count(ctx)==1);
    CHECK(declare_test_view(ctx));
    CHECK(gl_setting_set(ctx,"context.1.gyro.space",GL_SPACE_LOCAL_YAW)==GL_OK);
    CHECK(gl_setting_set(ctx,"context.1.gyro.sensitivity_x",1)==GL_OK);
    gl_host_state host{};host.focused=host.camera_allowed=1;
    gl_output output{};
    for(int i=0;i<80;++i) {CHECK(poll()==GL_OK);CHECK(update_test_view(ctx,time,&host,&output)==GL_OK);}
    CHECK(output.source==GL_SOURCE_STEAM);
    CHECK(std::abs(output.yaw_degrees+1)<0.01); // 100 degrees/s * 10 ms.
    // A verified SDL identity shares this Steam handle. SDL must take over
    // only after qualification, and the two angular streams must never add.
    gl_endpoint sdl{};sdl.id=99;sdl.physical_id=42;sdl.source=GL_SOURCE_SDL;
    sdl.connected=1;sdl.caps.gyro=sdl.caps.accelerometer=1;
    CHECK(gl_register_endpoint(ctx,&sdl)==GL_OK);
    for(int i=0;i<250;++i) {
        CHECK(poll()==GL_OK);
        gl_sample sample{time,time,{0,100,0},{0,1,0}};
        CHECK(gl_submit_sample(ctx,sdl.id,&sample)==GL_OK);
        CHECK(update_test_view(ctx,time,&host,&output)==GL_OK);
        CHECK(std::abs(output.yaw_degrees)<=1.01);
    }
    CHECK(output.source==GL_SOURCE_SDL);
    // Keep SDL connected but stop its motion: public Steam must take over.
    for(int i=0;i<40;++i) {CHECK(poll()==GL_OK);CHECK(update_test_view(ctx,time,&host,&output)==GL_OK);}
    CHECK(output.source==GL_SOURCE_STEAM);CHECK(std::abs(output.yaw_degrees+1)<0.01);
    gl_disconnect_endpoint(ctx,sdl.id);gl_forget_endpoint(ctx,sdl.id);
    // Observed host topology: sensorless Steam virtual pad + healthy public Steam,
    // then an independent SDL worker discovers/re-discovers the physical sensor.
    sdl.caps={};CHECK(gl_register_endpoint(ctx,&sdl)==GL_OK);
    CHECK(gl_set_endpoint_pairing_hint(ctx,sdl.id,0x28de,1,1)==GL_OK);
    for(uint64_t id:{81ull,82ull}) {
        gl_endpoint companion{};companion.id=companion.physical_id=id;
        companion.source=GL_SOURCE_SDL;companion.connected=1;
        companion.caps.gyro=companion.caps.accelerometer=1;
        CHECK(gl_register_endpoint(ctx,&companion)==GL_OK);
        CHECK(gl_set_motion_companion(ctx,id)==GL_OK);
        CHECK(gl_set_endpoint_pairing_hint(ctx,id,0x28de,2,0)==GL_OK);
        for(int i=0;i<250;++i) {
            CHECK(poll()==GL_OK);
            gl_sample sample{time,time,{0,100,0},{0,1,0}};
            CHECK(gl_submit_sample(ctx,id,&sample)==GL_OK);
            CHECK(update_test_view(ctx,time,&host,&output)==GL_OK);
            CHECK(std::abs(output.yaw_degrees)<=1.01);
        }
        CHECK(gl_get_motion_sensor(ctx,42)==id);
        CHECK(output.source==GL_SOURCE_SDL);
        gl_disconnect_endpoint(ctx,id);gl_forget_endpoint(ctx,id);
        for(int i=0;i<40;++i) {CHECK(poll()==GL_OK);CHECK(update_test_view(ctx,time,&host,&output)==GL_OK);}
        CHECK(output.source==GL_SOURCE_STEAM);
    }
    gl_disconnect_endpoint(ctx,sdl.id);gl_forget_endpoint(ctx,sdl.id);
    CHECK(forbidden()==0);
    gl_steam_runtime_destroy(runtime);CHECK(gl_endpoint_count(ctx)==0);
    CHECK(forbidden()==0);FreeLibrary(module);gl_destroy(ctx);
    std::puts("Loaded Steam flat API: lifecycle, reconnect, invalid reports and borrowed service passed");
}
