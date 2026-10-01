#include "../examples/tps/host.hpp"
#include <gyrolib/gyrolib.hpp>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition,const char* message){if(!condition){std::cerr<<message<<'\n';std::exit(1);}}
struct Game {
    gyrolib::Context context;
    tps::Host host;uint64_t now=1000000000;
    Game(){require(context&&host.setup(context.get()),"demo setup failed");}
    void tick(double dt=.016,const tps::Input& input={}){now+=uint64_t(dt*1e9);require(host.step(context.get(),now,dt,input),"demo step failed");}
};
void reload_and_weapons(){
    Game game;auto& h=game.host;
    require(tps::weapons.size()==4&&h.ammo()==30,"inventory must contain four weapons");
    h.reload();require(!h.reloading(),"a full magazine should not reload");
    h.fire();require(h.ammo()==29&&h.shots==1&&h.shot_flash>0&&h.kick>0,"shot must consume ammo and produce feedback");
    h.fire();require(h.shots==1,"shot interval must prevent duplicate shots");
    h.reload();require(h.reloading()&&h.ammo()==29&&h.reload_progress()==0,"reload should start without refilling");
    game.tick(.7);require(h.reload_progress()>.4&&h.reload_pose()>.9,"reload should animate through an intermediate pose");
    require(h.weapon_point({0,0,.6}).y<1.3,"reload must visibly lower the weapon");
    h.fire();require(h.shots==1,"reloading must block firing");
    const auto progress=h.reload_progress();
    tps::Input in{};in.focused=false;game.tick(.5,in);
    require(h.reload_progress()==progress,"focus loss must freeze reload");
    in={};in.inventory_press=true;game.tick(.5,in);game.tick(.5);
    require(h.reload_progress()==progress,"inventory must freeze reload");
    game.tick(0,in);in={};in.pause_press=true;game.tick(.5,in);game.tick(.5);
    require(h.reload_progress()==progress,"pause must freeze reload");
    game.tick(0,in);gl_set_panel_open(game.context.get(),1);game.tick(.5);
    require(h.reload_progress()==progress,"settings must freeze reload");
    gl_set_panel_open(game.context.get(),0);game.tick(.9);
    require(h.reloading()&&h.ammo()==29,"ammo cannot refill before animation completes");
    game.tick(.06);require(!h.reloading()&&h.ammo()==30&&h.reload_pose()==0,"completed reload must refill and restore pose");
    h.fire();h.reload();h.equip(1);
    require(h.equipped==1&&h.ammo()==5&&!h.reloading(),"weapon change must cancel reload");
    h.equip(0);require(h.ammo()==29,"weapon change cannot refill old magazine");
    h.equip(-1);h.equip(4);require(h.equipped==0,"invalid inventory selections must be ignored");
    h.equip(1);game.tick(.2);h.fire();h.reload();game.tick(1.7);
    require(h.ammo()==4&&h.reloading(),"Sniper uses its own reload duration");
    game.tick(.51);require(h.ammo()==5&&!h.reloading(),"Sniper reload must finish");
}
void aiming_and_trigger(){
    Game game;auto& h=game.host;tps::Input input{};input.aim=true;game.tick(.016,input);
    require(h.mode()==tps::AimStandard,"Rifle aim must activate on command");
    h.equip(1);game.tick(.016,input);require(h.mode()==tps::AimSniper,"Sniper aim must use its own context immediately");
    input.aim=false;game.tick(.016,input);require(h.mode()==tps::Explore,"aim release cannot wait for zoom animation");
    input.fire_held=true;
    const auto shots=h.shots;game.tick(1,input);require(h.shots==shots,"Sniper should fire on a trigger press only");
    input.fire_press=true;game.tick(.016,input);input.fire_press=false;game.tick(1,input);
    require(h.shots==shots+1,"held Sniper trigger must not repeat");
    h.equip(0);game.tick(.2,input);game.tick(.14,input);require(h.shots==shots+3,"Rifle should repeat while trigger is held");
    input.inventory_press=true;game.tick(.5,input);require(h.shots==shots+3,"inventory must block all firing");
}
void controller_short_press(){
    for(auto mode:{GL_HOLD,GL_HOLD_DISABLE})for(uint64_t duration:{50000000ull,199999999ull,200000000ull,350000000ull}){
        Game game;auto* c=game.context.get();auto& h=game.host;
        gl_endpoint e{};e.id=e.physical_id=1;e.connected=1;e.source=GL_SOURCE_SDL;
        e.caps.buttons=0xffffffffu;e.caps.gyro=e.caps.accelerometer=1;gl_register_endpoint(c,&e);
        for(auto id:{tps::Explore,tps::AimStandard}){
            const auto prefix="context."+std::to_string(id)+".";
            gl_setting_set(c,(prefix+"gyro.activation").c_str(),mode);
            gl_setting_set(c,(prefix+"activation.button").c_str(),tps::ButtonWest+1);
            gl_setting_set(c,(prefix+"activation.short_press").c_str(),1);
        }
        tps::Input in{};in.controller_id=1;
        const auto tick=[&](uint64_t elapsed=10000000){
            game.now+=elapsed;gl_sample sample{game.now,game.now,{0,30,0},{0,1,0}};
            gl_controls controls{};controls.timestamp_ns=game.now;controls.buttons=in.controller_buttons;
            gl_submit_sample(c,1,&sample);gl_submit_controls(c,1,&controls);
            require(h.step(c,game.now,elapsed*1e-9,in),"filtered controller step failed");
        };
        for(int n=0;n<4;++n)tick();h.fire();
        in.controller_buttons=1u<<tps::ButtonWest;tick();
        require(!h.reloading(),"filtered reload must wait for release");
        require(bool(h.output.gyro_active)==(mode==GL_HOLD),"hold gyro must respond on the first press frame");
        uint64_t remaining=duration;
        while(remaining>10000000){tick();remaining-=10000000;}
        in.controller_buttons=0;tick(remaining);
        require(h.reloading()==(duration<200000000),"only a short controller press may reload, strictly below 200 ms");
        require(bool(h.output.gyro_active)==(mode==GL_HOLD_DISABLE),"gyro must respond immediately on release");
        h.reload_elapsed=-1;
        // A new press on the same frame as an aim transition belongs to the
        // newly observed view; it must not be cancelled by the camera update.
        in.aim=true;in.controller_buttons=1u<<tps::ButtonWest;tick();
        require(!h.reloading(),"new aim profile must also defer the reload");
        in.controller_buttons=0;tick(50000000);require(h.reloading(),"tap in a newly selected view was lost");
        h.reload_elapsed=-1;in.controller_buttons=1u<<tps::ButtonWest;tick();
        in.focused=false;tick();in.focused=true;in.controller_buttons=0;tick(50000000);
        require(!h.reloading(),"focus loss must cancel a pending reload tap");
        in.native_hold=true;in.controller_buttons=1u<<tps::ButtonWest;tick();
        require(h.reloading(),"native-hold override must preserve the original game press");
        h.reload_elapsed=-1;in.controller_buttons=0;tick(50000000);
        require(!h.reloading(),"a forwarded hold must not create a second action on release");
        in.native_hold=false;tick();
        gl_setting_set(c,"context.205.activation.short_press",0);
        in.controller_buttons=1u<<tps::ButtonWest;tick();require(h.reloading(),"unchecked filter must reload immediately on press");
        h.reload_elapsed=-1;in.controller_buttons=0;tick();
        gl_setting_set(c,"context.205.activation.short_press",1);gl_setting_set(c,"context.205.gyro.activation",GL_TOGGLE);
        in.controller_buttons=1u<<tps::ButtonWest;tick();require(h.reloading(),"saved hold filter must stay inactive for Toggle");
    }
}
void target_zones(){
    const double offsets[]={0,.35,.60,.72};
    for(int zone=0;zone<4;++zone){
        Game game;auto& h=game.host;h.pitch=0;
        for(auto& target:h.targets)target.position={30,30,40};
        const auto eye=h.eye();h.targets[0].position=eye+tps::V3{offsets[zone],0,12};h.fire();
        if(zone<3){
            require(h.last_hit_zone==zone&&h.hit_flash>0,"hit must identify the actual concentric zone");
            for(int other=0;other<3;++other)require((h.targets[0].flash[other]>0)==(other==zone),"only the struck target zone should light up");
        }else require(h.last_hit_zone==-1&&h.hit_flash==0,"outside target radius must miss");
        game.tick(.5);require(h.shot_flash==0&&h.hit_flash==0&&h.targets[0].flash==std::array<double,3>{},"feedback must decay with elapsed time");
    }
    Game game;auto& h=game.host;
    const auto initial=h.targets[2].position.x;game.tick(.5);
    require(std::abs(h.targets[2].position.x-initial)>.5,"one target should move");
    require(h.targets[0].position.x==-4&&h.targets[1].position.x==.8,"other targets must stay still");
    h.player={-6.85,0,0};h.targets[0].position={-6,.72,8};h.pitch=std::atan2(.72-2.6,14)/tps::rad;
    h.fire();require(h.last_hit_zone==-1&&h.shot_end.z<8,"cover must stop shots before the target");
}
void sniper_scope(){
    Game game;auto& h=game.host;h.equip(1);h.pitch=0;
    tps::Input aim{};aim.aim=true;game.tick(.016,aim);
    require(h.scoped()&&h.mode()==tps::AimSniper&&h.fov()==20,"Sniper must enter the scoped camera on aim command");
    const auto eye=h.eye();
    require(eye.z>h.player.z&&eye.z<h.player.z+1&&eye.y>1.4&&eye.y<1.8,"scope camera must be at the optic, not behind the character");
    for(auto& target:h.targets)target.position={30,30,40};
    h.targets[0].position=eye+h.forward()*12;h.fire();
    require(h.last_hit_zone==0&&h.hit_flash>0,"scoped shots must converge on the rendered center ray");
    require(std::abs(h.shot_end.x-h.targets[0].position.x)<1e-8&&std::abs(h.shot_end.y-h.targets[0].position.y)<1e-8,"scope and hit testing must agree on impact");
    const auto frozen=h.eye();const auto pitch=h.view_pitch();
    tps::Input input=aim;input.inventory_press=true;game.tick(.3,input);game.tick(.3,aim);
    require(h.scoped()&&h.eye().x==frozen.x&&h.eye().y==frozen.y&&h.eye().z==frozen.z&&h.view_pitch()==pitch,"inventory must freeze the scoped camera including recoil");
    game.tick(0,input);input=aim;input.focused=false;game.tick(.3,input);
    require(h.scoped()&&h.eye().z==frozen.z&&h.view_pitch()==pitch,"focus loss must not move the scoped camera");
    gl_set_panel_open(game.context.get(),1);game.tick(.3,aim);
    require(h.scoped()&&h.eye().z==frozen.z,"F10 must preserve the scoped background");
    gl_set_panel_open(game.context.get(),0);
    input=aim;input.reload_press=true;game.tick(.016,input);
    require(h.reloading()&&!h.scoped()&&h.mode()==tps::AimSniper,"reload must reveal its animation without changing the observed aim profile");
    require(h.eye().z<h.player.z&&h.fov()==68,"reload must restore the third-person camera and projection together");
    game.tick(2.21,aim);require(h.scoped()&&!h.reloading(),"held aim must restore the scope after reload");
    game.tick(.016);require(!h.scoped()&&h.mode()==tps::Explore&&h.fov()==68,"release must leave scope and its profile immediately");
    h.equip(0);game.tick(.016,aim);require(!h.scoped()&&h.mode()==tps::AimStandard,"Rifle must keep shoulder aiming");
}
void shared_standard_profile(){
    Game game;auto* c=game.context.get();tps::Input aim{};aim.aim=true;
    require(gl_menu_tab_count(c)==4,"adding weapons must not add aim tabs");
    gl_endpoint e{};e.id=e.physical_id=444;e.source=GL_SOURCE_SDL;e.connected=1;e.caps.gyro=e.caps.accelerometer=1;
    require(gl_register_endpoint(c,&e)==GL_OK&&gl_select_device(c,e.id)==GL_OK,"synthetic gyro setup failed");
    require(gl_setting_set(c,"context.205.sensitivity_x",3.5)==GL_OK&&gl_setting_set(c,"context.205.gyro.space",GL_SPACE_LOCAL_YAW)==GL_OK,"shared profile setting failed");
    require(gl_setting_set(c,"context.206.gyro.space",GL_SPACE_LOCAL_YAW)==GL_OK,"scope profile setting failed");
    const auto tick=[&]{const auto time=game.now+16000000;gl_sample sample{time,time,{0,-12,0},{0,1,0}};
        require(gl_submit_sample(c,444,&sample)==GL_OK,"sample rejected");game.tick(.016,aim);};
    for(int i=0;i<12;++i)tick();
    for(int weapon:{tps::Rifle,tps::Pistol,tps::Shotgun}){
        game.host.equip(weapon);tick();
        require(gl_get_active_gameplay_context(c)==205&&!game.host.scoped(),"all three standard weapons must activate the same real library profile");
        require(std::abs(game.host.output.yaw_degrees-.672)<.0001,"shared sensitivity edit must immediately apply to Rifle, Pistol and Shotgun");
    }
    game.host.equip(tps::Sniper);tick();
    require(gl_get_active_gameplay_context(c)==206&&game.host.scoped()&&std::abs(game.host.output.yaw_degrees-.096)<.0001,"Sniper must keep its independent gain and scope");
}
void pistol_and_shotgun(){
    for(int weapon:{tps::Pistol,tps::Shotgun}){
        Game game;auto& h=game.host;h.equip(weapon);const auto capacity=h.ammo();
        tps::Input in{};in.fire_held=true;game.tick(.3,in);
        require(h.shots==0,"Pistol and Shotgun require a fresh trigger press");
        in.fire_press=true;game.tick(.016,in);in.fire_press=false;
        require(h.shots==1&&h.ammo()==capacity-1,"one press must spend one round");
        game.tick(.30,in);if(weapon==tps::Shotgun)require(h.pump_pose()>.9,"shotgun fore-end should cycle after firing");
        game.tick(1,in);require(h.shots==1&&h.pump_pose()==0,"holding trigger must not repeat a semi-auto or pump shot");
        h.equip(tps::Rifle);h.equip(weapon);require(h.ammo()==capacity-1,"each added weapon must retain its own ammunition");
        h.reload();game.tick(h.weapon().reload_seconds-.01);
        require(h.reloading()&&h.ammo()==capacity-1,"new weapon reload must wait for its animation");
        game.tick(.02);require(!h.reloading()&&h.ammo()==capacity,"new weapon reload must refill");
    }
    Game blast;auto& h=blast.host;h.equip(tps::Shotgun);h.pitch=0;
    for(auto& target:h.targets)target.position={30,30,40};
    h.targets[0].position=h.eye()+tps::V3{0,0,10};h.fire();
    require(h.impact_count==9&&h.last_hit_zone==0&&h.shots==1&&h.ammo()==5,"shotgun blast must cast nine rays but consume one round");
    require(h.targets[0].flash[0]>0&&h.targets[0].flash[1]>0&&h.targets[0].flash[2]==0,"pellets should illuminate all and only the zones they hit");
    for(int weapon:{tps::Pistol,tps::Shotgun}){
        Game edge;auto& gun=edge.host;gun.equip(weapon);gun.pitch=0;
        for(auto& target:gun.targets)target.position={30,30,40};
        gun.targets[0].position=gun.eye()+tps::V3{.95,0,20};gun.fire();
        require(gun.shot_impacts[0].zone==-1,"center ray should miss the offset target");
        require((gun.hit_flash>0)==(weapon==tps::Shotgun),"spread should hit an offset target that a pistol's single ray misses");
    }
}
void camera_orbit(){
    Game game;auto& h=game.host;
    for(double aim:{0.,1.})for(double yaw:{-180.,-90.,0.,90.,179.}){
        h.aim_blend=aim;h.yaw=yaw;
        for(double pitch:{-80.,-60.,0.,60.,80.}){
            h.pitch=pitch;const auto eye=h.eye(),forward=h.forward(),pivot=h.camera_pivot();
            require(std::isfinite(eye.y)&&eye.y>.22,"camera must stay above the floor at all pitch limits");
            require(std::abs(tps::dot(forward,forward)-1)<1e-9&&std::abs(tps::dot(forward,h.right()))<1e-9,"camera basis cannot flip or degenerate");
            require(tps::dot(pivot-eye,forward)>0,"camera must keep the pivot ahead while orbiting");
            if(pitch<0)require(eye.y>pivot.y,"looking down must raise the boom around the body");
            if(pitch>0)require(eye.y<pivot.y,"looking up must lower the boom around the body");
            if(pitch==80)require(!h.show_character(),"ground-compressed camera must hide the nearby body to keep the reticle clear");
        }
    }
    h.pitch=0;h.yaw=90;h.player={-11.5,0,5};h.aim_blend=0;
    require(h.eye().x>-12.16,"pillar behind the character must shorten the camera boom");
    h.player={-6,0,8};h.yaw=0;h.pitch=35;
    require(h.eye().z>6.95,"camera must stop before a crate behind the character");
    h.rotate(0,1000);h.kick=10;require(h.view_pitch()==80,"pitch plus recoil cannot overturn the camera");
    h.rotate(0,-2000);require(h.pitch==-80,"downward camera pitch must remain bounded");
}
void muzzle_shots(){
    for(int weapon:{tps::Rifle,tps::Sniper,tps::Pistol,tps::Shotgun}){
        Game game;auto& h=game.host;h.equip(weapon);h.pitch=0;
        for(auto& target:h.targets)target.position={30,30,40};
        h.targets[0].position=h.eye()+h.forward()*8;
        const auto muzzle=h.muzzle();h.fire();
        require(tps::dot(h.shot_start-muzzle,h.shot_start-muzzle)<1e-10,"visible projectile must start at the weapon muzzle");
        require(h.shot_impacts[0].zone==0&&tps::dot(h.shot_end-h.targets[0].position,h.shot_end-h.targets[0].position)<1e-10,"muzzle ray must converge on a close reticle target");
    }
    // The reticle sees over the crate but the lower, left-offset muzzle cannot.
    Game cover;auto& h=cover.host;h.player={-7.4,0,3.9};h.pitch=0;
    for(auto& target:h.targets)target.position={30,30,40};
    h.targets[0].position=h.eye()+h.forward()*14;
    // Lift the camera aim point while lowering the barrel behind the crate.
    h.pitch=-7;
    h.targets[0].position=h.eye()+h.forward()*14;
    require(h.trace(h.eye(),h.forward()).target==0,"cover fixture must expose the target to the camera");
    h.fire();
    require(h.shot_impacts[0].target==-1&&h.shot_end.z<8,"muzzle-side cover must block a camera-visible target");
    // The barrel itself crosses a wall: no shot may originate on its far side.
    Game wall;auto& w=wall.host;w.player={0,0,42};w.pitch=0;w.targets[0].position={.8,1.65,47};
    w.fire();require(w.shot_end.z<=42.5+1e-9,"a protruding barrel must not bypass the wall");
}
void calibration_menus(){
    Game game;auto& h=game.host;auto* c=game.context.get();
    gl_endpoint e{};e.id=e.physical_id=777;e.source=GL_SOURCE_SDL;e.connected=1;e.caps.gyro=e.caps.accelerometer=1;
    require(gl_register_endpoint(c,&e)==GL_OK,"calibration test sensor failed");
    gl_setting_set(c,"calibration.automatic",GL_CAL_MENUS);
    const auto tick=[&](const tps::Input& input=tps::Input{}){
        const auto now=game.now+10000000;gl_sample sample{now,now,{0,.5f,0},{0,1,0}};
        gl_submit_sample(c,777,&sample);game.tick(.01,input);
    };
    tps::Input in{};in.inventory_press=true;tick(in);const auto cursor=h.cursor_x;
    for(int i=0;i<700;++i)tick();
    gl_diagnostics d{};gl_get_diagnostics(c,&d);
    require(h.inventory&&h.output.gyro_active&&h.cursor_x!=cursor&&d.bias.y==0,"inventory must keep cursor gyro without menu-only calibration");
    // F10 suspends the inventory cursor and is an eligible calibration screen.
    gl_set_panel_open(c,1);for(int i=0;i<500;++i)tick();gl_get_diagnostics(c,&d);
    require(d.bias.y>.3&&!h.output.gyro_active,"F10 may calibrate while cursor output is suspended");
    gl_set_panel_open(c,0);const auto bias=d.bias.y;for(int i=0;i<300;++i)tick();gl_get_diagnostics(c,&d);
    require(d.bias.y==bias,"closing F10 onto inventory must stop bias updates again");
    in={};in.pause_press=true;tick(in);tick(in);
    require(h.paused&&!h.inventory,"pause must follow closing inventory");
    h.gyro_menu=true;const auto yaw=h.yaw,clock=h.clock;
    for(int i=0;i<400;++i)tick();gl_get_diagnostics(c,&d);
    require(d.bias.y>bias&&!h.output.gyro_active&&h.yaw==yaw&&h.clock==clock,"native gyro menu must pause gameplay and allow menu-only calibration");
    tick(in);require(h.paused&&!h.gyro_menu,"back from gyro settings must return to pause");
    tick(in);require(!h.paused,"back from pause must resume gameplay");
}
}
int main(){reload_and_weapons();aiming_and_trigger();controller_short_press();target_zones();sniper_scope();shared_standard_profile();pistol_and_shotgun();camera_orbit();muzzle_shots();calibration_menus();
    std::cout<<"Demo combat: weapons, profiles, scope, reload, spread, camera orbit, muzzle traces and cover passed\n";}
