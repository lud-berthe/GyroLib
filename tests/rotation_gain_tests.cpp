#include <gyrolib/gyrolib.hpp>
#include "../examples/tps/host.hpp"
#include "../src/detail/sensor_clock.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x);}while(0)
static void near(double actual,double expected,double tolerance=.002){
    if(!std::isfinite(actual)||std::abs(actual-expected)>tolerance)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
struct SampleProbe {
    uint64_t previous{},calls{};double angle{};
    static void GL_CALL observe(void* user,uint64_t endpoint,const gl_sample* sample){
        auto& probe=*static_cast<SampleProbe*>(user);CHECK(endpoint==1);++probe.calls;
        if(probe.previous)probe.angle+=sample->gyro_dps.y*(sample->sensor_ns-probe.previous)*1e-9;
        probe.previous=sample->sensor_ns;
    }
};
static void revolution(int space,uint64_t sensor_step,uint64_t render_step,bool variable,double tilt_degrees=0,
                       double local_angle=0,double contribution=100,int direction=1,bool invert_x=false){
    gyrolib::Context owner;auto* c=owner.get();tps::Host camera;CHECK(camera.setup(c));
    SampleProbe probe;gl_set_sample_observer(c,SampleProbe::observe,&probe);
    gl_endpoint endpoint{};endpoint.id=endpoint.physical_id=1;endpoint.source=GL_SOURCE_SDL;
    endpoint.connected=1;endpoint.caps.gyro=endpoint.caps.accelerometer=1;CHECK(gl_register_endpoint(c,&endpoint)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.sensitivity_x",6)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.sensitivity_y",0)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.gyro.space",space)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.gyro.local_angle_degrees",local_angle)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.gyro.local_roll_percent",contribution)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.gyro.invert_x",invert_x)==GL_OK);
    if(space==GL_SPACE_LOCAL_YAW_ROLL)CHECK(gl_setting_set(c,"context.101.gyro.invert_roll",invert_x)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.gyro.smoothing_ms",0)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.gyro.acceleration",0)==GL_OK);
    uint64_t now=1000000000,next_render=now,elapsed=0;
    const tps::Input input{};
    const auto angle=tilt_degrees*std::acos(-1)/180;
    const float up_y=static_cast<float>(std::cos(angle)),up_z=static_cast<float>(std::sin(angle));
    auto submit=[&](float velocity,uint64_t dt){now+=dt;
        // A real vertical-axis turn, with the controller pitched relative to the floor.
        gl_sample sample{now,now,{0,velocity*up_y,velocity*up_z},{0,up_y,up_z}};
        CHECK(gl_submit_sample(c,1,&sample)==GL_OK);};
    for(int i=0;i<5;++i){submit(0,sensor_step);CHECK(camera.step(c,now,sensor_step*1e-9,input));}
    next_render=now+render_step;double total=0,previous_yaw=camera.yaw,physical=0;
    for(int i=0;elapsed<4000000000ull;++i){
        const auto dt=std::min(sensor_step,4000000000ull-elapsed);elapsed+=dt;
        // A known 360-degree turn, either constant rate or alternating speeds.
        const float velocity=direction*(variable?(i%2?120.f:60.f):90.f);physical+=velocity*dt*1e-9;
        submit(velocity,dt);
        if(now>=next_render||elapsed==4000000000ull){
            CHECK(camera.step(c,now,render_step*1e-9,input));
            total+=std::remainder(camera.yaw-previous_yaw,360.0);previous_yaw=camera.yaw;
            next_render=now+render_step;
        }
    }
    // This fixture's positive tilt pitches the FRONT DOWN. A front-up pose has
    // negative tilt, so a world turn has opposite local Y/Z signs. Local axes
    // deliberately retain only their components; Yaw + Roll sums them instead
    // of normalizing the sum or switching to whichever axis is dominant.
    double gain=1;
    switch(space){
    case GL_SPACE_LOCAL_YAW:gain=up_y;break;
    case GL_SPACE_LOCAL_ROLL:gain=-up_z;break;
    case GL_SPACE_LOCAL_YAW_ROLL:gain=up_y-up_z*contribution/100;break;
    case GL_SPACE_LOCAL_ADVANCED:{
        const auto a=local_angle*std::acos(-1)/180;
        gain=std::cos(angle+a)-contribution/100*std::sin(angle+a);break;
    }
    case GL_SPACE_PLAYER_LEAN:case GL_SPACE_WORLD_LEAN:gain=0;break;
    // Laser tracks the ray's azimuth; at the vertical pole it is undefined.
    case GL_SPACE_LASER_POINTER:if(std::abs(up_y)<1e-6)gain=0;break;
    }
    const auto expected=-physical*6*gain*(invert_x?-1:1);
    try{near(total,expected);near(physical,direction*360);near(camera.yaw,std::remainder(expected,360.0));}
    catch(const std::exception& e){throw std::runtime_error("space="+std::to_string(space)+", tilt="+std::to_string(tilt_degrees)+
        ", local angle="+std::to_string(local_angle)+", contribution="+std::to_string(contribution)+": "+e.what());}
    near(probe.angle,physical*up_y);near(camera.gyro_yaw_total,total);near(camera.camera_yaw_total,total);
    near(camera.mouse_yaw_total,0);near(camera.native_yaw_total,0);
    CHECK(probe.calls==5+4000000000ull/sensor_step);
    const auto calls=probe.calls;gl_sample duplicate{now,now,{0,90,0},{0,1,0}};
    CHECK(gl_submit_sample(c,1,&duplicate)==GL_INVALID);CHECK(probe.calls==calls);
    gl_set_sample_observer(c,nullptr,nullptr);submit(0,sensor_step);CHECK(probe.calls==calls);
}
static void clock_revolution(double clock_rate,uint64_t render_step,bool batched,bool steam=false){
    gyrolib::Context owner;auto* c=owner.get();tps::Host camera;CHECK(camera.setup(c));
    gl_endpoint endpoint{};endpoint.id=endpoint.physical_id=1;endpoint.connected=1;
    endpoint.source=steam?GL_SOURCE_STEAM:GL_SOURCE_SDL;endpoint.caps.gyro=endpoint.caps.accelerometer=1;
    CHECK(gl_register_endpoint(c,&endpoint)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.sensitivity_x",6)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.sensitivity_y",0)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.gyro.space",GL_SPACE_LOCAL_YAW)==GL_OK);
    uint64_t elapsed=0,now=1000000000,next_render=now,last_frame=now;std::vector<gl_sample> queue;
    const tps::Input input{};
    auto run=[&](uint64_t duration,float speed){
        for(uint64_t run=0;run<duration;run+=4000000){
            elapsed+=4000000;now=1000000000+elapsed;
            const auto sensor=8000000000+static_cast<uint64_t>(std::llround(elapsed*clock_rate));
            queue.push_back({sensor,now,{0,speed,0},{0,1,0}});
            if(now>=next_render||run+4000000==duration){
                for(auto sample:queue){if(batched)sample.arrival_ns=now;CHECK(gl_submit_sample(c,1,&sample)==GL_OK);}
                queue.clear();CHECK(camera.step(c,now,(now-last_frame)*1e-9,input));
                last_frame=now;next_render=now+render_step;
            }
        }
    };
    run(5000000000ull,0); // qualify the frequency without moving the camera
    double scale=0;CHECK(gl_get_sensor_clock_scale(c,1,&scale)==GL_OK);
    near(scale,steam?1:1/clock_rate,batched?.001:.0000001);
    run(4000000000ull,90);
    near(camera.gyro_yaw_total,-2160*(steam?clock_rate:1),batched?2:.002);
    near(camera.camera_yaw_total,camera.gyro_yaw_total);
    CHECK(gl_get_sensor_clock_scale(nullptr,1,&scale)==GL_INVALID);
    CHECK(gl_get_sensor_clock_scale(c,2,&scale)==GL_INVALID);
    CHECK(gl_get_sensor_clock_scale(c,1,nullptr)==GL_INVALID);
    const auto before=camera.gyro_yaw_total;
    CHECK(gl_get_sensor_clock_scale(c,1,&scale)==GL_OK);const auto learned=scale;
    elapsed+=1000000000;now+=1000000000; // wake/focus gap: retain learned frequency, never replay it
    run(80000000,0);CHECK(gl_get_sensor_clock_scale(c,1,&scale)==GL_OK);near(scale,learned,1e-12);
    run(4000000000ull,90);
    near(camera.gyro_yaw_total-before,-2160*(steam?clock_rate:1),batched?2:.002);
    gl_diagnostics d{};CHECK(gl_get_diagnostics(c,&d)==GL_OK);CHECK(!d.dropped_samples);
}
static void transport_delays(){
    gyrolib::SensorClock matching;
    for(uint64_t i=1;i<=5000;++i){
        const auto sensor=1000000000+i*4000000;
        const auto delay=static_cast<uint64_t>(20000000*(1+std::sin(i*4000000e-9*6.283185307179586)));
        matching.observe(sensor,sensor+delay);near(matching.scale(),1,1e-12);
    }
    gyrolib::SensorClock stepped;
    for(uint64_t i=1;i<=5000;++i){
        const auto sensor=1000000000+i*4000000;
        stepped.observe(sensor,sensor+(i>=2000?60000000:0));near(stepped.scale(),1,1e-12);
    }
    gyrolib::SensorClock invalid;
    for(uint64_t i=1;i<=3000;++i)invalid.observe(1000000000+i*4000000,2000000000+i*16000000);
    near(invalid.scale(),1,1e-12); // impossible clock ratio cannot become a gain
    invalid.resume();invalid.observe(10,100);invalid.observe(20,200);invalid.observe(5,50);
    near(invalid.scale(),1,1e-12);
}
int main(){try{
    unsigned turns=0;
    for(int space:{GL_SPACE_LOCAL_YAW,GL_SPACE_PLAYER,GL_SPACE_WORLD})
        for(uint64_t sensor:{1000000ull,4000000ull,10000000ull,20000000ull})
            for(uint64_t render:{6944444ull,16666667ull,33333333ull,80000000ull})
                for(bool variable:{false,true}){revolution(space,sensor,render,variable);++turns;}
    for(int space:{GL_SPACE_LOCAL_YAW,GL_SPACE_PLAYER,GL_SPACE_WORLD})
        for(double tilt:{-30.,-12.,12.,30.}){revolution(space,4000000,16666667,false,tilt);++turns;}
    // Physical left/right full turns through the sample queue, sensor fusion,
    // setting projection and demo's real camera callback. Include front-up and
    // front-down vertical poses and the exact cancellation at front-down 45deg.
    for(int space:{GL_SPACE_LOCAL_YAW,GL_SPACE_LOCAL_ROLL,GL_SPACE_LOCAL_YAW_ROLL,
        GL_SPACE_PLAYER,GL_SPACE_WORLD,GL_SPACE_LASER_POINTER,GL_SPACE_PLAYER_LEAN,GL_SPACE_WORLD_LEAN})
        for(double tilt:{-90.,-45.,0.,45.,90.})
            for(uint64_t sensor:{1000000ull,4000000ull,10000000ull,20000000ull})
                for(int direction:{-1,1})for(bool inverted:{false,true}){
                    revolution(space,sensor,16666667,true,tilt,0,100,direction,inverted);++turns;
                }
    for(double angle:{-180.,-90.,0.,45.,90.,180.})for(double contribution:{-100.,0.,50.,100.})
        for(double tilt:{-45.,0.,45.})for(int direction:{-1,1}){
            revolution(GL_SPACE_LOCAL_ADVANCED,4000000,33333333,true,tilt,angle,contribution,direction);++turns;
        }
    for(double contribution:{-100.,0.,50.})for(double tilt:{-45.,0.,45.}){
        revolution(GL_SPACE_LOCAL_YAW_ROLL,4000000,33333333,true,tilt,0,contribution);++turns;
    }
    for(double clock:{.9,.9435,1.,1.1})for(uint64_t render:{6944444ull,16666667ull,33333333ull,80000000ull})
        for(bool batch:{false,true})clock_revolution(clock,render,batch);
    clock_revolution(.9435,16666667,false,true);transport_delays();
    std::printf("%u full-turn gain/direction cases across all nine spaces and 33 clock/wake cases passed; transport jitter is not gain.\n",turns);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
