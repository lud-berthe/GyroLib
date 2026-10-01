#include <gyrolib/gyrolib.hpp>
#include "../src/detail/flick.hpp"
#include "../src/detail/projection.hpp"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x);}while(0)
static void near(double a,double b,double tolerance=1e-5){
    if(!std::isfinite(a)||std::abs(a-b)>=tolerance)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a)+", tolerance "+std::to_string(tolerance));
}
struct Fixture {
    gyrolib::Context owner;gl_context* c=owner.get();
    uint64_t now=1000000000;gl_controls controls{};gl_host_state host{};gl_output out{};
    explicit Fixture(gl_vec3 initial_accel={0,1,0}){
        gl_gameplay_context view{1,"Camera","",0};CHECK(gl_register_gameplay_context(c,&view)==GL_OK);
        set("gyro.space",GL_SPACE_LOCAL_YAW);set("sensitivity_x",1);set("sensitivity_y",1);
        gl_endpoint e{};e.id=e.physical_id=1;e.connected=1;e.source=GL_SOURCE_SDL;e.caps.gyro=e.caps.accelerometer=1;
        e.caps.buttons=0xffffffffu;e.caps.sticks=3;CHECK(gl_register_endpoint(c,&e)==GL_OK);
        host.focused=host.camera_allowed=1;
        for(int i=0;i<3;++i)tick({},10000000,initial_accel);
    }
    void set(const char* suffix,double v){CHECK(gl_setting_set(c,(std::string("context.1.")+suffix).c_str(),v)==GL_OK);}
    gl_output tick(gl_vec3 rate={},uint64_t step=10000000,gl_vec3 accel={0,1,0}){
        now+=step;gl_sample s{now,now,rate,accel};CHECK(gl_submit_sample(c,1,&s)==GL_OK);
        controls.timestamp_ns=now;CHECK(gl_submit_controls(c,1,&controls)==GL_OK);
        CHECK(gl_set_gameplay_context_state(c,1,1,1)==GL_OK);CHECK(gl_update(c,now,&host,&out)==GL_OK);return out;
    }
    gl_setting_info info(const char* suffix){
        const auto key=std::string("context.1.")+suffix;
        for(uint32_t n=0;n<gl_menu_tab_setting_count(c,2);++n){gl_setting_info s{};gl_menu_tab_setting_at(c,2,n,&s);if(s.id==key)return s;}
        throw std::runtime_error("Missing setting");
    }
};
static void adaptive_smoothing(){
    for(uint64_t dt:{1000000ull,4000000ull,10000000ull,20000000ull}){
        Fixture f;f.set("gyro.smoothing_ms",100);double sum=0;
        for(uint64_t t=0;t<1000000000;t+=dt)sum+=f.tick({0,2,0},dt).yaw_degrees;
        near(sum,-2*(1-.1*(1-std::exp(-10))));
        for(uint64_t t=0;t<2000000000;t+=dt)sum+=f.tick({},dt).yaw_degrees;
        near(sum,-2); // all slow movement is eventually delivered, at any report rate
    }
    Fixture fast;fast.set("gyro.smoothing_ms",500);near(fast.tick({0,100,0}).yaw_degrees,-1);
    Fixture transition;transition.set("gyro.smoothing_ms",100);transition.set("gyro.smoothing_threshold_dps",10);
    double sum=transition.tick({0,7.5f,0}).yaw_degrees;CHECK(sum<-.0375&&sum>-.075);
    for(int i=0;i<300;++i)sum+=transition.tick().yaw_degrees;near(sum,-.075);
    Fixture variable;variable.set("gyro.smoothing_ms",100);
    double seconds=0,total=0;
    for(int i=0;i<100;++i){uint64_t dt=i%2?17000000:3000000;seconds+=dt*1e-9;total+=variable.tick({0,2,0},dt).yaw_degrees;}
    near(total,-2*(seconds-.1*(1-std::exp(-seconds/.1))));
    variable.set("gyro.smoothing_ms",0);near(variable.tick({0,2,0}).yaw_degrees,-.02);
}
static void world_gravity_direction(){
    for(uint64_t step:{1000000ull,4000000ull,10000000ull,20000000ull}){
        Fixture f;f.set("gyro.space",GL_SPACE_WORLD);f.set("sensitivity_x",6);
        double actual=0,projected=0,actual_pitch=0,projected_pitch=0,min_length=1;
        for(uint64_t time=0;time<6000000000ull;time+=step){
            // Translation perturbs acceleration while fusion continues to track
            // gravity. World yaw must use its DIRECTION, not its temporary length.
            const auto seconds=time*1e-9;
            const gl_vec3 accel{float(.4*std::sin(seconds*7)),1,float(.2*std::cos(seconds*5))};
            const auto out=f.tick({0,90,0},step,accel);gl_diagnostics d{};
            CHECK(gl_get_diagnostics(f.c,&d)==GL_OK);CHECK(out.gyro_active);
            const auto length=std::hypot(d.gravity.x,d.gravity.y,d.gravity.z);
            min_length=std::min(min_length,double(length));
            actual+=out.yaw_degrees;
            const double along_up=-(d.calibrated_dps.x*d.gravity.x+d.calibrated_dps.y*d.gravity.y+d.calibrated_dps.z*d.gravity.z)/length;
            projected-=along_up*step*1e-9*6;
            const double ux=d.gravity.x/length,uy=d.gravity.y/length,uz=d.gravity.z/length;
            CHECK(std::max(std::abs(uy),std::abs(uz))>.25); // outside the side-held reduction cone
            const double px=1-ux*ux,py=-ux*uy,pz=-ux*uz;
            const double along_pitch=(d.calibrated_dps.x*px+d.calibrated_dps.y*py+d.calibrated_dps.z*pz)/std::hypot(px,py,pz);
            actual_pitch+=out.pitch_degrees;projected_pitch+=along_pitch*step*1e-9;
        }
        CHECK(min_length<.999); // the regression must exercise a non-unit fusion vector
        if(std::abs(actual-projected)>.005)throw std::runtime_error("World yaw depends on gravity length: got "+std::to_string(actual)+", expected "+std::to_string(projected));
        near(actual_pitch,projected_pitch,.005);
    }
}
static void tightening_and_acceleration(){
    Fixture f;f.set("gyro.tightening_dps",10);auto out=f.tick({3,4,0});near(out.yaw_degrees,-.02);near(out.pitch_degrees,.015);
    CHECK(f.tick({0,.001f,0}).yaw_degrees<0); // no cutoff
    f.set("gyro.tightening_dps",0);f.set("gyro.acceleration",4);
    f.set("gyro.fast_sensitivity_x",5);f.set("gyro.fast_sensitivity_y",8);
    f.set("sensitivity_y",2);f.set("gyro.slow_threshold_dps",10);f.set("gyro.fast_threshold_dps",50);
    near(f.tick({0,30,0}).yaw_degrees,-.9);near(f.tick({30,0,0}).pitch_degrees,1.5);
    f.set("gyro.fast_threshold_dps",10);near(f.tick({0,10,0}).yaw_degrees,-.1);near(f.tick({0,30,0}).yaw_degrees,-1.5);
    Fixture filtered;filtered.set("gyro.smoothing_ms",100);filtered.set("gyro.smoothing_threshold_dps",100);filtered.set("gyro.acceleration",3);
    // At first sample the filtered speed is <5 dps, so the raw 30 dps cannot amplify it.
    near(filtered.tick({0,30,0}).yaw_degrees,-30*(.01-.1*(1-std::exp(-.1))));
}
static void acceleration_presets(){
    constexpr double gain[]={1,1.5,2,3};
    for(int preset=0;preset<4;++preset){
        Fixture f;f.set("sensitivity_x",2.5);f.set("sensitivity_y",1.5);f.set("gyro.acceleration",preset);
        near(f.info("gyro.fast_sensitivity_x").value,2.5*gain[preset]);
        near(f.info("gyro.fast_sensitivity_y").value,1.5*gain[preset]);
        CHECK(bool(f.info("gyro.fast_sensitivity_x").visible)==(preset!=0));
        near(f.tick({0,5,0}).yaw_degrees,-.05*2.5);
        near(f.tick({0,75,0}).yaw_degrees,-.75*2.5*gain[preset]);
        near(f.tick({75,0,0}).pitch_degrees,.75*1.5*gain[preset]);
        f.set("sensitivity_x",3.2);near(f.info("gyro.fast_sensitivity_x").value,3.2*gain[preset]);
        near(f.info("gyro.acceleration").value,preset);
        f.set("gyro.smoothing_ms",30);near(f.info("gyro.acceleration").value,preset);
        f.set("gyro.fast_sensitivity_x",f.info("gyro.fast_sensitivity_x").value);near(f.info("gyro.acceleration").value,preset);
        f.set("gyro.fast_sensitivity_x",8);near(f.info("gyro.acceleration").value,4);
        for(int mode:{preset,4}){
            f.set("gyro.acceleration",mode);
            unsigned offered=0;
            for(uint32_t n=0;n<gl_menu_choice_count(f.c,"context.1.gyro.acceleration");++n){gl_choice choice{};
                CHECK(gl_choice_at(f.c,"context.1.gyro.acceleration",n,&choice)==GL_OK);
                if(choice.available){CHECK(choice.value<4);++offered;}
                if(choice.value==4){CHECK(!choice.available);CHECK(!std::strcmp(choice.label,gl_text(f.c,"custom")));}
            }
            CHECK(offered==4);
        }
        f.set("gyro.acceleration",preset);near(f.info("gyro.fast_sensitivity_x").value,3.2*gain[preset]);
        for(const auto* key:{"gyro.fast_sensitivity_y","gyro.slow_threshold_dps","gyro.fast_threshold_dps"}){
            f.set("gyro.acceleration",preset);f.set(key,f.info(key).value+f.info(key).step);near(f.info("gyro.acceleration").value,4);
        }
    }
    const auto file=std::filesystem::temp_directory_path()/"gyrolib-preset-migration.ini";
    Fixture f;
    {std::ofstream out(file);out<<"schema=11\ncontext.1.sensitivity_x=20\ncontext.1.sensitivity_y=2.5\ncontext.1.gyro.acceleration=3\ncontext.1.gyro.fast_sensitivity_x=5\ncontext.1.gyro.fast_threshold_dps=20\nfuture.preference=keep\n";}
    CHECK(gl_load_settings(f.c,file.string().c_str())==GL_OK);
    near(f.info("gyro.fast_sensitivity_x").value,60);near(f.info("gyro.fast_threshold_dps").value,75);
    near(f.tick({0,75,0}).yaw_degrees,-45);
    CHECK(gl_save_settings(f.c,file.string().c_str())==GL_OK);
    Fixture restored;CHECK(gl_load_settings(restored.c,file.string().c_str())==GL_OK);
    near(restored.info("gyro.acceleration").value,3);near(restored.info("gyro.fast_sensitivity_x").value,60);
    restored.set("gyro.slow_threshold_dps",12);near(restored.info("gyro.acceleration").value,4);
    Fixture custom;CHECK(gl_load_settings(custom.c,file.string().c_str())==GL_OK);
    near(custom.info("gyro.acceleration").value,4);near(custom.info("gyro.slow_threshold_dps").value,12);
    {std::ofstream out(file);out<<"schema=12\ncontext.1.gyro.acceleration=1\ncontext.1.gyro.fast_sensitivity_x=8\n";}
    CHECK(gl_load_settings(custom.c,file.string().c_str())==GL_OK);near(custom.info("gyro.acceleration").value,4);
    near(custom.info("gyro.fast_sensitivity_x").value,8);
    {std::ofstream out(file);out<<"schema=11\ncontext.1.gyro.acceleration=4\ncontext.1.gyro.fast_sensitivity_y=12.3\n";}
    CHECK(gl_load_settings(custom.c,file.string().c_str())==GL_OK);
    near(custom.info("gyro.fast_sensitivity_x").value,5);near(custom.info("gyro.fast_sensitivity_y").value,12.3);
    std::filesystem::remove(file);
}
static void lean_spaces(){
    for(bool player:{false,true}){
        auto flat=gyrolib::project_lean({3,0,30},{0,-1,0},player);near(flat.pitch,3);near(flat.yaw,30);
        auto upright=gyrolib::project_lean({3,30,0},{0,0,-1},player);near(upright.pitch,3);near(upright.yaw,-30);
        auto side=gyrolib::project_lean({3,30,30},{-1,0,0},player);near(side.yaw,0);near(side.pitch,player?3:0);
        // Physical direction, independent of the projection implementation:
        // a right lean lowers the controller's right (+X) edge. With SDL's
        // right-handed axes and down gravity, this is positive rotation about
        // the forward axis X cross gravity. It must produce right-positive yaw.
        for(gl_vec3 down:{gl_vec3{0,-1,0},gl_vec3{0,0,-1},gl_vec3{0,0,1},gl_vec3{0,1,0},
            gl_vec3{0,-.8f,-.6f},gl_vec3{0,-.8f,.6f},gl_vec3{.3f,-.8f,-.51961524f}}){
            const float length=std::hypot(down.y,down.z);
            const gl_vec3 right_lean{0,-30*down.z/length,30*down.y/length};
            const gl_vec3 left_lean{0,-right_lean.y,-right_lean.z};
            const gl_vec3 accel{-down.x,-down.y,-down.z};
            for(uint64_t dt:{1000000ull,4000000ull,10000000ull,20000000ull}){
                Fixture f(accel);f.set("gyro.space",player?GL_SPACE_PLAYER_LEAN:GL_SPACE_WORLD_LEAN);
                const double angle=30*dt*1e-9;
                near(f.tick(right_lean,dt,accel).yaw_degrees,angle,.002);
                near(f.tick(left_lean,dt,accel).yaw_degrees,-angle,.002);
                f.set("gyro.invert_x",1);
                near(f.tick(right_lean,dt,accel).yaw_degrees,-angle,.002);
                near(f.tick(left_lean,dt,accel).yaw_degrees,angle,.002);
            }
        }
        Fixture vertical;vertical.set("gyro.space",player?GL_SPACE_PLAYER_LEAN:GL_SPACE_WORLD_LEAN);
        near(vertical.tick({3,0,0}).pitch_degrees,.03,1e-4);
        vertical.set("gyro.invert_x",1);near(vertical.tick({3,0,0}).pitch_degrees,.03,1e-4);
        vertical.set("gyro.invert_y",1);near(vertical.tick({3,0,0}).pitch_degrees,-.03,1e-4);
    }
}
static void space_axes_and_inversions(){
    for(int space=0;space<=GL_SPACE_WORLD_LEAN;++space){
        for(uint64_t dt:{1000000ull,4000000ull,10000000ull,20000000ull}){
            // At a flat pose, positive local X raises the front of the pad.
            // Every space keeps this upward pitch, independently of horizontal
            // inversion. Correct acceleration follows the physical pitch.
            for(int direction:{-1,1}){
                const double angle=direction*10.0*dt*1e-9*std::numbers::pi/180;
                const gl_vec3 accel{0,float(std::cos(angle)),float(-std::sin(angle))};
                Fixture pitch;pitch.set("gyro.space",space);pitch.set("sensitivity_x",2);pitch.set("sensitivity_y",3);
                const double expected=direction*30.0*dt*1e-9;
                const auto up=pitch.tick({float(direction*10),0,0},dt,accel);
                near(up.pitch_degrees,expected,1e-5);near(up.yaw_degrees,0,1e-5);
                Fixture inverted;inverted.set("gyro.space",space);inverted.set("sensitivity_x",2);inverted.set("sensitivity_y",3);
                inverted.set("gyro.invert_x",1);inverted.set("gyro.invert_y",1);
                if(space==GL_SPACE_LOCAL_YAW_ROLL)inverted.set("gyro.invert_roll",1);
                const auto down=inverted.tick({float(direction*10),0,0},dt,accel);
                near(down.pitch_degrees,-expected,1e-5);near(down.yaw_degrees,0,1e-5);
            }
            // Independent copies see exactly the same motion and fusion. Each
            // inversion must affect only its output axis, at every sensor rate.
            for(gl_vec3 accel:{gl_vec3{0,1,0},gl_vec3{0,.70710678f,-.70710678f},
                gl_vec3{0,0,-1},gl_vec3{1,0,0},gl_vec3{0,-1,0}}){
                Fixture normal(accel),ix(accel),iy(accel),both(accel);
                for(auto* f:{&normal,&ix,&iy,&both}){
                    f->set("gyro.space",space);f->set("sensitivity_x",2);f->set("sensitivity_y",3);
                }
                ix.set("gyro.invert_x",1);iy.set("gyro.invert_y",1);
                both.set("gyro.invert_x",1);both.set("gyro.invert_y",1);
                if(space==GL_SPACE_LOCAL_YAW_ROLL){ix.set("gyro.invert_roll",1);both.set("gyro.invert_roll",1);}
                const gl_vec3 rate{10,-20,30};const auto n=normal.tick(rate,dt,accel);
                const auto x=ix.tick(rate,dt,accel),y=iy.tick(rate,dt,accel),b=both.tick(rate,dt,accel);
                CHECK(std::isfinite(n.yaw_degrees)&&std::isfinite(n.pitch_degrees));
                near(x.yaw_degrees,-n.yaw_degrees);near(x.pitch_degrees,n.pitch_degrees);
                near(y.yaw_degrees,n.yaw_degrees);near(y.pitch_degrees,-n.pitch_degrees);
                near(b.yaw_degrees,-n.yaw_degrees);near(b.pitch_degrees,-n.pitch_degrees);
            }
        }
    }
    // All fixed local modes remain usable with no acceleration/orientation.
    for(int space:{GL_SPACE_LOCAL_YAW,GL_SPACE_LOCAL_ROLL,GL_SPACE_LOCAL_YAW_ROLL,GL_SPACE_LOCAL_ADVANCED}){
        Fixture with,without({});with.set("gyro.space",space);without.set("gyro.space",space);
        const gl_vec3 rate{10,-20,30};const auto a=with.tick(rate),b=without.tick(rate,10000000,{});
        CHECK(a.gyro_active&&b.gyro_active);near(a.yaw_degrees,b.yaw_degrees);near(a.pitch_degrees,b.pitch_degrees);
    }
}
static void combined_axis_inversion(){
    for(uint64_t dt:{1000000ull,4000000ull,10000000ull,20000000ull})
        for(int yaw:{0,1})for(int roll:{0,1})for(int pitch:{0,1})
            for(double contribution:{-100.,0.,50.,100.}){
                Fixture f;f.set("gyro.space",GL_SPACE_LOCAL_YAW_ROLL);f.set("sensitivity_x",2);f.set("sensitivity_y",3);
                f.set("gyro.invert_x",yaw);f.set("gyro.invert_roll",roll);f.set("gyro.invert_y",pitch);
                f.set("gyro.local_roll_percent",contribution);
                for(gl_vec3 rate:{gl_vec3{8,20,0},gl_vec3{8,0,30},gl_vec3{8,20,30},gl_vec3{8,30,30}}){
                    const auto out=f.tick(rate,dt);
                    const double horizontal=(-rate.y*(yaw?-1:1)+rate.z*(roll?-1:1)*contribution/100)*2;
                    near(out.yaw_degrees,horizontal*dt*1e-9);near(out.pitch_degrees,rate.x*3*dt*1e-9*(pitch?-1:1));
                }
            }
    Fixture f;f.set("gyro.space",GL_SPACE_LOCAL_YAW_ROLL);
    CHECK(f.info("gyro.invert_roll").visible&&f.info("gyro.invert_roll").available);
    CHECK(!std::strcmp(f.info("gyro.invert_x").label,gl_text(f.c,"InvertYaw")));
    CHECK(!std::strcmp(f.info("gyro.invert_x").description,gl_text(f.c,"description.InvertYaw")));
    gl_event event{};while(gl_poll_event(f.c,&event)==1){}
    f.set("gyro.invert_roll",1);bool notified=false;
    while(gl_poll_event(f.c,&event)==1)notified|=event.type==GL_EVENT_SETTING&&!std::strcmp(event.setting_id,"context.1.gyro.invert_roll");
    CHECK(notified);
    const gl_gameplay_context second{2,"Other camera","",1};CHECK(gl_register_gameplay_context(f.c,&second)==GL_OK);
    double other=1;CHECK(gl_setting_get(f.c,"context.2.gyro.invert_roll",&other)==GL_OK);near(other,0);
    // Hidden preferences in other spaces have no effect on those spaces.
    for(int space=0;space<=GL_SPACE_WORLD_LEAN;++space)if(space!=GL_SPACE_LOCAL_YAW_ROLL){
        f.set("gyro.space",space);CHECK(!f.info("gyro.invert_roll").visible);
        Fixture normal,ignored;normal.set("gyro.space",space);ignored.set("gyro.space",space);ignored.set("gyro.invert_roll",1);
        const auto a=normal.tick({10,20,30}),b=ignored.tick({10,20,30});
        near(a.yaw_degrees,b.yaw_degrees);near(a.pitch_degrees,b.pitch_degrees);
    }
    const auto file=std::filesystem::temp_directory_path()/"gyrolib-yaw-roll-inversion.ini";
    for(int invert:{0,1})for(double contribution:{-50.,100.}){
        {std::ofstream out(file);out<<"schema=16\ncontext.1.gyro.space=4\ncontext.1.sensitivity_x=2\ncontext.1.sensitivity_y=3\n"
            <<"context.1.gyro.acceleration=0\ncontext.1.gyro.fast_sensitivity_x=2\ncontext.1.gyro.fast_sensitivity_y=3\n"
            <<"context.1.gyro.invert_x="<<invert<<"\ncontext.1.gyro.invert_y=1\ncontext.1.gyro.local_roll_percent="<<contribution<<"\nhost.preference=keep\n";}
        Fixture migrated;CHECK(gl_load_settings(migrated.c,file.string().c_str())==GL_OK);
        near(migrated.info("gyro.invert_roll").value,invert);
        const auto out=migrated.tick({8,20,30});near(out.yaw_degrees,(-20+30*contribution/100)*2*.01*(invert?-1:1));
        near(out.pitch_degrees,-.24);
        migrated.set("gyro.invert_x",1);migrated.set("gyro.invert_roll",0); // deliberately different
        CHECK(gl_save_settings(migrated.c,file.string().c_str())==GL_OK);
        {std::ifstream in(file);const std::string text((std::istreambuf_iterator<char>(in)),{});
            CHECK(text.find("schema=17")!=std::string::npos&&text.find("host.preference=keep")!=std::string::npos);}
        Fixture restored;CHECK(gl_load_settings(restored.c,file.string().c_str())==GL_OK);
        near(restored.info("gyro.invert_x").value,1);near(restored.info("gyro.invert_roll").value,0);
        restored.set("gyro.space",GL_SPACE_PLAYER);restored.set("gyro.space",GL_SPACE_LOCAL_YAW_ROLL);
        near(restored.info("gyro.invert_roll").value,0);gl_reset_settings(restored.c);near(restored.info("gyro.invert_roll").value,0);
    }
    // An explicit new inversion in an old-schema INI is also respected.
    {std::ofstream out(file);out<<"schema=16\ncontext.1.gyro.invert_x=1\ncontext.1.gyro.invert_roll=0\n";}
    Fixture explicit_roll;CHECK(gl_load_settings(explicit_roll.c,file.string().c_str())==GL_OK);
    near(explicit_roll.info("gyro.invert_roll").value,0);
    std::filesystem::remove(file);
}
// Independent finite-difference oracle: rotate a ray in 3D, then measure its
// azimuth/elevation. No production projection equation is used here.
struct Vec {
    double x,y,z;
    Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
    Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
    Vec operator*(double k)const{return {x*k,y*k,z*k};}
};
static double dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static Vec rotate(Vec v,Vec rate,double seconds){
    const double speed=std::sqrt(dot(rate,rate));if(speed<1e-10)return v;
    const auto axis=rate*(1/speed);const double angle=speed*seconds*std::numbers::pi/180;
    return v*std::cos(angle)+cross(axis,v)*std::sin(angle)+axis*(dot(axis,v)*(1-std::cos(angle)));
}
static void laser_ray_geometry(){
    for(gl_vec3 accel:{gl_vec3{0,1,0},gl_vec3{1,0,0},gl_vec3{0,-1,0},
        gl_vec3{0,.5f,-.8660254f},gl_vec3{.3f,.8f,.51961524f}})
        for(gl_vec3 rate:{gl_vec3{10,0,0},gl_vec3{0,-20,0},gl_vec3{0,0,30},gl_vec3{10,-20,30}})
            for(uint64_t dt:{1000000ull,4000000ull,10000000ull,20000000ull}){
                Fixture f(accel);f.set("gyro.space",GL_SPACE_LASER_POINTER);
                f.set("sensitivity_x",2);f.set("sensitivity_y",3);
                const auto output=f.tick(rate,dt,accel);gl_diagnostics diagnostics{};
                CHECK(gl_get_diagnostics(f.c,&diagnostics)==GL_OK);
                const auto gravity=diagnostics.gravity,calibrated=diagnostics.calibrated_dps;
                Vec up{-gravity.x,-gravity.y,-gravity.z};up=up*(1/std::sqrt(dot(up,up)));
                const Vec ray{0,0,-1};constexpr double epsilon=1e-6;
                const auto next=rotate(ray,{calibrated.x,calibrated.y,calibrated.z},epsilon);
                const auto h0=ray-up*dot(up,ray),h1=next-up*dot(up,next);
                const double yaw=-std::atan2(dot(up,cross(h0,h1)),dot(h0,h1))/epsilon*180/std::numbers::pi;
                const double pitch=(std::asin(dot(up,next))-std::asin(dot(up,ray)))/epsilon*180/std::numbers::pi;
                near(output.yaw_degrees,yaw*2*dt*1e-9,1e-6);
                near(output.pitch_degrees,pitch*3*dt*1e-9,1e-6);
            }
}
static void flick_options(){
    using gyrolib::Flick;using gyrolib::FlickOptions;
    const auto step=[](Flick& f,double angle,double dt=.01){return f.process(float(std::sin(angle*std::numbers::pi/180)),float(std::cos(angle*std::numbers::pi/180)),dt,true,true,2,100,1);};
    const auto neutral=[](Flick& f){return f.process(0,0,.01,true,true,2,100,1);};
    Flick smooth;neutral(smooth);for(int i=0;i<11;++i)step(smooth,0);
    double sum=step(smooth,.2);CHECK(sum>=0&&sum<.2);
    for(int i=0;i<100;++i)sum+=step(smooth,.2);near(sum,.2);
    near(step(smooth,10.2),10,1e-4);neutral(smooth);near(neutral(smooth),0);
    for(auto style:{GL_FLICK_STYLE_FULL,GL_FLICK_STYLE_PIVOT_ONLY,GL_FLICK_STYLE_ROTATE_ONLY}){
        Flick f;FlickOptions o;o.style=style;o.smoothing_ms=0;f.configure(o);neutral(f);
        double pivot=0;for(int i=0;i<10;++i)pivot+=step(f,90);
        near(pivot,style==GL_FLICK_STYLE_ROTATE_ONLY?0:90);
        near(step(f,120),style==GL_FLICK_STYLE_PIVOT_ONLY?0:30,1e-4);
    }
    Flick snapped;FlickOptions o;o.snap=1;snapped.configure(o);neutral(snapped);
    double sum_snap=0;for(int i=0;i<10;++i)sum_snap+=step(snapped,70);near(sum_snap,90);
    o.snap=2;o.snap_strength=.5;Flick partial;partial.configure(o);neutral(partial);
    double sum_partial=0;for(int i=0;i<10;++i)sum_partial+=step(partial,70);near(sum_partial,80,1e-4);
    o={};o.forward_degrees=10;Flick forward;forward.configure(o);neutral(forward);
    near(step(forward,5),0);near(step(forward,15),10,1e-4);
    o={};o.duration_exponent=1;Flick duration;duration.configure(o);neutral(duration);
    double fast=0;for(int i=0;i<5;++i)fast+=step(duration,90);near(fast,90);
    Fixture actual;gl_set_host_capabilities(actual.c,GL_HOST_NATIVE_STICK_SUPPRESSION);
    actual.set("flick.mode",GL_FLICK_ON);actual.set("flick.style",GL_FLICK_STYLE_ROTATE_ONLY);actual.set("flick.smoothing_ms",0);
    actual.tick();actual.controls.right_x=1;near(actual.tick({0,30,0}).yaw_degrees,-.3);
    actual.controls.right_x=0;actual.controls.right_y=-1;near(actual.tick().yaw_degrees,90);
}
static void calibration_guard(){
    Fixture f;CHECK(gl_setting_set(f.c,"calibration.automatic",GL_CAL_ANYTIME)==GL_OK);
    for(int i=0;i<1200;++i)f.tick({0,2,0});gl_diagnostics d{};gl_get_diagnostics(f.c,&d);near(d.bias.y,0);near(f.out.yaw_degrees,-.02);
    f.set("gyro.enabled",0);
    for(int i=0;i<700;++i){gl_set_auto_calibration_allowed(f.c,0);f.tick({0,.5f,0});}
    gl_get_diagnostics(f.c,&d);near(d.bias.y,0);
    for(int i=0;i<1000;++i)f.tick({0,.5f,0});gl_get_diagnostics(f.c,&d);CHECK(d.bias.y>.45);
    Fixture tiny;gl_setting_set(tiny.c,"calibration.automatic",GL_CAL_ANYTIME);
    for(int i=0;i<2500;++i)tiny.tick({0,.1f,0});gl_get_diagnostics(tiny.c,&d);CHECK(d.bias.y>.09&&d.bias.y<=.1f);
}
static void camera_hooks(){
    Fixture f;int calls=0;CHECK(!f.info("camera.recenter_button").visible);
    CHECK(gl_request_recenter(f.c)==GL_UNAVAILABLE);
    gl_set_recenter_callback(f.c,[](void* n){++*static_cast<int*>(n);},&calls);CHECK(f.info("camera.recenter_button").visible);
    f.set("camera.recenter_button",9);f.tick();f.controls.buttons=1u<<8;f.tick();CHECK(calls==1);
    f.tick();CHECK(calls==1);gl_request_recenter(f.c);f.tick();CHECK(calls==2);
    f.host.focused=0;gl_request_recenter(f.c);f.tick();f.host.focused=1;f.tick();CHECK(calls==2);
    f.controls.buttons=0;f.tick();f.controls.buttons=1u<<8;f.tick();CHECK(calls==3);
    gl_set_gameplay_context_output_target(f.c,1,GL_OUTPUT_CURSOR);f.host.menu_open=1;gl_set_host_capabilities(f.c,GL_HOST_MENU_STATE);
    gl_request_recenter(f.c);f.tick();CHECK(calls==3&&!f.info("camera.recenter_button").visible);
    gl_set_gameplay_context_output_target(f.c,1,GL_OUTPUT_CAMERA);f.host.menu_open=0;
    CHECK(!f.info("gyro.zoom_compensation").visible);CHECK(gl_set_gameplay_context_zoom_available(f.c,1,1)==GL_OK);
    CHECK(f.info("gyro.zoom_compensation").visible);f.set("gyro.zoom_compensation",1);
    CHECK(gl_set_gameplay_context_fov(f.c,1,30,90)==GL_OK);near(f.tick({0,30,0}).yaw_degrees,-.3*std::tan(std::numbers::pi/12));
    near(f.tick({0,30,0}).yaw_degrees,-.3); // missing observation never reuses a stale zoom
    gl_set_gameplay_context_fov(f.c,1,30,90);CHECK(gl_set_gameplay_context_fov(f.c,1,0,90)==GL_INVALID);
    near(f.tick({0,30,0}).yaw_degrees,-.3);
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION);f.set("flick.mode",GL_FLICK_ON);f.set("flick.duration_ms",0);f.tick();
    gl_set_gameplay_context_fov(f.c,1,30,90);f.controls.right_x=1;
    near(f.tick({0,30,0}).yaw_degrees,90-.3*std::tan(std::numbers::pi/12));
    gl_set_gameplay_context_zoom_available(f.c,1,0);CHECK(!f.info("gyro.zoom_compensation").visible);
    gl_set_recenter_callback(f.c,nullptr,nullptr);CHECK(!f.info("camera.recenter_button").visible);
}
static void metadata_and_migration(){
    Fixture f;CHECK(gl_setting_is_advanced("context.1.gyro.tightening_dps"));CHECK(!gl_setting_is_advanced("unknown"));
    CHECK(gl_setting_advanced_group("context.1.gyro.tightening_dps")==GL_ADVANCED_SMOOTHING);
    CHECK(gl_setting_advanced_group("context.1.gyro.fast_sensitivity_y")==GL_ADVANCED_ACCELERATION);
    CHECK(gl_setting_advanced_group("context.1.flick.snap")==GL_ADVANCED_FLICK);
    CHECK(gl_setting_advanced_group("context.1.flick.duration_ms")==GL_ADVANCED_FLICK);
    CHECK(gl_setting_advanced_group("context.1.camera.recenter_button")==GL_ADVANCED_NONE);
    CHECK(gl_setting_advanced_group("context.1.gyro.zoom_compensation")==GL_ADVANCED_NONE);
    CHECK(gl_setting_advanced_group(nullptr)==GL_ADVANCED_NONE);
    f.set("gyro.smoothing_ms",0);CHECK(!f.info("gyro.smoothing_threshold_dps").visible);
    CHECK(!f.info("flick.style").visible); // a host still needs a suppression hook
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION);f.set("flick.mode",GL_FLICK_OFF);f.tick();
    for(const char* key:{"flick.duration_ms","flick.style","flick.smoothing_ms","flick.snap","flick.forward_deadzone_degrees","flick.duration_exponent"})
        CHECK(!f.info(key).visible);
    f.set("flick.mode",GL_FLICK_ON);for(const char* key:{"flick.duration_ms","flick.style","flick.smoothing_ms"})CHECK(f.info(key).visible&&f.info(key).available);
    f.set("flick.mode",GL_FLICK_OFF);
    f.set("flick.duration_ms",250);f.set("flick.smoothing_ms",0);
    f.set("gyro.smoothing_threshold_dps",12);
    near(f.info("flick.mode").value,GL_FLICK_OFF);near(f.info("gyro.smoothing_ms").value,0);
    f.controls.right_x=1;CHECK(!f.tick().suppress_native_right_stick);near(f.out.yaw_degrees,0);
    gl_set_gameplay_context_output_target(f.c,1,GL_OUTPUT_CURSOR);
    CHECK(!f.info("flick.duration_ms").visible&&!f.info("flick.style").visible);
    gl_set_gameplay_context_output_target(f.c,1,GL_OUTPUT_CAMERA);
    f.set("flick.mode",GL_FLICK_ON);
    for(uint32_t n=0;n<gl_menu_tab_setting_count(f.c,2);++n){gl_setting_info info{};gl_menu_tab_setting_at(f.c,2,n,&info);
        if(info.visible&&gl_setting_advanced_group(info.id)==GL_ADVANCED_FLICK){CHECK(!std::strcmp(info.id,"context.1.flick.duration_ms"));break;}}
    CHECK(!f.info("gyro.fast_sensitivity_x").visible);f.set("gyro.acceleration",4);CHECK(f.info("gyro.fast_sensitivity_x").visible);
    for(auto lang:{"en","fr"}){gl_set_language(f.c,lang);
        CHECK(!std::strcmp(f.info("sensitivity_x").label,gl_text(f.c,"SlowSensitivityX")));
        for(uint32_t i=0;i<gl_menu_tab_setting_count(f.c,2);++i){gl_setting_info s{};gl_menu_tab_setting_at(f.c,2,i,&s);
            CHECK(std::strcmp(s.label,"?")&&std::strcmp(s.description,"?"));
            for(uint32_t n=0;n<gl_menu_choice_count(f.c,s.id);++n){gl_choice choice{};gl_choice_at(f.c,s.id,n,&choice);
                CHECK(std::strcmp(choice.label,"?"));if(gl_setting_is_advanced(s.id)&&s.type==GL_SETTING_ENUM)CHECK(*gl_choice_description(f.c,s.id,choice.value));}
        }
    }
    const auto file=std::filesystem::temp_directory_path()/"gyrolib-advanced-migration.ini";
    {std::ofstream out(file);out<<"schema=10\nui.menu_key=\ncontext.1.sensitivity_x=7.2\ncontext.1.gyro.smoothing_ms=85\ncustom.host=keep\n";}
    CHECK(gl_load_settings(f.c,file.string().c_str())==GL_OK);near(f.info("sensitivity_x").value,7.2);near(f.info("gyro.smoothing_ms").value,85);
    f.set("gyro.tightening_dps",.35);f.set("gyro.space",GL_SPACE_PLAYER_LEAN);f.set("flick.snap",2);
    Fixture loaded;CHECK(gl_load_settings(loaded.c,file.string().c_str())==GL_OK);near(loaded.info("gyro.tightening_dps").value,.35);near(loaded.info("flick.snap").value,2);
    CHECK(gl_get_menu_key(loaded.c)==0);std::ifstream in(file);std::string data((std::istreambuf_iterator<char>(in)),{});CHECK(data.find("schema=17")!=std::string::npos&&data.find("custom.host=keep")!=std::string::npos);
    in.close();std::filesystem::remove(file);
}
int main(){try{
    const auto run=[](const char* name,auto test){try{test();}catch(const std::exception& e){throw std::runtime_error(std::string(name)+": "+e.what());}};
    run("adaptive smoothing",adaptive_smoothing);run("world gravity direction",world_gravity_direction);
    run("tightening and acceleration",tightening_and_acceleration);run("acceleration presets",acceleration_presets);
    run("lean spaces",lean_spaces);run("space axes and inversions",space_axes_and_inversions);
    run("Yaw + Roll independent inversions",combined_axis_inversion);
    run("laser ray geometry",laser_ray_geometry);run("flick options",flick_options);run("calibration guard",calibration_guard);
    run("camera hooks",camera_hooks);run("metadata and migration",metadata_and_migration);
    std::cout<<"Advanced motion: 12 synthetic regression groups passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
