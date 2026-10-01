#pragma once
#include "gyrolib/gyrolib.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace gyrolib {
struct Projection { double pitch,yaw; };
// Independent implementation of Jibb Smart's gravity-relative Lean equations.
// World lean uses world pitch; player lean retains local pitch and relaxes roll.
inline Projection project_lean(gl_vec3 g,gl_vec3 grav,bool player){
    const double n=std::hypot(grav.x,grav.y,grav.z);
    if(n<1e-6)return {};
    const double x=grav.x/n,y=grav.y/n,z=grav.z/n;
    const double length=std::hypot(y,z);
    if(length<1e-6)return {player?g.x:0,0};
    const double reduction=std::clamp((std::max(std::abs(y),std::abs(z))-.125)/.125,0.0,1.0);
    const double roll=(-g.y*z+g.z*y)/length;
    const double pitch=player?g.x:(g.x*(1-x*x)-g.y*x*y-g.z*x*z)/length*reduction;
    const double yaw=player?std::copysign(std::min(std::abs(roll)*1.15,double(std::hypot(g.y,g.z))),roll):roll;
    // gravity points down: pitchAxis x gravity is the forward roll axis.
    // Positive rotation about it is a right lean. Motion later converts its
    // yaw rate with x=-yaw, so return the opposite sign for right-positive X.
    return {pitch,-yaw*reduction};
}
// Fixed SDL axes: +Y yaw and -Z roll before Motion's x=-yaw conversion.
// Those contributions agree for world turns when the pad's front is pitched UP.
// Local Roll and gravity-relative Lean intentionally use different gestures.
inline Projection project_local(gl_vec3 rate,double tilt_degrees,double complement) {
    double angle=tilt_degrees*std::numbers::pi/180,c=std::cos(angle),s=std::sin(angle);
    double primary=rate.y*c-rate.z*s,secondary=-rate.y*s-rate.z*c;
    return {rate.x,primary+complement*secondary};
}
// Angular azimuth/elevation velocity of a forward (-Z) ray. Gravity is DOWN.
// Unlike world-space projection this keeps vertical control with a side-held pad.
inline Projection project_laser(gl_vec3 rate,gl_vec3 gravity) {
    double length=std::hypot(gravity.x,gravity.y,gravity.z);
    if(length<1e-6)return {};
    double ux=-gravity.x/length,uy=-gravity.y/length;
    double horizontal=std::hypot(ux,uy);
    // Azimuth is undefined at the poles. Fade in a five-degree cone to avoid
    // divisions by zero and unbounded turns; no recentering or hidden state.
    constexpr double pole=0.08715574274765817; // sin(5 degrees)
    double pitch=(rate.x*uy-rate.y*ux)/std::max(horizontal,pole);
    double yaw=(rate.x*ux+rate.y*uy)/std::max(horizontal*horizontal,pole*pole);
    return {pitch,yaw};
}
}
