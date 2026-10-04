// GyroLib flick-stick processing (MIT).
#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>
#include "filter.hpp"
namespace gyrolib {
struct FlickOptions {
    int style{},smoothing_ms{30},snap{};
    double smoothing_angle{1.5},snap_strength{1},forward_degrees{},duration_exponent{};
    bool operator==(const FlickOptions&)const=default;
};
class Flick {
    FlickOptions options_;
    double pending_{};
    double release_threshold_{.65},start_threshold_{.9};
    bool initialized_{},armed_{},edge_{},condition_paused_{};
    double angle_{},spin_{},elapsed_{},duration_{};
    int mode_{-1},duration_ms_{-1};
    uint32_t context_id_{};
    bool feedback_{};
    double feedback_distance_{};
    static double ease(double t) { t=std::clamp(t,0.0,1.0); return 1-std::pow(1-t,3); }
public:
    void reset() {const auto options=options_;const double inner=release_threshold_,outer=start_threshold_,
        speed=smoothing_speed,pi=pad_inner,po=pad_outer;*this=Flick{};options_=options;
        thresholds(inner,outer);smoothing_speed=speed;pad_inner=pi;pad_outer=po;}
    void configure(const FlickOptions& options){if(options_!=options){reset();options_=options;}}
    void thresholds(double inner,double outer){release_threshold_=inner;start_threshold_=outer;}
    double smoothing_speed{45};
    bool feedback() const { return feedback_; }
    double process(float x,float y,double dt,bool safe,bool enabled,int mode,int ms,uint32_t context_id,bool profile_changed=false,bool touchpad=false,bool new_input=true,double report_dt=0) {
        feedback_=false;
        // Zero animation time still permits a newly received, delayed input edge.
        if (!safe || !mode || !std::isfinite(dt) || dt<0 || dt>0.1) { reset(); return 0; }
        if (profile_changed || mode_!=mode || duration_ms_!=ms || context_id_!=context_id) {
            reset();mode_=mode;duration_ms_=ms;context_id_=context_id;
            condition_paused_=profile_changed;
        }
        if (!enabled) {
            pending_=0;
            initialized_=armed_=edge_=false;spin_=elapsed_=duration_=0;condition_paused_=true;return 0;
        }
        bool resume=condition_paused_;condition_paused_=false;
        double magnitude=std::hypot(x,y),angle=std::atan2(x,y)*180/std::numbers::pi;
        const double inner=touchpad?pad_inner:release_threshold_,outer=touchpad?pad_outer:start_threshold_;
        if (!initialized_) {
            initialized_=true; armed_=magnitude<=inner;
            if (resume && !armed_) { edge_=true; angle_=angle; }
            return 0;
        }
        double result=0;
        if(options_.smoothing_ms>0){const double portion=-std::expm1(-dt/(options_.smoothing_ms/1000.0));
            result+=pending_*portion;pending_*=1-portion;}
        else {result+=pending_;pending_=0;}
        if (magnitude<=inner) { armed_=true; edge_=false;result+=pending_;pending_=0; }
        else if (new_input&&(magnitude>=outer || edge_)) {
            if (armed_ && !edge_) {
                feedback_=touchpad;feedback_distance_=0;
                double pivot=std::abs(angle)<=options_.forward_degrees?0:angle;
                if(options_.snap){const double step=options_.snap==1?90:45;
                    pivot+=(std::round(pivot/step)*step-pivot)*options_.snap_strength;}
                if(options_.style==2)pivot=0;
                spin_=(duration_>0 ? spin_*(1-ease(elapsed_/duration_)) : 0)+pivot;
                elapsed_=0; duration_=ms/1000.0*std::pow(std::abs(pivot)/180,options_.duration_exponent);
                angle_=angle; edge_=true; armed_=false;result+=pending_;pending_=0;
                if (duration_==0) { result+=spin_; spin_=0; }
            } else if (edge_) {
                const double turn=std::remainder(angle-angle_,360.0);angle_=angle;
                if(options_.style!=1){const double input_dt=report_dt>0?report_dt:dt;
                    const double speed=input_dt>0?std::abs(turn)/input_dt:0;
                    const double direct=options_.smoothing_ms<=0||smoothing_speed<=0?1:
                        std::clamp((speed-smoothing_speed*.5)/(smoothing_speed*.5),0.0,1.0);
                    result+=turn*direct;pending_+=turn*(1-direct);}
                feedback_distance_+=std::abs(turn);
                if(touchpad&&feedback_distance_>=15){feedback_=true;feedback_distance_=std::fmod(feedback_distance_,15.0);}
            }
        }
        if (duration_>0 && elapsed_<duration_) {
            double before=ease(elapsed_/duration_); elapsed_=std::min(duration_,elapsed_+dt);
            result+=spin_*(ease(elapsed_/duration_)-before);
        }
        return result;
    }
    double pad_inner{.2},pad_outer{.35};
};
class LongPressBlocker {
    bool held_{},cancelled_{},forwarded_{};
    uint64_t started_{};
public:
    void cancel() { if (held_) cancelled_=true; }
    int event(unsigned event,uint64_t now,bool eligible,bool native_hold) {
        if (forwarded_) { if (event==GL_RELEASE) forwarded_=false; return GL_FORWARD; }
        if (held_ && (!eligible || native_hold)) cancelled_=true;
        if (event==GL_PRESS) {
            if (held_) return GL_SUPPRESS;
            if (!eligible || native_hold) { forwarded_=true; return GL_FORWARD; }
            held_=true; cancelled_=false; started_=now; return GL_SUPPRESS;
        }
        if (!held_) return GL_FORWARD;
        if (event==GL_RELEASE) {
            bool tap=!cancelled_ && eligible && now>=started_ && now-started_<200000000;
            held_=cancelled_=false; return tap ? GL_EMIT_TAP : GL_SUPPRESS;
        }
        return GL_SUPPRESS;
    }
};
}
