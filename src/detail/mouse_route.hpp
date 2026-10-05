#pragma once
#include <cstdint>
#include <cmath>

namespace gyrolib_detail {
// Serialized by the shared input bridge. Anonymous raw packets alone are not enough:
// correlate them with injected window motion and the selected pad's contact.
struct MouseRoute {
    bool enabled=true,safe{},touching{},observing{},detected{},convert=true;
    uint32_t mode=1,view{},raw_count{};
    uint64_t device{},updated{},last_packet{},first_queued{},raw_at{},injected_at{};
    double gain=.05,x{},y{}; // degrees/count; not gyro sensor samples
    static bool fresh(uint64_t now,uint64_t stamp,uint64_t limit){return stamp&&now>=stamp&&now-stamp<=limit;}
    void clear(){safe=touching=observing=false;x=y=0;last_packet=first_queued=raw_at=injected_at=0;raw_count=0;}
    bool candidate(uint64_t now)const{
        return enabled&&observing&&device&&fresh(now,updated,100)&&
            (touching||fresh(now,last_packet,150));
    }
    bool armed(uint64_t now)const{return safe&&mode!=0&&detected&&candidate(now);}
    void detect(uint64_t now){if(raw_count>=3&&fresh(now,raw_at,100)&&fresh(now,injected_at,100))detected=true;}
    bool injected(uint64_t now){
        if(!candidate(now))return false;
        injected_at=now;detect(now);return armed(now);
    }
    bool raw(uint64_t now,uint64_t physical_device,bool absolute,uint16_t buttons,int32_t dx,int32_t dy){
        if(physical_device||absolute||buttons||(!dx&&!dy)||std::abs(double(dx))>8192||std::abs(double(dy))>8192)return false;
        if(!candidate(now))return false;
        raw_count=fresh(now,raw_at,100)?(raw_count<3?raw_count+1:3):1;
        raw_at=last_packet=now;detect(now);
        if(!armed(now))return false;
        if(mode==2&&convert){
            if(std::abs(x+dx)>1000000||std::abs(y+dy)>1000000){clear();return false;}
            if(!first_queued)first_queued=now;x+=dx;y+=dy;
        }
        return true;
    }
    void consume(uint64_t now,bool allowed,uint32_t next_view,uint64_t next_device,bool contact,
                 uint32_t next_mode,bool observe,bool conversion,double& yaw,double& pitch){
        if(allowed&&safe&&mode==2&&next_mode==mode&&convert&&conversion&&view==next_view&&device==next_device&&fresh(now,first_queued,100)){
            yaw+=x*gain;pitch-=y*gain;
        }
        x=y=0;first_queued=0;
        if(device!=next_device){clear();detected=false;}
        if(!observe)clear();
        safe=allowed;view=next_view;device=next_device;touching=contact;updated=now;
        mode=next_mode;observing=observe;convert=conversion;
    }
};
}
