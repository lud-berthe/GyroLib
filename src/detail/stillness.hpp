#pragma once
#include <gyrolib/gyrolib.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace gyrolib {
// Time-weighted 250 ms windows separate sensor noise from sustained motion.
// No dead zone is applied to the motion delivered to the host.
class Stillness {
    double elapsed_{};
    unsigned samples_{};
    std::array<double,6> mean_{},variance_{},anchor_{};
    bool anchored_{},stationary_{},moving_{};
    void clear_window(){elapsed_=0;samples_=0;mean_={};variance_={};}
public:
    struct Window {bool complete{},stable{};double seconds{};unsigned samples{};gl_vec3 gyro{};};
    void reset(){*this={};}
    bool stationary()const{return stationary_;}
    bool moving()const{return moving_;}
    Window update(gl_vec3 gyro,gl_vec3 accel,double dt){
        // Large impulses cancel immediately; ordinary MEMS jitter is assessed
        // statistically instead of resetting a timer for each noisy sample.
        if(std::hypot(gyro.x,gyro.y,gyro.z)>6||std::abs(std::hypot(accel.x,accel.y,accel.z)-1)>.20){
            reset();moving_=true;return {};
        }
        const std::array<double,6> value={gyro.x,gyro.y,gyro.z,accel.x,accel.y,accel.z};
        elapsed_+=dt;++samples_;
        for(size_t i=0;i<value.size();++i){
            const double delta=value[i]-mean_[i];mean_[i]+=delta*dt/elapsed_;
            variance_[i]+=dt*delta*(value[i]-mean_[i]);
        }
        if(elapsed_<.25-1e-9)return {};
        const auto norm=[&](int offset){return std::hypot(mean_[offset],mean_[offset+1],mean_[offset+2]);};
        const auto drift=[&](int offset){return std::hypot(mean_[offset]-anchor_[offset],
            mean_[offset+1]-anchor_[offset+1],mean_[offset+2]-anchor_[offset+2]);};
        bool stable=norm(0)<3&&std::abs(norm(3)-1)<.08&&
            (variance_[0]+variance_[1]+variance_[2])/elapsed_<.9*.9&&
            (variance_[3]+variance_[4]+variance_[5])/elapsed_<.025*.025;
        if(stable&&anchored_)stable=drift(0)<.35&&drift(3)<.02;
        stationary_=stable;moving_=!stable;
        if(stable&&!anchored_){anchor_=mean_;anchored_=true;}
        if(!stable)anchored_=false;
        Window result{true,stable,elapsed_,samples_,{float(mean_[0]),float(mean_[1]),float(mean_[2])}};
        clear_window();return result;
    }
};
}
