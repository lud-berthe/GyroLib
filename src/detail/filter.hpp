#pragma once
#include <algorithm>
#include <cmath>
namespace gyrolib {
// Soft tiered smoothing inspired by Jibb Smart. Split BEFORE filtering so that
// fast input bypasses the filter while the remaining slow-input tail is drained.
// The analytic interval mean preserves displacement even with variable dt.
class TieredFilter {
    double x_{},y_{};
public:
    void reset(){x_=y_=0;}
    void apply(double& x,double& y,double dt,double tau,double threshold){
        if(tau<=0||threshold<=0){reset();return;}
        const double weight=std::clamp((std::hypot(x,y)-threshold*.5)/(threshold*.5),0.0,1.0);
        const double alpha=-std::expm1(-dt/tau);
        const auto axis=[&](double input,double& state){
            const double slow=input*(1-weight),before=state;
            state+=alpha*(slow-state);
            return input*weight+slow+(before-slow)*(tau*alpha/dt);
        };
        x=axis(x,x_);y=axis(y,y_);
    }
};
}
