#include "declared_view.hpp"
#include "../src/detail/sensor_process.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc,char** argv){
    if(argc!=3)return 1;const bool live=std::strcmp(argv[2],"live")==0||std::strcmp(argv[2],"shell-live")==0;
    const bool stale=std::strcmp(argv[2],"stale")==0,bad=std::strcmp(argv[2],"bad")==0;
    const bool stall=std::strcmp(argv[2],"stall")==0;
    const bool automatic=std::strcmp(argv[2],"auto")==0;
    const bool contacts=std::strncmp(argv[2],"contacts",8)==0;
    const bool flick_stream=std::strcmp(argv[2],"flick-stream")==0;
    // Reproduce environment inherited at process launch, not just SDL's cached
    // environment object (SDL_CreateEnvironment(true) reads the OS environment).
    SDL_setenv_unsafe("SteamAppId","Fixture host untouched",1);
    SDL_setenv_unsafe("SteamVirtualGamepadInfo","Fixture host metadata",1);
    SDL_setenv_unsafe("GYROLIB_SENSOR_FIXTURE",argv[2],1);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"SteamAppId","Fixture host untouched",true);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"SteamVirtualGamepadInfo","Fixture host metadata",true);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"GYROLIB_SENSOR_FIXTURE",argv[2],true);
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES,"0x054c/0x0ce6");
    if(!SDL_Init(0))return 2;
    auto* c=gl_create(GL_ABI_VERSION);if(!declare_test_view(c))return 5;auto* process=sensor_process_create(c);
    gl_endpoint pad{};pad.id=pad.physical_id=77;pad.connected=1;pad.source=GL_SOURCE_SDL;gl_register_endpoint(c,&pad);
    if(automatic)gl_set_endpoint_pairing_hint(c,77,0xffff,0xfffe,1);
    gl_select_device(c,77);gl_host_state host{};host.focused=host.camera_allowed=1;
    if(contacts){
        gl_setting_set(c,"context.1.gyro.activation",GL_HOLD);
        gl_setting_set(c,std::strcmp(argv[2],"contacts-pad")==0?"context.1.activation.touchpad":
            std::strcmp(argv[2],"contacts-stick")==0?"context.1.activation.stick_touch":"context.1.activation.grip_touch",GL_SIDE_BOTH);
    }
    if(sensor_process_path(process,argv[1])!=GL_OK)return 3;
    uint64_t accepted=0,last_id=0;unsigned binds=0;double rotation=0;bool before_bind=false;
    unsigned active_frames=0,inactive_frames=0,transitions=0;bool previous=false,contact_caps=false,labels=false,duplicates_hidden=false;
    bool pad_position=false,pad_released=false;
    unsigned flick_changes=0;float previous_axis=0;uint64_t last_flick_change=0,max_flick_gap=0;
    bool feedback_sent=false,feedback_delivered=false;
    // Measure stream behavior after the fixture appears, separately from OS
    // startup. A slow process launch must not consume the entire data window.
    const auto begin=SDL_GetTicks();Uint64 ready=0;double resumed=0;
    while(SDL_GetTicks()<(ready?ready+(stall?1800:1000):begin+10000)){
        const auto now=SDL_GetTicksNS();sensor_process_poll(process,now,true);
        gl_output output{};update_test_view(c,now,&host,&output);rotation+=std::abs(output.yaw_degrees);
        if(flick_stream){
            gl_flick_input input{};gl_get_flick_input(c,&input);
            if(input.available==3&&input.touching&&input.stick_x!=previous_axis){
                ++flick_changes;previous_axis=input.stick_x;
                if(last_flick_change)max_flick_gap=std::max(max_flick_gap,now-last_flick_change);
                last_flick_change=now;
                if(!feedback_sent&&last_id)feedback_sent=sensor_process_feedback(process,last_id)==GL_OK;
            }
            feedback_delivered|=feedback_sent&&sensor_process_feedback_result(process)==GL_OK;
        }
        if(contacts){
            if(output.source==GL_SOURCE_SDL){if(output.gyro_active)++active_frames;else ++inactive_frames;}
            if(bool(output.gyro_active)!=previous)++transitions;previous=output.gyro_active!=0;
            labels|=std::strcmp(gl_get_button_label(c,25),"Grip Sense R")==0;
            gl_choice choice{};if(gl_choice_at(c,"context.1.activation.button",26,&choice)==GL_OK&&labels)duplicates_hidden|=!choice.available;
            gl_flick_input flick{};gl_get_flick_input(c,&flick);
            if(flick.available==3){pad_position|=flick.touching&&flick.touchpad_x==1&&flick.touchpad_y==1;pad_released|=!flick.touching;}
        }
        if(ready&&SDL_GetTicks()-ready>1200)resumed+=std::abs(output.yaw_degrees);
        gl_diagnostics d{};gl_get_diagnostics(c,&d);accepted+=d.accepted_samples?1:0;
        for(uint32_t i=0;i<gl_endpoint_count(c);++i){gl_endpoint e{};gl_get_endpoint(c,i,&e);
            if(gl_is_motion_companion(c,e.id)&&e.caps.touchpads==3&&e.caps.stick_touch==3&&e.caps.grip_touch==3&&e.caps.sticks==3)contact_caps=true;
            if(gl_is_motion_companion(c,e.id)&&e.id!=last_id){
                if(output.source==GL_SOURCE_NONE)before_bind=true;
                if(!automatic){gl_bind_motion_sensor(c,77,e.id);++binds;}
                last_id=e.id;
            }}
        if(automatic&&!binds&&gl_get_motion_sensor(c,77))++binds;
        if(!ready&&(last_id||(bad&&*sensor_process_error(process))))ready=SDL_GetTicks();
        SDL_Delay(1);
    }
    const std::string error=sensor_process_error(process);
    const auto closing=SDL_GetTicks();sensor_process_destroy(process);
    const bool stopped=SDL_GetTicks()-closing<1000&&gl_endpoint_count(c)==1;
    bool ok=before_bind&&binds>0&&(live?accepted>50:(stale?accepted==0&&rotation==0:rotation>10));
    if(bad)ok=binds==0&&error.find("protocol")!=std::string::npos;
    if(std::strcmp(argv[2],"fragment-delayed")==0)ok&=ready&&ready-begin>=2300;
    if(std::strcmp(argv[2],"reconnect")==0)ok&=binds>=2;
    if(stall)ok&=binds==1&&resumed>10; // same endpoint/binding, fresh rotation after re-arm
    if(contacts)ok&=contact_caps&&labels&&duplicates_hidden&&pad_position&&pad_released&&active_frames>30&&inactive_frames>30&&transitions>=4;
    if(flick_stream){std::printf("flick changes=%u max_gap_ms=%.2f feedback=%d\n",flick_changes,max_flick_gap*1e-6,feedback_delivered);ok&=flick_changes>80&&max_flick_gap<100000000&&feedback_delivered;}
    ok&=stopped&&std::strcmp(SDL_GetEnvironmentVariable(SDL_GetEnvironment(),"SteamAppId"),"Fixture host untouched")==0;
    ok&=std::strcmp(SDL_GetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES),"0x054c/0x0ce6")==0;
    std::printf("mode=%s startup_ms=%llu bound=%u frames_with_samples=%llu rotation=%.3f stopped=%d error=%s\n",argv[2],
        static_cast<unsigned long long>(ready?ready-begin:0),binds,static_cast<unsigned long long>(accepted),rotation,stopped,error.c_str());
    if(contacts)std::printf("contacts caps=%d labels=%d active=%u inactive=%u transitions=%u\n",contact_caps,labels,active_frames,inactive_frames,transitions);
    gl_destroy(c);SDL_Quit();return ok?0:4;
}
