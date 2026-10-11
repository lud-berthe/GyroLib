#pragma once
#include <gyrolib/gyrolib.h>
#include <algorithm>
#include <array>
#include <cstdint>
namespace gyrolib {
// Pace already integrated angles, not velocities: no extrapolation, gain change,
// or repeated fusion/calibration samples. Regular streams keep immediate output.
class MotionDelivery {
    struct Pending {double yaw{},pitch{},remaining{};};
    std::array<Pending,128> pending_{};
    uint64_t sensor_{},arrival_{},batch_arrival_{},frame_{};
    unsigned samples_{},evidence_{};
    uint32_t modifiers_{};
    bool burst_{},idle_{},paced_{};
    double window_=.015;
public:
    void clear(){pending_={};}
    void reset(){*this=MotionDelivery{};}
    void observe(const gl_sample& s){
        // Real intra-report batching, not merely several regularly timestamped
        // samples consumed in one render frame. Require subsequent empty frames.
        if(sensor_&&s.sensor_ns>sensor_&&s.sensor_ns-sensor_>=2000000&&
           s.arrival_ns>=arrival_&&s.arrival_ns-arrival_<1000000)burst_=true;
        sensor_=s.sensor_ns;arrival_=s.arrival_ns;++samples_;
    }
    void update(uint64_t now,bool allowed,uint32_t modifiers,gl_output& out){
        const double dt=frame_&&now>=frame_?(now-frame_)*1e-9:0;
        if(dt>.1||modifiers!=modifiers_)clear();
        modifiers_=modifiers;
        if(samples_){
            if(burst_&&idle_){
                evidence_=std::min(evidence_+1,3u);
                if(batch_arrival_&&arrival_>=batch_arrival_)
                    window_=std::clamp(std::max(window_*.98,(arrival_-batch_arrival_)*1e-9),.015,.060);
                if(evidence_==3)paced_=true;
            }else if(!paced_)evidence_=0;
            batch_arrival_=arrival_;idle_=false;
        }else if(dt>0)idle_=true;
        samples_=0;burst_=false;frame_=now;
        if(!allowed){clear();return;}
        if(!paced_)return;
        const double yaw=out.yaw_degrees,pitch=out.pitch_degrees;
        out.yaw_degrees=out.pitch_degrees=0;
        // Deliver only the elapsed part of previous arrivals. New angles start
        // at now; repeated updates with the same timestamp cannot advance them.
        for(auto& p:pending_)if(p.remaining>0){
            const double elapsed=std::min(dt,p.remaining),fraction=elapsed/p.remaining;
            const double x=p.yaw*fraction,y=p.pitch*fraction;
            out.yaw_degrees+=x;out.pitch_degrees+=y;
            p.yaw-=x;p.pitch-=y;p.remaining-=elapsed;
        }
        if(yaw!=0||pitch!=0){
            auto slot=std::find_if(pending_.begin(),pending_.end(),[](const auto& p){return p.remaining<=0;});
            // Bound storage even for pathological host polling rates. On overflow
            // merge into the newest segment, retaining all angle and the deadline.
            if(slot==pending_.end())slot=std::max_element(pending_.begin(),pending_.end(),
                [](const auto& a,const auto& b){return a.remaining<b.remaining;});
            slot->yaw+=yaw;slot->pitch+=pitch;if(slot->remaining<=0)slot->remaining=window_;
        }
        if(out.yaw_degrees!=0||out.pitch_degrees!=0)out.gyro_active=1;
    }
};
}
