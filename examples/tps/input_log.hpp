#pragma once
#include <gyrolib/sdl.h>
#include <gyrolib/runtime.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include "host.hpp"

// Opt-in by normal interactive launch, never used by tests or synthetic captures.
// Replaced per launch, capped at 1 MiB; no unbounded frame-by-frame logging.
class DemoInputLog {
    std::ofstream file_;
    std::string endpoints_;
    std::string reader_error_;
    uint64_t next_{};uint32_t source_=~0u,view_=~0u,active_=~0u;std::streamoff bytes_{};
    uint64_t flick_stamp_{},flick_updates_{},flick_max_age_{};
    gl_context* context_{};
    struct MotionTotals {
        uint64_t sensor{},arrival{},samples{},gaps{};
        double sensor_seconds{},arrival_seconds{},x{},y{},z{};
    };
    std::map<uint64_t,MotionTotals> motion_;
    static void GL_CALL observe(void* user,uint64_t endpoint,const gl_sample* sample) noexcept {
        try {
            auto& self=*static_cast<DemoInputLog*>(user);
            if(!self.motion_.count(endpoint)&&self.motion_.size()>=64)self.motion_.clear();
            auto& t=self.motion_[endpoint];++t.samples;
            if(t.sensor&&sample->sensor_ns>t.sensor&&sample->arrival_ns>=t.arrival){
                const auto sensor_dt=sample->sensor_ns-t.sensor,arrival_dt=sample->arrival_ns-t.arrival;
                if(sensor_dt<=100000000&&arrival_dt<=100000000){
                    const double dt=sensor_dt*1e-9;t.sensor_seconds+=dt;t.arrival_seconds+=arrival_dt*1e-9;
                    t.x+=sample->gyro_dps.x*dt;t.y+=sample->gyro_dps.y*dt;t.z+=sample->gyro_dps.z*dt;
                }else ++t.gaps;
            }else if(t.sensor)++t.gaps;
            t.sensor=sample->sensor_ns;t.arrival=sample->arrival_ns;
        }catch(...){}
    }
    void write(const std::string& line){
        if(file_&&bytes_<1024*1024){file_<<line<<'\n';file_.flush();bytes_+=static_cast<std::streamoff>(line.size()+1);}
    }
public:
    ~DemoInputLog(){close();}
    void close(){if(context_)gl_set_sample_observer(context_,nullptr,nullptr);context_=nullptr;if(file_)file_.close();}
    void open(gl_context* context,const std::string& path){
        file_.open(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())),std::ios::trunc);
        if(!file_)return;context_=context;gl_set_sample_observer(context_,observe,this);
        write("GyroLib Demo input diagnostic v11; stable gyro activity and acquisition metrics; SDL="+std::to_string(SDL_GetVersion()));
        const char* runtime=gl_runtime_directory();write(std::string("Runtime cache: ")+(*runtime?runtime:"modular build"));
        // Explicit non-sensitive allowlist, not a dump of the process environment.
        for(const char* name:{SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES,SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,
            SDL_HINT_JOYSTICK_HIDAPI,SDL_HINT_JOYSTICK_HIDAPI_STEAM,SDL_HINT_HIDAPI_IGNORE_DEVICES,
            "SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD","SteamAppId","SteamGameId","SteamVirtualGamepadInfo"}){
            const char* value=SDL_GetHint(name);write(std::string(name)+"="+(value?value:"<unset>"));
        }
        // Enumeration only: do not open, configure or write any HID device here.
        auto* devices=SDL_hid_enumerate(0,0);
        for(auto* d=devices;d;d=d->next){std::ostringstream line;
            line<<"HID vid="<<std::hex<<d->vendor_id<<" pid="<<d->product_id<<std::dec<<" interface="<<d->interface_number<<" bus="<<d->bus_type;
            write(line.str());}
        SDL_hid_free_enumeration(devices);
    }
    void update(gl_context* c,gl_sdl* s,const gl_output& output,uint64_t now,const tps::Host& camera){
        if(!file_)return;
        gl_flick_input flick{};gl_get_flick_input(c,&flick);
        if(flick.timestamp_ns&&flick.timestamp_ns!=flick_stamp_){++flick_updates_;flick_stamp_=flick.timestamp_ns;}
        if(flick.timestamp_ns&&now>=flick.timestamp_ns)flick_max_age_=std::max(flick_max_age_,now-flick.timestamp_ns);
        const std::string error=gl_sdl_error(s);
        if(error!=reader_error_){reader_error_=error;write("SDL reader status: "+(error.empty()?std::string("OK"):error));}
        std::ostringstream devices;
        for(uint32_t i=0;i<gl_endpoint_count(c);++i){
            gl_endpoint e{};gl_get_endpoint(c,i,&e);
            devices<<"endpoint="<<e.id<<" physical="<<e.physical_id<<" source="<<e.source<<" connected="<<e.connected
                <<" gyro="<<e.caps.gyro<<" accel="<<e.caps.accelerometer
                <<" buttons="<<e.caps.buttons<<" sticks="<<e.caps.sticks<<" touchpads="<<e.caps.touchpads<<" stick_touch="<<e.caps.stick_touch<<" grip_touch="<<e.caps.grip_touch
                <<" steam_handle="<<gl_sdl_steam_handle(s,e.id)<<" name="<<e.name<<'\n';
        }
        if(devices.str()!=endpoints_){endpoints_=devices.str();write("Devices changed:\n"+endpoints_);}
        const auto view=gl_get_active_gameplay_context(c);
        gl_gyro_state state{};gl_get_gyro_state(c,&state);
        if(now>=next_||source_!=output.source||view_!=view||active_!=state.enabled){
            source_=output.source;view_=view;active_=state.enabled;next_=now+2000000000ull;gl_diagnostics d{};gl_get_diagnostics(c,&d);
            gl_input_metrics metrics{};gl_get_input_metrics(c,output.endpoint_id,&metrics);
            double gain=0,space=0,clock_scale=1;gl_get_sensor_clock_scale(c,output.endpoint_id,&clock_scale);
            gl_setting_get(c,("context."+std::to_string(view)+".sensitivity_x").c_str(),&gain);
            gl_setting_get(c,("context."+std::to_string(view)+".gyro.space").c_str(),&space);
            std::ostringstream sample;sample<<std::setprecision(12)<<"time_ns="<<now<<" selected="<<gl_get_selected_device(c)<<" source="<<output.source
                <<" active="<<state.enabled<<" new_reports="<<state.new_samples<<" trackball="<<state.trackball_active
                <<" view="<<view<<" sensitivity_x="<<gain<<" space="<<space<<" clock_scale="<<clock_scale
                <<" clock_qualified="<<metrics.clock_qualified<<" report_hz="<<metrics.report_hz
                <<" report_jitter_ms="<<metrics.interval_jitter_ms<<" sample_age_ms="<<metrics.sample_age_ns*1e-6
                <<" accepted="<<d.accepted_samples<<" rejected="<<d.rejected_samples
                <<" dropped="<<d.dropped_samples
                <<" flick_updates="<<flick_updates_<<" flick_max_age_ms="<<flick_max_age_*1e-6;
            const auto it=motion_.find(output.endpoint_id);
            if(it!=motion_.end()){const auto& t=it->second;
                sample<<" sensor_s="<<t.sensor_seconds<<" arrival_s="<<t.arrival_seconds<<" received="<<t.samples<<" gaps="<<t.gaps
                    <<" input_deg="<<t.x<<','<<t.y<<','<<t.z;}
            sample<<" camera_yaw_deg="<<camera.camera_yaw_total<<" gyro_yaw_deg="<<camera.gyro_yaw_total
                <<" mouse_yaw_deg="<<camera.mouse_yaw_total<<" native_yaw_deg="<<camera.native_yaw_total
                <<" bias_dps="<<d.bias.x<<','<<d.bias.y<<','<<d.bias.z
                <<" gravity_length="<<std::hypot(d.gravity.x,d.gravity.y,d.gravity.z)
                <<" gravity="<<d.gravity.x<<','<<d.gravity.y<<','<<d.gravity.z;
            write(sample.str());
            flick_updates_=flick_max_age_=0;
        }
    }
};
