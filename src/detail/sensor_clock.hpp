#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace gyrolib {
// Estimate device-clock frequency against monotonic arrivals over seconds, not
// frame-to-frame intervals. Hardware intervals still preserve report ordering
// and intra-frame motion; batching jitter must not become camera acceleration.
class SensorClock {
    struct Point {double sensor,arrival;};
    struct Fit {double rate=1,error=0;};
    std::array<Point,64> points_{};
    unsigned count_{},next_{};
    uint64_t origin_sensor_{},origin_arrival_{},last_arrival_{};
    double scale_=1;
    bool qualified_{};
    Point point(unsigned i)const{return points_[(count_==points_.size()?next_+i:i)%points_.size()];}
    Fit fit(unsigned start,unsigned count)const{
        double x=0,y=0;
        for(unsigned i=start;i<start+count;++i){const auto p=point(i);x+=p.sensor;y+=p.arrival;}
        x/=count;y/=count;
        double xx=0,xy=0;
        for(unsigned i=start;i<start+count;++i){const auto p=point(i);const double dx=p.sensor-x;xx+=dx*dx;xy+=dx*(p.arrival-y);}
        if(xx<=0)return {};
        const double rate=xy/xx;double residual=0;
        for(unsigned i=start;i<start+count;++i){const auto p=point(i);const double e=p.arrival-y-rate*(p.sensor-x);residual+=e*e;}
        return {rate,std::sqrt(residual/(count-2)/xx)};
    }
public:
    void resume(){count_=next_=0;origin_sensor_=origin_arrival_=last_arrival_=0;qualified_=false;}
    double scale()const{return scale_;}
    bool qualified()const{return qualified_;}
    void observe(uint64_t sensor,uint64_t arrival){
        if(!origin_sensor_){origin_sensor_=sensor;origin_arrival_=arrival;}
        if(sensor<origin_sensor_||arrival<origin_arrival_||(last_arrival_&&arrival<last_arrival_)){
            resume();origin_sensor_=sensor;origin_arrival_=arrival;
        }
        if(last_arrival_&&arrival-last_arrival_<125000000)return;
        last_arrival_=arrival;points_[next_]={double(sensor-origin_sensor_)*1e-9,double(arrival-origin_arrival_)*1e-9};
        next_=(next_+1)%points_.size();if(count_<points_.size())++count_;
        if(count_<12||point(count_-1).arrival-point(0).arrival<2)return;
        const auto whole=fit(0,count_);
        if(whole.rate<.5||whole.rate>2)return; // a stall or an invalid clock is not a gain setting
        // A transport-delay step can look like a different clock frequency in a
        // single regression. Require the same slope in both halves of the window.
        for(const auto part:{fit(0,count_/2),fit(count_/2,count_-count_/2)})
            if(std::abs(whole.rate-part.rate)>std::max(.001,5*std::hypot(whole.error,part.error)))return;
        qualified_=true;
        // Keep matching clocks exactly at unity. Require evidence beyond normal
        // arrival jitter before changing frequency; no controller-specific factor.
        if(std::abs(whole.rate-1)>std::max(.0001,5*whole.error))scale_=whole.rate;
        else if(std::abs(whole.rate-1)<=.0001&&whole.error<=.0001)scale_=1;
    }
};
}
