#include "detail/sensor_process.hpp"
#include "detail/sensor_wire.hpp"
#include "detail/runtime.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "detail/sensor_desktop.hpp"
#endif
using namespace gyrolib_sensor;
static size_t pipe_read(SDL_IOStream* io,void* buffer,size_t size){
#ifdef _WIN32
    // SDL 3.4's buffered Windows stream can return zero after consuming its
    // readahead on ERROR_NO_DATA. Read this nonblocking pipe directly, without
    // mixing SDL buffered reads. Fragmented-message regression covers this.
    auto handle=static_cast<HANDLE>(SDL_GetPointerProperty(SDL_GetIOProperties(io),SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER,nullptr));
    DWORD read=0;return ReadFile(handle,buffer,static_cast<DWORD>(size),&read,nullptr)?read:0;
#else
    return SDL_ReadIO(io,buffer,size);
#endif
}
struct SensorProcess {
    gl_context* context{};SDL_Process* child{};std::string path,error;
    std::map<uint64_t,uint64_t> endpoints;
    std::array<unsigned char,sizeof(Record)> partial{};size_t used{};
    uint64_t retry{},last_message{},next_id=1;bool hello{};
    int feedback_result=GL_UNAVAILABLE;
    bool bundled{};
#ifdef _WIN32
    DesktopSensor desktop;
#endif
};
static bool running(SensorProcess* p){
#ifdef _WIN32
    if(p->desktop.pipe!=INVALID_HANDLE_VALUE)return true;
#endif
    return p->child!=nullptr;
}
static bool exited(SensorProcess* p){
#ifdef _WIN32
    if(p->desktop.pipe!=INVALID_HANDLE_VALUE)return sensor_desktop_exited(p->desktop);
#endif
    return SDL_WaitProcess(p->child,false,nullptr);
}
static size_t read_output(SensorProcess* p,void* data,size_t size){
#ifdef _WIN32
    if(p->desktop.pipe!=INVALID_HANDLE_VALUE)return sensor_desktop_read(p->desktop,data,size);
#endif
    return pipe_read(SDL_GetProcessOutput(p->child),data,size);
}
static void stop(SensorProcess* p){
    for(auto [remote,id]:p->endpoints){gl_disconnect_endpoint(p->context,id);gl_forget_endpoint(p->context,id);}p->endpoints.clear();
    if(p->child){
        const Command quit{.kind=Quit};auto* input=SDL_GetProcessInput(p->child);
#ifdef _WIN32
        auto handle=static_cast<HANDLE>(SDL_GetPointerProperty(SDL_GetIOProperties(input),SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER,nullptr));
        DWORD written=0;WriteFile(handle,&quit,sizeof(quit),&written,nullptr);
#else
        SDL_WriteIO(input,&quit,sizeof(quit));
#endif
        const auto until=SDL_GetTicks()+100;bool ended=false;
        while(!(ended=SDL_WaitProcess(p->child,false,nullptr))&&SDL_GetTicks()<until){
            unsigned char drain[4096];pipe_read(SDL_GetProcessOutput(p->child),drain,sizeof(drain));SDL_Delay(1);
        }
        if(!ended)SDL_KillProcess(p->child,true);
        SDL_WaitProcess(p->child,true,nullptr);SDL_DestroyProcess(p->child);p->child=nullptr;
    }
    p->hello=false;p->used=0;p->feedback_result=GL_UNAVAILABLE;
#ifdef _WIN32
    sensor_desktop_stop(p->desktop);
#endif
}
SensorProcess* sensor_process_create(gl_context* c){
    auto* p=new(std::nothrow) SensorProcess;if(!p)return nullptr;p->context=c;
#ifdef GL_SINGLE_DLL
    p->bundled=true;return p;
#else
    try {const char* base=SDL_GetBasePath();if(base)p->path=std::string(base)+"gyrolib_sensor_worker"
#ifdef _WIN32
        ".exe"
#endif
        ;}catch(...){delete p;return nullptr;}return p;
#endif
}
void sensor_process_destroy(SensorProcess* p){if(p){stop(p);delete p;}}
int sensor_process_path(SensorProcess* p,const char* path){
    if(!p||!path)return GL_INVALID;std::string replacement(path);
    if(!replacement.empty()&&!std::filesystem::path(reinterpret_cast<const char8_t*>(replacement.c_str())).is_absolute())return GL_INVALID;
    stop(p);p->bundled=false;p->path=std::move(replacement);p->retry=0;p->error.clear();return GL_OK;
}
const char* sensor_process_error(const SensorProcess* p){return p?p->error.c_str():"Sensor process unavailable";}
static bool start(SensorProcess* p,uint64_t now){
    p->retry=now+5000000000ull;
#ifdef GL_SINGLE_DLL
    if(p->bundled){p->path=gyrolib_runtime_worker(p->error);if(p->path.empty())return false;}
#endif
#ifdef _WIN32
    // Steam injects into ordinary descendants even with a sanitized environment.
    // Use the documented desktop launch API when the host has Steam's module.
    if(GetModuleHandleW(L"gameoverlayrenderer64.dll")||GetModuleHandleW(L"gameoverlayrenderer.dll")
#ifdef GL_SENSOR_PROCESS_TEST
        ||(SDL_getenv("GYROLIB_SENSOR_FIXTURE")&&std::strncmp(SDL_getenv("GYROLIB_SENSOR_FIXTURE"),"shell",5)==0)
#endif
    ){
        if(!sensor_desktop_start(p->desktop,p->path,p->error))return false;
        p->error.clear();p->last_message=now;return true;
    }
#endif
    auto* env=SDL_CreateEnvironment(true);
    if(!env){p->error=std::string("Cannot prepare isolated sensor environment: ")+SDL_GetError();return false;}
    // This copy belongs solely to the helper. Never alter the host environment,
    // its SDL hints, Steam session, overlay, input mappings or events.
    for(const char* key:{"SteamAppId","SteamGameId","SteamOverlayGameId","SteamVirtualGamepadInfo","SDL_GAMECONTROLLERCONFIG","SDL_GAMECONTROLLER_IGNORE_DEVICES","SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT","SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD","SDL3_DYNAMIC_API"})SDL_UnsetEnvironmentVariable(env,key);
    SDL_SetEnvironmentVariable(env,"SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD","0",true);
    const char* args[]={p->path.c_str(),"--pipe-v1",nullptr};auto props=SDL_CreateProperties();
    SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,const_cast<char**>(args));
    SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,env);
    SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,SDL_PROCESS_STDIO_NULL);
#ifdef _WIN32
    SDL_SetBooleanProperty(props,SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN,true); // CREATE_NO_WINDOW; pipes stay owned
#endif
    p->child=SDL_CreateProcessWithProperties(props);SDL_DestroyProperties(props);SDL_DestroyEnvironment(env);
    if(!p->child){p->error=std::string("Cannot start isolated SDL sensor reader: ")+SDL_GetError();return false;}
    p->error.clear();p->last_message=now;return true;
}
static bool receive(SensorProcess* p,const Record& r,uint64_t now){
    if(r.signature!=magic||r.revision!=version||r.kind<Hello||r.kind>Feedback)return false;
    if(r.kind==Hello){if(p->hello)return false;p->hello=true;return true;}
    if(!p->hello)return false;
    if(r.kind==Heartbeat)return true;
    if(!r.endpoint)return false;
    auto i=p->endpoints.find(r.endpoint);
    if(r.kind==Device){
        if(i!=p->endpoints.end()||p->endpoints.size()>=32||!r.identity||!std::memchr(r.name,0,sizeof(r.name)))return false;
        gl_endpoint e{};e.id=0x3000000000000000ull|p->next_id++;e.physical_id=r.identity;
        e.source=GL_SOURCE_SDL;e.connected=1;e.caps.gyro=1;e.caps.accelerometer=r.caps.accelerometer;std::memcpy(e.name,r.name,sizeof(e.name));
        if(gl_register_endpoint(p->context,&e)!=GL_OK)return false;
        gl_set_endpoint_pairing_hint(p->context,e.id,r.hardware&65535,r.hardware>>16,0);
        gl_set_motion_companion(p->context,e.id);p->endpoints.emplace(r.endpoint,e.id);return true;
    }
    if(i==p->endpoints.end())return false;
    if(r.kind==Feedback){
        p->feedback_result=static_cast<int32_t>(r.hardware);
        if(p->feedback_result!=GL_OK)p->error="Right-touchpad feedback unavailable or write failed";
        else if(p->error=="Right-touchpad feedback unavailable or write failed")p->error.clear();
        return true;
    }
    if(r.kind==Removed){gl_disconnect_endpoint(p->context,i->second);gl_forget_endpoint(p->context,i->second);p->endpoints.erase(i);return true;}
    if(r.kind==ButtonLabel){
        if(r.hardware>=32||!std::memchr(r.name,0,sizeof(r.name)))return false;
        if(gl_set_button_contact(p->context,i->second,r.hardware,static_cast<uint32_t>(r.sensor_ns),static_cast<uint32_t>(r.sensor_ns>>32))!=GL_OK)return false;
        return gl_set_button_label(p->context,i->second,r.hardware,r.name,GL_LABEL_PHYSICAL)==GL_OK;
    }
    if(r.kind==Capabilities){
        if((r.caps.sticks&~3u)||(r.hardware&~GL_CONTROL_ALL)||r.caps.gyro!=1||r.caps.accelerometer>1||
           (r.caps.touchpads&~7u)||(r.caps.stick_touch&~7u)||(r.caps.grip_touch&~7u))return false;
        for(uint32_t n=0;n<gl_endpoint_count(p->context);++n){gl_endpoint e{};gl_get_endpoint(p->context,n,&e);
            if(e.id==i->second){for(int side=0;side<2;++side){if(!std::memchr(r.trigger_labels[side],0,32))return false;
                    gl_set_trigger_label(p->context,e.id,side?GL_RIGHT:GL_LEFT,r.trigger_labels[side]);}
                e.caps=r.caps;return gl_register_endpoint(p->context,&e)==GL_OK&&
                gl_set_endpoint_control_authority(p->context,e.id,r.hardware)==GL_OK;}}
        return false;
    }
    const auto current=clock_ns();if(!fresh(r.observed_ns,current)||current-r.observed_ns>=now)return true;
    if(r.kind==Controls){
        auto controls=r.controls;controls.timestamp_ns=now-(current-r.observed_ns);
        auto flick=r.flick;flick.timestamp_ns=controls.timestamp_ns;
        auto triggers=r.triggers;triggers.timestamp_ns=controls.timestamp_ns;
        return gl_submit_controls(p->context,i->second,&controls)==GL_OK&&gl_submit_flick_input(p->context,i->second,&flick)==GL_OK&&
            gl_submit_trigger_input(p->context,i->second,&triggers)==GL_OK;
    }
    const gl_sample sample{r.sensor_ns,now-(current-r.observed_ns),r.gyro,r.accel};
    gl_submit_sample(p->context,i->second,&sample);return true;
}
int sensor_process_feedback_result(const SensorProcess* p){return p?p->feedback_result:GL_UNAVAILABLE;}
int sensor_process_feedback(SensorProcess* p,uint64_t endpoint){
    if(!p||!p->hello||!running(p))return GL_UNAVAILABLE;
    uint64_t remote=0;for(auto [key,id]:p->endpoints)if(id==endpoint){remote=key;break;}
    if(!remote)return GL_UNAVAILABLE;
    const Command command{.kind=RightPadPulse,.endpoint=remote,.observed_ns=clock_ns()};
#ifdef _WIN32
    auto handle=p->desktop.pipe!=INVALID_HANDLE_VALUE?p->desktop.pipe:
        static_cast<HANDLE>(SDL_GetPointerProperty(SDL_GetIOProperties(SDL_GetProcessInput(p->child)),SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER,nullptr));
    DWORD written=0;const bool ok=WriteFile(handle,&command,sizeof(command),&written,nullptr)&&written==sizeof(command);
#else
    const bool ok=SDL_WriteIO(SDL_GetProcessInput(p->child),&command,sizeof(command))==sizeof(command);
#endif
    return ok?GL_OK:GL_IO_ERROR;
}
void sensor_process_poll(SensorProcess* p,uint64_t now,bool needed){
    if(!p||(!p->bundled&&p->path.empty()))return;
    if(!running(p)){if(!needed||now<p->retry||!start(p,now))return;}
    bool valid=true;
    // Bounded work per host frame, with partial-record handling. Old samples are
    // aged against an OS monotonic clock shared by both processes, never arrival.
    for(unsigned n=0;n<2048;++n){
        const auto read=read_output(p,p->partial.data()+p->used,p->partial.size()-p->used);
        if(!read)break;p->used+=read;
        if(p->used==sizeof(Record)){Record r;std::memcpy(&r,p->partial.data(),sizeof(r));p->used=0;
            if(!(valid=receive(p,r,now))){p->error="Invalid isolated sensor protocol: signature="+std::to_string(r.signature)+" version="+std::to_string(r.revision)+" kind="+std::to_string(r.kind)+" endpoint="+std::to_string(r.endpoint);break;}p->last_message=now;}
    }
    // SDL's initial device enumeration can take several seconds on a loaded
    // Windows host. Keep a bounded startup grace, then the short live watchdog.
    const bool child_exited=exited(p);
    const bool timed_out=now-p->last_message>(p->hello?2000000000ull:10000000000ull);
    if(!valid||child_exited||timed_out){
        if(valid)p->error=child_exited?"Isolated SDL sensor reader stopped":
            p->hello?"Isolated SDL sensor stream timed out":"Isolated SDL sensor startup timed out";
        stop(p);p->retry=now+5000000000ull;
    }
}
