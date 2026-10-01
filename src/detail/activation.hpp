#pragma once
#include <algorithm>
#include <cstdint>
namespace gyrolib {
class AnalogActivation {
    uint32_t held_{};
public:
    void reset(){held_=0;}
    uint32_t update(double left,double right,double threshold){
        const double release=std::max(0.0,threshold-std::min(.05,threshold*.25));
        uint32_t next=0;
        if(left>((held_&1)?release:threshold))next|=1;
        if(right>((held_&2)?release:threshold))next|=2;
        return held_=next;
    }
};
}
