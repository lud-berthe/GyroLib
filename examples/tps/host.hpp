#pragma once
#include <gyrolib/gyrolib.h>
#include "controller_actions.hpp"
#include "performance.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>

namespace tps {
constexpr uint32_t Explore=101,AimStandard=205,AimSniper=206,Inventory=309;
constexpr double rad=std::numbers::pi/180;
struct V3 {
    double x{},y{},z{};
    V3 operator+(V3 b)const{return {x+b.x,y+b.y,z+b.z};}
    V3 operator-(V3 b)const{return {x-b.x,y-b.y,z-b.z};}
    V3 operator*(double s)const{return {x*s,y*s,z*s};}
};
inline double dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V3 unit(V3 v){const double length=std::sqrt(dot(v,v));return length>1e-9?v*(1/length):V3{0,0,1};}
struct Input {
    bool focused=true,aim{},inventory_press{},pause_press{},fire_press{},fire_held{},confirm_press{},reload_press{},recenter_press{},zoom_press{};
    double move_x{},move_z{},look_x{},look_y{},mouse_yaw{},mouse_pitch{};
    double controller_look_x{},controller_look_y{};
    bool pointer_moved{};double pointer_x{},pointer_y{};
    uint64_t controller_id{};uint32_t controller_buttons{};
    bool native_hold{}; // host interaction that needs the original held action
};
enum WeaponId { Rifle,Sniper,Pistol,Shotgun };
struct Recoil {
    // Camera angles in degrees, recovery time constants in seconds, push in metres.
    double pitch,yaw,recovery,max_pitch,push,push_recovery;
};
struct Weapon {
    const char* name;int capacity;double reload_seconds,shot_interval;Recoil recoil;double muzzle_z;
    bool automatic,scope;int pellets=1;double spread_degrees=0;
};
inline constexpr std::array<Weapon,4> weapons={{{"Rifle",30,1.65,.13,{.85,.20,.28,3.0,.055,.07},.79,true,false},
    {"Sniper",5,2.2,.85,{2.4,.12,.40,4.0,.10,.12},1.12,false,true},
    {"Pistol",12,1.25,.22,{1.65,.18,.16,3.0,.065,.065},.50,false,false},
    {"Shotgun",6,2.6,.85,{4.2,.32,.30,6.0,.16,.10},.99,false,false,9,2.0}}};
inline constexpr double range_end=120,range_start=-8,range_side=17,range_wall_height=12,shot_range=180;
inline constexpr std::array<double,2> sniper_fovs={20,10};
inline constexpr std::array<V3,6> crates={{{-6,0,6},{6,0,9},{-10,0,32},{10,0,54},{-10,0,76},{10,0,98}}};
struct Target {
    V3 position,origin,travel;double speed,phase;std::array<double,3> flash{};
    Target(V3 center={},V3 amplitude={},double rate=.9,double offset=0):
        position(center),origin(center),travel(amplitude),speed(rate),phase(offset){update(0);}
    void update(double time){
        if(travel.x||travel.y)position=origin+V3{travel.x*std::sin(time*speed+phase),
            travel.y*std::sin(time*speed*(travel.x?1.37:1)+phase),0};
    }
    static constexpr double radius=.70;
    static int zone(double distance){return distance<=radius/3?0:distance<=radius*2/3?1:distance<=radius?2:-1;}
};
struct Impact {V3 position;int target=-1,zone=-1;};
inline double ray_box(V3 origin,V3 direction,V3 base,V3 size,double angle=0){
    auto local=[&](V3 p){return V3{p.x*std::cos(angle)-p.z*std::sin(angle),p.y,p.x*std::sin(angle)+p.z*std::cos(angle)};};
    const auto o=local(origin-base),d=local(direction);double lo=0,hi=1000;
    const double origins[]={o.x,o.y,o.z},dirs[]={d.x,d.y,d.z};
    const double mins[]={-size.x/2,0,-size.z/2},maxs[]={size.x/2,size.y,size.z/2};
    for(int i=0;i<3;++i){
        if(std::abs(dirs[i])<1e-9){if(origins[i]<mins[i]||origins[i]>maxs[i])return 1000;continue;}
        double a=(mins[i]-origins[i])/dirs[i],b=(maxs[i]-origins[i])/dirs[i];if(a>b)std::swap(a,b);
        lo=std::max(lo,a);hi=std::min(hi,b);if(lo>hi)return 1000;
    }return lo;
}
// Shared solid geometry for the camera boom and weapon traces. Padding protects
// the camera's near plane as well as its centre from walls and the ground.
inline double trace_solids(V3 origin,V3 direction,double range=shot_range,double padding=0){
    double closest=range;
    const auto box=[&](V3 base,V3 size,double angle=0){
        closest=std::min(closest,ray_box(origin,direction,base-V3{0,padding,0},
            size+V3{2*padding,2*padding,2*padding},angle));};
    for(auto crate:crates){box(crate,{2.2,1.15,1.6},.15);box(crate+V3{0,1.15,0},{1.95,.12,1.45},.15);}
    box({0,0,range_end},{2*range_side+2,range_wall_height,1});
    for(double x:{-range_side,range_side})box({x,0,(range_start+range_end)/2},{1,4,range_end-range_start});
    for(int z=5;z<range_end;z+=18)for(double x:{-13.,13.})box({x,0,double(z)},{1.2,5.5,1.2});
    if(direction.y<0)closest=std::min(closest,std::max(0.0,(-.02+padding-origin.y)/direction.y));
    return closest;
}

// Everything here is the example game's responsibility. GyroLib only observes
// resolved commands and produces deltas. No SDL/engine types cross its C API.
struct Host {
    bool measure_performance{};uint64_t gyro_update_ns{};
    V3 player{};double yaw{},pitch=-2.5,aim_blend{},zoom_degrees=68,walk{},clock{};
    bool inventory{},aiming{},paused{},scope_view{},gyro_menu{};double cursor_x=.5,cursor_y=.5;
    int hovered=-1,equipped=Rifle,sniper_zoom=0;
    std::array<int,weapons.size()> magazines=[](){std::array<int,weapons.size()> ammo{};
        for(size_t i=0;i<weapons.size();++i)ammo[i]=weapons[i].capacity;return ammo;}();
    double shot_flash{},hit_flash{},kick{},kick_yaw{},weapon_kick{},cooldown{},reload_elapsed=-1,notification_time{};
    std::array<unsigned,weapons.size()> recoil_shots{};
    unsigned shots{},reloads{};int last_hit_zone=-1,last_fired_weapon=-1;V3 shot_end{},shot_start{};
    std::array<Impact,9> shot_impacts{};int impact_count{};
    std::string notification;
    std::array<Target,15> targets={{
        {{-7,1.7,18}},{{.8,1.7,22}},{{8,2.2,24},{2,0,0}},
        {{-8,3,40},{0,1.1,0},.8},{{0,3.4,44},{2.3,.9,0},.65},{{8,2.6,38}},
        {{-8,5,62},{2,0,0},.75},{{1,4.4,68},{0,1.6,0},.7},{{8,4.5,64},{1.8,1.2,0},.6},
        {{-9,5.8,86}},{{-1,6.2,92},{3,1.5,0},.5},{{9,6,88},{0,2,0},.65},
        {{-8,7.6,110},{2,0,0},.6},{{-3.5,10.3,114}},{{8,7.8,108},{2,1.4,0},.55}
    }};
    gl_output output{};unsigned camera_callbacks{};
    double camera_yaw_total{},gyro_yaw_total{},mouse_yaw_total{},native_yaw_total{};
    ControllerActions controller_actions;bool controller_confirm{};

    const Weapon& weapon()const{return weapons[equipped];}
    int ammo()const{return magazines[equipped];}
    bool reloading()const{return reload_elapsed>=0;}
    double reload_progress()const{return reloading()?std::clamp(reload_elapsed/weapon().reload_seconds,0.0,1.0):0;}
    double reload_pose()const{return reloading()?std::sin(std::numbers::pi*reload_progress()):0;}
    double pump_pose()const{
        if(equipped!=Shotgun||last_fired_weapon!=Shotgun||reloading())return 0;
        const double phase=(weapon().shot_interval-cooldown-.1)/.4;
        return phase>0&&phase<1?std::sin(std::numbers::pi*phase):0;
    }
    void clear_recoil(){kick=kick_yaw=weapon_kick=0;}
    void recover_recoil(double dt){
        if(last_fired_weapon<0)return;
        const auto& r=weapons[last_fired_weapon].recoil;
        const double recovery=std::exp(-dt/r.recovery);
        kick*=recovery;kick_yaw*=recovery;weapon_kick*=std::exp(-dt/r.push_recovery);
    }
    void apply_recoil(){
        const auto& r=weapon().recoil;
        constexpr double lateral[]={-.4,.6,.8,-.7,1,-.5,.3,-1};
        const double side=lateral[recoil_shots[equipped]++%std::size(lateral)];
        kick=std::min(kick+r.pitch,r.max_pitch);
        kick_yaw=std::clamp(kick_yaw+r.yaw*side,-2*r.yaw,2*r.yaw);
        weapon_kick=std::min(weapon_kick+r.push,.22);
    }
    void equip(int index){if(index<0||index>=int(weapons.size())||index==equipped)return;equipped=index;reload_elapsed=-1;if(!inventory){clear_recoil();shot_flash=0;}}
    void reload(){if(!reloading()&&ammo()<weapon().capacity){reload_elapsed=0;++reloads;}}
    double view_pitch()const{return std::clamp(pitch+kick,-80.0,80.0);}
    double view_yaw()const{return yaw+kick_yaw;}
    bool scoped()const{return scope_view;}
    double fov()const{return zoom_degrees;}
    V3 right()const{return {std::cos(view_yaw()*rad),0,-std::sin(view_yaw()*rad)};}
    V3 up()const{const auto f=forward(),r=right();return {f.y*r.z,f.z*r.x-f.x*r.z,-f.y*r.x};}
    V3 weapon_origin()const{
        return player+right()*(.38+.22*reload_pose())+V3{0,1.42-.18*reload_pose(),0}+
            V3{std::sin(view_yaw()*rad),0,std::cos(view_yaw()*rad)}*(.38-weapon_kick);
    }
    V3 weapon_direction()const{
        // The optic follows the weapon directly. In third person the barrel
        // converges on the reticle, accounting for the shoulder offset.
        if(scoped())return forward();
        const auto to_aim=trace(eye(),forward()).position-weapon_origin();
        return dot(to_aim,forward())>.1?unit(to_aim):forward();
    }
    V3 weapon_point(V3 p)const{
        const double a=-.65*reload_pose(),ca=std::cos(a),sa=std::sin(a);
        const V3 pitched{p.x,p.y*ca+p.z*sa,-p.y*sa+p.z*ca};
        const double cant=.55*reload_pose();
        const V3 local{pitched.x*std::cos(cant)+pitched.z*std::sin(cant),pitched.y,-pitched.x*std::sin(cant)+pitched.z*std::cos(cant)};
        const auto direction=weapon_direction(),side=unit(V3{direction.z,0,-direction.x});
        const V3 up{direction.y*side.z,direction.z*side.x-direction.x*side.z,-direction.y*side.x};
        return weapon_origin()+side*local.x+up*local.y+direction*local.z;
    }
    V3 muzzle()const{return weapon_point({0,.04,weapon().muzzle_z});}

    bool setup(gl_context* c){
        const gl_gameplay_context modes[]={
            {Explore,"Exploration","Look around while exploring.",10},
            {AimStandard,"Aim Standard","Rifle, pistol and shotgun aiming.",20},
            {AimSniper,"Aim Sniper","Aim through the scope.",20},
            {Inventory,"Inventory cursor","Move the cursor in your inventory.",30}
        };
        for(const auto& m:modes){
            if(gl_register_gameplay_context(c,&m)!=GL_OK)return false;
            if(gl_set_gameplay_context_output_target(c,m.id,m.id==Inventory?GL_OUTPUT_CURSOR:GL_OUTPUT_CAMERA)!=GL_OK)return false;
        }
        // Recommended profile, captured before loading the player's INI.
        // Keep broad input preferences: GyroLib adapts them to each controller.
        if(gl_set_context_parent(c,AimStandard,Explore)!=GL_OK||gl_set_context_parent(c,AimSniper,AimStandard)!=GL_OK||
           gl_set_context_parent(c,Inventory,Explore)!=GL_OK)return false;
        for(auto [key,value]:std::initializer_list<std::pair<const char*,double>>{
            {"context.101.gyro.activation",GL_HOLD_DISABLE},{"context.101.activation.button",3},
            {"context.101.activation.block_long_press",1},{"context.101.flick.mode",GL_FLICK_BOTH},
            {"context.101.camera.recenter_button",9},{"context.205.flick.mode",GL_FLICK_OFF},
            {"context.206.sensitivity_x",1},{"context.206.sensitivity_y",1},
            {"context.206.gyro.zoom_compensation",1},
            {"context.309.gyro.space",GL_SPACE_LASER_POINTER}})
            if(gl_setting_set(c,key,value)!=GL_OK)return false;
        if(gl_set_host_capabilities(c,GL_HOST_MENU_STATE|GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION|GL_HOST_LONG_PRESS_BLOCKING)!=GL_OK)return false;
        if(gl_set_gameplay_context_zoom_available(c,AimSniper,1)!=GL_OK)return false;
        gl_set_camera_callback(c,[](void* user,double x,double y){
            auto& self=*static_cast<Host*>(user);++self.camera_callbacks;self.gyro_yaw_total+=x;
            self.rotate(x,y);
        },this);
        gl_set_recenter_step_callback(c,[](void* user,double fraction){auto& h=*static_cast<Host*>(user);h.pitch*=1-fraction;h.clear_recoil();},this);
        if(gl_capture_recommended_settings(c)!=GL_OK)return false; // before the player's INI is loaded
        return true;
    }
    void rotate(double x,double y){camera_yaw_total+=x;yaw=std::remainder(yaw+x,360.0);pitch=std::clamp(pitch+y,-80.0,80.0);}
    uint32_t mode()const{return inventory?Inventory:aiming?(weapon().scope?AimSniper:AimStandard):Explore;}
    V3 forward()const{return {std::sin(view_yaw()*rad)*std::cos(view_pitch()*rad),std::sin(view_pitch()*rad),std::cos(view_yaw()*rad)*std::cos(view_pitch()*rad)};}
    V3 camera_pivot()const{return player+V3{0,1.65,0};}
    V3 eye()const{
        // Orbit around the upper body in both yaw and pitch; never change the
        // look direction to resolve a collision. Menus freeze this whole pose.
        if(scoped())return weapon_point({0,.17,.10});
        const auto offset=right()*(.8-.15*aim_blend)-forward()*(4.2-2.1*aim_blend);
        const double length=std::sqrt(dot(offset,offset));
        const auto direction=offset*(1/length),pivot=camera_pivot();
        return pivot+direction*std::max(0.0,trace_solids(pivot,direction,length,.24)-.015);
    }
    bool show_character()const{
        // Measure proximity to the whole upright body, not just the shoulder:
        // a ground collision can place the camera beside the feet when looking up.
        const auto position=eye();
        const auto nearest=player+V3{0,std::clamp(position.y-player.y,0.0,1.93),0};
        return !scoped()&&dot(position-nearest,position-nearest)>1.25*1.25;
    }
    Impact trace(V3 origin,V3 direction)const{
        double closest=trace_solids(origin,direction);int hit=-1,zone=-1;
        for(size_t index=0;index<targets.size();++index){const auto& target=targets[index];
            if(std::abs(direction.z)<1e-9)continue;
            const double along=(target.position.z-origin.z)/direction.z;
            const auto at=origin+direction*along-target.position;
            const int ring=Target::zone(std::hypot(at.x,at.y));
            if(along>0&&along<closest&&ring>=0){closest=along;hit=int(index);zone=ring;}
        }
        return {origin+direction*closest,hit,zone};
    }
    void fire(){
        if(reloading()||cooldown>0)return;
        if(ammo()<=0){notification="Magazine empty";notification_time=1;return;}
        --magazines[equipped];++shots;last_fired_weapon=equipped;shot_flash=.065;cooldown=weapon().shot_interval;
        const auto origin=eye(),direction=forward(),side=right();
        const auto vertical=up();
        shot_start=muzzle();impact_count=weapon().pellets;last_hit_zone=-1;hit_flash=0;
        // A barrel poking through cover cannot shoot from its far side.
        const auto barrel=shot_start-weapon_origin();
        const auto obstruction=trace(weapon_origin(),unit(barrel));
        const bool blocked=dot(obstruction.position-weapon_origin(),unit(barrel))<std::sqrt(dot(barrel,barrel));
        for(int i=0;i<impact_count;++i){
            auto ray=direction;
            if(i){const double angle=(i-1)*2*std::numbers::pi/(impact_count-1),spread=std::tan(weapon().spread_degrees*rad);
                ray=ray+(side*std::cos(angle)+vertical*std::sin(angle))*spread;ray=ray*(1/std::sqrt(dot(ray,ray)));}
            const auto aim=trace(origin,ray).position-shot_start;
            // The camera only chooses the aim point. Every projectile starts
            // at the muzzle and can hit intervening cover, even in the scope.
            auto& hit=shot_impacts[i];hit=blocked?obstruction:trace(shot_start,dot(aim,ray)>0?unit(aim):ray);
            if(hit.target>=0){targets[hit.target].flash[hit.zone]=.38;
                last_hit_zone=last_hit_zone<0?hit.zone:std::min(last_hit_zone,hit.zone);}
        }
        shot_end=shot_impacts[0].position;
        if(blocked)shot_start=obstruction.position;
        if(last_hit_zone>=0)hit_flash=.18;
        // Apply the impulse after tracing this shot. Later shots use the recoiled
        // camera and barrel; player/gyro input stays additive and unmodified.
        apply_recoil();
    }
    bool report_views(gl_context* c,bool aim){
        return gl_set_gameplay_context_state(c,Explore,!inventory&&!aim,1)==GL_OK&&
            gl_set_gameplay_context_state(c,AimStandard,aim&&!weapon().scope,1)==GL_OK&&
            gl_set_gameplay_context_state(c,AimSniper,aim&&weapon().scope,1)==GL_OK&&
            gl_set_gameplay_context_state(c,Inventory,inventory,1)==GL_OK;
    }
    bool step(gl_context* c,uint64_t now,double dt,const Input& raw){
        Input in=raw;
        const bool settings=gl_panel_open(c)!=0;
        // Observe the current aim command before filtering ordinary game
        // actions. The physical button state still feeds acquisition unchanged.
        if(!report_views(c,in.aim&&!inventory&&!paused&&!settings&&in.focused))return false;
        const auto buttons=controller_actions.update(c,now,in.controller_id,in.controller_buttons,in.focused,
            in.native_hold||inventory||paused||settings||in.pause_press||in.inventory_press);
        in.reload_press|=(buttons&(1u<<ButtonWest))!=0;
        in.inventory_press|=(buttons&(1u<<ButtonNorth))!=0;
        in.confirm_press|=(buttons&(1u<<ButtonSouth))!=0;
        controller_confirm=(buttons&(1u<<ButtonSouth))!=0;
        const bool menu_chord=gl_get_gamepad_menu_shortcut(c)&&(in.controller_buttons&(1u<<ButtonBack));
        in.pause_press|=((buttons&(1u<<ButtonStart))!=0&&!menu_chord)||
            (paused&&!settings&&(buttons&(1u<<ButtonEast))!=0);
        if(in.focused&&!settings){
            if(in.pause_press){
                if(inventory)inventory=false;
                else if(paused&&gyro_menu)gyro_menu=false;
                else {paused=!paused;gyro_menu=false;}
            }
            if(in.inventory_press&&!paused){inventory=!inventory;hovered=-1;}
        }
        aiming=in.aim&&!inventory&&!paused&&!settings&&in.focused;
        // Report every mode on every update. No animation decides these states.
        if(!report_views(c,aiming))return false;
        gl_host_state host{};host.focused=in.focused;host.menu_open=inventory||paused;
        host.camera_allowed=!inventory&&!paused;host.paused=paused;host.suspend_long_press_blocking=in.native_hold;
        if(!inventory&&!paused&&!settings&&in.focused){
            if(reloading()){reload_elapsed+=dt;if(reload_elapsed>=weapon().reload_seconds){magazines[equipped]=weapon().capacity;reload_elapsed=-1;}}
            if(in.reload_press)reload();
            const bool was_scoped=scoped();scope_view=aiming&&weapon().scope&&!reloading();
            if(scoped()&&(in.zoom_press||(buttons&(1u<<ButtonSouth))))sniper_zoom=1-sniper_zoom;
            const bool shoulder_aim=aiming&&!weapon().scope;
            aim_blend+=(shoulder_aim?1.0-aim_blend:-aim_blend)*-std::expm1(-dt/.13);
            const double target=scoped()?sniper_fovs[sniper_zoom]:shoulder_aim?50.0:68.0;
            if(scoped()||was_scoped)zoom_degrees=target;
            else zoom_degrees+=(target-zoom_degrees)*-std::expm1(-dt/.13);
        }
        if(in.recenter_press)gl_request_recenter(c);
        // The base scope is the sensitivity reference for this view. Report the
        // displayed FOV while scoped; reloading in third person uses no zoom scale.
        if(scoped()&&aiming&&gl_set_gameplay_context_fov(c,AimSniper,fov(),sniper_fovs[0])!=GL_OK)return false;
        const auto gyro_begin=measure_performance?performance_clock():0;
        const auto gyro_result=gl_update(c,now,&host,&output);
        if(measure_performance)gyro_update_ns=performance_clock()-gyro_begin;
        if(gyro_result!=GL_OK)return false;
        if(!in.focused||settings||gl_panel_open(c)||paused)return true;
        if(inventory){
            // Processed angular deltas -> normalized local UI coordinates.
            // Never move the OS cursor; no camera callback runs in cursor mode.
            if(in.pointer_moved){cursor_x=in.pointer_x;cursor_y=in.pointer_y;}
            cursor_x=std::clamp(cursor_x+output.yaw_degrees/90+in.look_x*dt*.7,0.0,1.0);
            cursor_y=std::clamp(cursor_y-output.pitch_degrees/60-in.look_y*dt*.7,0.0,1.0);
        }else{
            clock+=dt;shot_flash=std::max(0.0,shot_flash-dt);hit_flash=std::max(0.0,hit_flash-dt);
            cooldown=std::max(0.0,cooldown-dt);recover_recoil(dt);
            notification_time=std::max(0.0,notification_time-dt);
            for(auto& target:targets){for(auto& flash:target.flash)flash=std::max(0.0,flash-dt);
                target.update(clock);}
            mouse_yaw_total+=in.mouse_yaw;rotate(in.mouse_yaw,in.mouse_pitch);
            if(!output.suppress_native_right_stick){
                double x=in.look_x,y=in.look_y;
                if(gl_suppress_native_right_touchpad(c)){
                    // Steam may map the pad to this virtual look axis. Preserve
                    // native physical-stick camera control in Touchpad mode.
                    const auto axis=[](float value){return std::abs(value)<.16?0.0:std::copysign((std::abs(value)-.16)/.84,value);};
                    gl_flick_input physical{};gl_get_flick_input(c,&physical);
                    x=in.look_x-in.controller_look_x+axis(physical.stick_x);
                    y=in.look_y-in.controller_look_y+axis(physical.stick_y);
                }
                native_yaw_total+=x*dt*130;rotate(x*dt*130,y*dt*100);
            }
            const double length=std::max(1.0,std::hypot(in.move_x,in.move_z));
            const double speed=aiming?2.5:5.5;
            V3 movement{(in.move_x*std::cos(yaw*rad)+in.move_z*std::sin(yaw*rad))/length,0,
                (-in.move_x*std::sin(yaw*rad)+in.move_z*std::cos(yaw*rad))/length};
            player=player+movement*(speed*dt);player.x=std::clamp(player.x,-12.0,12.0);player.z=std::clamp(player.z,range_start+2,range_end-4);
            walk+=std::hypot(movement.x,movement.z)*dt*8;
            if(in.fire_press||(weapon().automatic&&in.fire_held))fire();
        }
        return true;
    }
};
}
