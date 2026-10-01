// Math adapted from ReturnalGyro contributors, MIT. No engine dependencies.
#include "detail/internal.hpp"
#include "detail/projection.hpp"
#include <algorithm>
#include <cmath>
namespace gyrolib {
Motion::Motion() {
    fusion_.Reset();
    fusion_.SetCalibrationMode(GamepadMotionHelpers::CalibrationMode::Manual);
    fusion_.PauseContinuousCalibration();
}
void Motion::clear_smoothing() { filter_.reset(); }
void Motion::resume() {
    previous_ns_=0; gravity_ready_=false; clear_smoothing(); stillness_.reset();
    clock_.resume();
    still_time_=collect_time_=0; count_=0; mean_=auto_mean_={}; diagnostics.stationary=0;
    clear_trackball();
}
void Motion::begin() {
    diagnostics.calibration_state=GL_CAL_COUNTDOWN;
    diagnostics.calibration_seconds_remaining=5;
    countdown_=5; collect_time_=still_time_=0; count_=0; mean_={}; clear_smoothing();
}
void Motion::cancel() {
    diagnostics.calibration_state=GL_CAL_IDLE;
    diagnostics.calibration_seconds_remaining=0; countdown_=collect_time_=0;
}
void Motion::process(const gl_sample& s,const Settings& v,bool steam,bool menu,bool active,bool allow_calibration,
                     gl_output& out,uint32_t track_axes) {
    if (steam) { diagnostics.calibration_state=GL_CAL_EXTERNAL; diagnostics.bias={}; }
    // Gravity must be re-observed after an acceleration outage. Local gyro
    // remains usable; ordinary activation changes never enter this branch.
    if(std::hypot(s.accel_g.x,s.accel_g.y,s.accel_g.z)<.05)gravity_ready_=false;
    if (!gravity_ready_) gravity_ready_=fusion_.InitializeGravityFromAcceleration(s.accel_g.x,s.accel_g.y,s.accel_g.z);
    if (!previous_ns_) { previous_ns_=s.sensor_ns;if(!steam)clock_.observe(s.sensor_ns,s.arrival_ns);return; }
    if (s.sensor_ns<=previous_ns_) { ++diagnostics.rejected_samples; return; }
    double dt=(s.sensor_ns-previous_ns_)*1e-9; previous_ns_=s.sensor_ns;
    if (dt>0.1) { resume(); ++diagnostics.rejected_samples; return; }
    if(!steam){clock_.observe(s.sensor_ns,s.arrival_ns);dt*=clock_.scale();}
    if (dt>0.1) { resume(); ++diagnostics.rejected_samples; return; }
    ++diagnostics.accepted_samples;
    const auto g=s.gyro_dps,a=s.accel_g;
    const auto window=stillness_.update(g,a,dt);
    diagnostics.stationary=stillness_.stationary();
    // Only our SDL stream owns a bias. Steam data is fused with zero local bias.
    bool manual=!steam && (diagnostics.calibration_state==GL_CAL_COUNTDOWN ||
        diagnostics.calibration_state==GL_CAL_COLLECTING || diagnostics.calibration_state==GL_CAL_MOVING);
    if (manual && countdown_>0) {
        countdown_=std::max(0.0,countdown_-dt);
        diagnostics.calibration_seconds_remaining=static_cast<float>(countdown_);
        if (countdown_==0) { diagnostics.calibration_state=GL_CAL_COLLECTING; stillness_.reset(); }
    } else if (manual) {
        if (stillness_.moving()) {
            diagnostics.calibration_state=GL_CAL_MOVING; collect_time_=0; count_=0; mean_={};
        } else if (window.complete && window.stable) {
            diagnostics.calibration_state=GL_CAL_COLLECTING;
            count_+=window.samples; collect_time_+=window.seconds;
            const auto add=[&](float& mean,float value){mean+=float(window.seconds/collect_time_)*(value-mean);};
            add(mean_.x,window.gyro.x);add(mean_.y,window.gyro.y);add(mean_.z,window.gyro.z);
            if (collect_time_>=1-1e-9 && count_>=20) {
                fusion_.SetCalibrationOffset(mean_.x,mean_.y,mean_.z,1);
                diagnostics.bias=mean_; diagnostics.calibration_state=GL_CAL_COMPLETE;
            }
        }
    }
    int auto_mode=static_cast<int>(v[AutoCal]);
    bool auto_allowed=allow_calibration && !steam && !manual && (auto_mode==GL_CAL_ANYTIME || (auto_mode==GL_CAL_MENUS && menu));
    // Gravity cannot distinguish constant yaw from bias. During actual gyro use,
    // only accept tiny residual corrections relative to the last trusted bias.
    // Larger errors remain correctable in menus, while disabled, or manually.
    const bool guarded=active&&!menu;
    if(guarded&&window.complete&&std::hypot(window.gyro.x-diagnostics.bias.x,
        window.gyro.y-diagnostics.bias.y,window.gyro.z-diagnostics.bias.z)>.15)auto_allowed=false;
    if (!auto_allowed || stillness_.moving()) still_time_=0;
    else if (window.complete && window.stable) {
        still_time_+=window.seconds;
        auto blend=[&](float& x,float y) { x+=static_cast<float>(window.seconds/still_time_)*(y-x); };
        blend(auto_mean_.x,window.gyro.x); blend(auto_mean_.y,window.gyro.y); blend(auto_mean_.z,window.gyro.z);
        if (still_time_>=1-1e-9) {
            auto bias=diagnostics.bias;
            float alpha=static_cast<float>(-std::expm1(-window.seconds/(guarded?8:2)));
            bias.x+=alpha*(auto_mean_.x-bias.x); bias.y+=alpha*(auto_mean_.y-bias.y); bias.z+=alpha*(auto_mean_.z-bias.z);
            fusion_.SetCalibrationOffset(bias.x,bias.y,bias.z,1); diagnostics.bias=bias;
        }
    }
    fusion_.ProcessMotion(g.x,g.y,g.z,a.x,a.y,a.z,static_cast<float>(dt));
    auto& gravity=diagnostics.gravity; auto& calibrated=diagnostics.calibrated_dps;
    fusion_.GetGravity(gravity.x,gravity.y,gravity.z);
    fusion_.GetCalibratedGyro(calibrated.x,calibrated.y,calibrated.z);
    // Update gravity/fusion regardless of ordinary camera activation changes.
    if (!active || manual || (gravity_space(int(v[Space]))&&!gravity_ready_)) { clear_smoothing(); return; }
    if (previous_space_!=v[Space]) { previous_space_=static_cast<int>(v[Space]); clear_smoothing(); }
    float pitch=calibrated.x,yaw=calibrated.y;
    switch (static_cast<int>(v[Space])) {
        case GL_SPACE_PLAYER: fusion_.GetPlayerSpaceGyro(pitch,yaw); break;
        case GL_SPACE_WORLD: {
            // Fusion corrects gravity by moving between vectors; its length can
            // temporarily fall below 1. Only direction defines the world axes.
            const auto length=std::hypot(gravity.x,gravity.y,gravity.z);
            if(length>1e-6)GamepadMotion::CalculateWorldSpaceGyro(pitch,yaw,
                calibrated.x,calibrated.y,calibrated.z,gravity.x/length,gravity.y/length,gravity.z/length);
            else pitch=yaw=0;
            break;
        }
        case GL_SPACE_PLAYER_LEAN:
        case GL_SPACE_WORLD_LEAN: {
            const auto projected=project_lean(calibrated,gravity,int(v[Space])==GL_SPACE_PLAYER_LEAN);
            pitch=static_cast<float>(projected.pitch);yaw=static_cast<float>(projected.yaw);break;
        }
        case GL_SPACE_LOCAL_ROLL: yaw=-calibrated.z; break;
        case GL_SPACE_LOCAL_YAW_ROLL: {
            // In this combined mode the two horizontal sources have separate
            // inversions. Pitch remains local X, with its own Invert Y below.
            auto axes=calibrated;
            if(v[InvertX])axes.y=-axes.y;
            if(v[InvertRoll])axes.z=-axes.z;
            const auto projected=project_local(axes,0,v[LocalContribution]/100);
            pitch=static_cast<float>(projected.pitch);yaw=static_cast<float>(projected.yaw);break;
        }
        case GL_SPACE_LOCAL_ADVANCED: {
            auto projected=project_local(calibrated,v[LocalAngle],v[LocalContribution]/100);
            pitch=static_cast<float>(projected.pitch);yaw=static_cast<float>(projected.yaw);break;
        }
        case GL_SPACE_LASER_POINTER: {
            auto projected=project_laser(calibrated,gravity);
            pitch=static_cast<float>(projected.pitch);yaw=static_cast<float>(projected.yaw);break;
        }
        default: break;
    }
    double x=-yaw,y=pitch;
    filter_.apply(x,y,dt,v[Smoothing]/1000,v[SmoothThreshold]);
    double speed=std::hypot(x,y);
    if(v[Tightening]>0&&speed<v[Tightening]){
        x*=speed/v[Tightening];y*=speed/v[Tightening];speed=std::hypot(x,y);
    }
    // Acceleration consumes the filtered/tightened velocity, never raw sensor noise.
    double sensitivity_x=v[SensX],sensitivity_y=v[SensY];
    {
        // Equal/reversed breakpoints form a well-defined step, never divide by zero.
        const double blend=v[FastSpeed]>v[SlowSpeed]?std::clamp((speed-v[SlowSpeed])/(v[FastSpeed]-v[SlowSpeed]),0.0,1.0):double(speed>v[SlowSpeed]);
        // Host zoom is applied below by core, so custom X/Y retain absolute units.
        sensitivity_x+=(v[FastSensX]-sensitivity_x)*blend;
        sensitivity_y+=(v[FastSensY]-sensitivity_y)*blend;
    }
    const bool invert_x=v[Space]!=double(GL_SPACE_LOCAL_YAW_ROLL)&&v[InvertX];
    const double vx=x*sensitivity_x*(invert_x?-1:1),vy=y*sensitivity_y*(v[InvertY]?-1:1);
    const double remember=-std::expm1(-dt/.04);
    if(!(track_axes&GL_MOD_TRACK_X)){track_x_+=remember*(vx-track_x_);out.yaw_degrees+=vx*dt;}
    if(!(track_axes&GL_MOD_TRACK_Y)){track_y_+=remember*(vy-track_y_);out.pitch_degrees+=vy*dt;}
    out.gyro_active=1;
}
void Motion::trackball(double dt,double decay,uint32_t axes,gl_output& out){
    if(!(axes&(GL_MOD_TRACK_X|GL_MOD_TRACK_Y))||dt<=0||dt>.1)return;
    const double lambda=decay*std::log(2.0),factor=std::exp(-lambda*dt);
    const double integral=lambda>0?-std::expm1(-lambda*dt)/lambda:dt;
    if(axes&GL_MOD_TRACK_X){out.yaw_degrees+=track_x_*integral;track_x_*=factor;}
    if(axes&GL_MOD_TRACK_Y){out.pitch_degrees+=track_y_*integral;track_y_*=factor;}
}
}
