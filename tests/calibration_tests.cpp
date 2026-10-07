#include <gyrolib/gyrolib.hpp>
#include "../src/sdl_identity.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(0)
static void near(double a,double b,double tolerance=.001){CHECK(std::abs(a-b)<tolerance);}
struct Fixture {
    gyrolib::Context owner;gl_context* c=owner.get();uint64_t now=1000000000,id=1;
    gl_host_state host{};gl_output output{};
    Fixture(){
        gl_gameplay_context v{1,"Camera","",0};CHECK(gl_register_gameplay_context(c,&v)==GL_OK);
        CHECK(gl_setting_set(c,"context.1.gyro.space",GL_SPACE_LOCAL_YAW)==GL_OK);
        CHECK(gl_setting_set(c,"context.1.sensitivity_x",1)==GL_OK);
        CHECK(gl_setting_set(c,"context.1.sensitivity_y",1)==GL_OK);
        host.focused=host.camera_allowed=1;
    }
    void device(uint64_t endpoint,uint64_t key,uint32_t source=GL_SOURCE_SDL){
        gl_endpoint e{};e.id=e.physical_id=endpoint;e.source=source;e.connected=1;
        e.caps.gyro=e.caps.accelerometer=1;std::strcpy(e.name,"Identical controller model");
        CHECK(gl_register_endpoint(c,&e)==GL_OK);
        if(source==GL_SOURCE_SDL)CHECK(gl_set_endpoint_calibration_identity(c,endpoint,key)==GL_OK);
        CHECK(gl_select_device(c,endpoint)==GL_OK);id=endpoint;
        for(int n=0;n<4;++n)tick();
    }
    void tick(gl_vec3 g={},gl_vec3 a={0,1,0}){
        now+=4000000;gl_sample s{now,now,g,a};CHECK(gl_submit_sample(c,id,&s)==GL_OK);
        CHECK(gl_set_gameplay_context_state(c,1,1,1)==GL_OK);
        CHECK(gl_update(c,now,&host,&output)==GL_OK);
    }
    gl_diagnostics diagnostics(){gl_diagnostics d{};CHECK(gl_get_diagnostics(c,&d)==GL_OK);return d;}
    void calibrate(float bias,float noise=0){
        CHECK(gl_begin_calibration(c)==GL_OK);
        for(int n=0;n<11000&&diagnostics().calibration_state!=GL_CAL_COMPLETE;++n)
            tick({bias+(n%2?noise:-noise),.2f,-.3f});
        CHECK(diagnostics().calibration_state==GL_CAL_COMPLETE);near(diagnostics().bias.x,bias,.05);
    }
};
static std::string read(const std::filesystem::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
static std::string row(const std::string& text){const auto start=text.find("calibration.device.");CHECK(start!=std::string::npos);return text.substr(start,text.find('\n',start)-start);}
static void write(const std::filesystem::path& p,const std::string& text){std::ofstream f(p);f<<text;CHECK(bool(f));}
int main()try{
    {
        // Acquisition runs before queued UI actions and gl_update. The newest
        // valid sample therefore belongs to the next frame, not c->now yet.
        Fixture f;f.device(1,0);gl_set_panel_open(f.c,1);
        const auto next=f.now+4000000;
        const gl_sample queued{next,next,{1.5f,0,0},{0,1,0}};
        CHECK(gl_submit_sample(f.c,1,&queued)==GL_OK);
        bool enabled=false;
        for(uint32_t i=0;i<gl_menu_setting_count(f.c);++i){gl_setting_info setting{};
            CHECK(gl_setting_at(f.c,i,&setting)==GL_OK);
            if(std::strcmp(setting.id,"calibration.begin")==0)enabled=setting.visible&&setting.available;
        }
        CHECK(enabled);
        CHECK(gl_action(f.c,"calibration.begin")==GL_OK);
        CHECK(f.diagnostics().calibration_state==GL_CAL_COUNTDOWN);
        for(int n=0;n<2000&&f.diagnostics().calibration_state!=GL_CAL_COMPLETE;++n){
            // Start after the already queued timestamp; update consumes both.
            if(n==0)f.now=next;
            f.tick({1.5f,0,0});
            CHECK(f.diagnostics().calibration_state!=GL_CAL_IDLE);
        }
        CHECK(f.diagnostics().calibration_state==GL_CAL_COMPLETE);near(f.diagnostics().bias.x,1.5);
        // A fresh gyro packet must not make missing acceleration look fresh.
        for(int n=0;n<50;++n)f.tick({},{});
        const gl_sample no_accel{f.now+4000000,f.now+4000000,{},{}};
        CHECK(gl_submit_sample(f.c,1,&no_accel)==GL_OK);
        CHECK(gl_action(f.c,"calibration.begin")==GL_UNAVAILABLE);
        CHECK(f.diagnostics().calibration_state==GL_CAL_COMPLETE);
        CHECK(gl_disconnect_endpoint(f.c,1)==GL_OK);
        CHECK(gl_action(f.c,"calibration.begin")==GL_UNAVAILABLE);
    }
    namespace fs=std::filesystem;
    const auto root=fs::current_path()/("calibration-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(root));
    struct Cleanup {fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{root};
    const auto ini=root/"gyrolib.ini",bad=root/"bad.ini";
    using gyrolib_sdl_detail::calibration_identity;
    const auto key1=calibration_identity(0x057e,0x2009,"serial-a");
    const auto key2=calibration_identity(0x057e,0x2009,"serial-b");
    CHECK(key1&&key2&&key1!=key2);CHECK(key1==calibration_identity(0x057e,0x2009,"serial-a"));
    CHECK(key1!=calibration_identity(0x057e,0x2010,"serial-a"));
    CHECK(!calibration_identity(0x057e,0x2009,nullptr)&&!calibration_identity(0x057e,0x2009,""));
    {
        Fixture f;f.device(1,key1);CHECK(gl_save_settings(f.c,ini.string().c_str())==GL_OK);
        f.calibrate(4.5f,2);const auto manual_file=read(ini),manual_row=row(manual_file);
        CHECK(manual_file.find("serial-a")==std::string::npos); // no serial in the INI
        CHECK(gl_get_settings_save_result(f.c)==GL_OK);
        f.tick({4.5f,.2f,-.3f});near(f.output.pitch_degrees,0);
        // Automatic learning can now refine this biased AND noisy sensor.
        CHECK(gl_setting_set(f.c,"calibration.automatic",GL_CAL_ANYTIME)==GL_OK);
        for(int n=0;n<10000;++n)f.tick({4.6f+(n%2?2.f:-2.f),.2f,-.3f});
        near(f.diagnostics().bias.x,4.6,.04);
        CHECK(row(read(ini))==manual_row); // never overwrite the trusted manual result
        const auto refined=f.diagnostics().bias.x;
        // Below the idle residual bound, but too large for learning during aim.
        for(int n=0;n<6000;++n)f.tick({4.5f,.5f+(n%2?2.f:-2.f),-.3f});
        near(f.diagnostics().bias.x,refined);near(f.diagnostics().bias.y,.2,.04);
        // The host's calibration veto also applies to the referenced path.
        for(int n=0;n<6000;++n){CHECK(gl_set_auto_calibration_allowed(f.c,0)==GL_OK);f.tick({4.7f+(n%2?2.f:-2.f),.2f,-.3f});}
        near(f.diagnostics().bias.x,refined);
        // A voluntary residual turn must not become a new reference, even idle.
        const auto trusted=f.diagnostics().bias.x;
        for(bool active:{true,false}){
            f.host.camera_allowed=active;
            for(int n=0;n<6000;++n)f.tick({4.5f,1.2f+(n%2?2.f:-2.f),-.3f});
            near(f.diagnostics().bias.x,trusted);near(f.diagnostics().bias.y,.2,.04);
        }
        f.host.camera_allowed=1;
        // Cancel does not destroy a completed reference or replace the saved row.
        CHECK(gl_begin_calibration(f.c)==GL_OK);f.tick({9,0,0});gl_cancel_calibration(f.c);
        CHECK(row(read(ini))==manual_row);near(f.diagnostics().bias.x,trusted);
        // Same-model second controller has a separate calibration.
        f.device(2,key2);near(f.diagnostics().bias.x,0);
        f.calibrate(-2);CHECK(row(read(ini))==manual_row||read(ini).find(manual_row)!=std::string::npos);
        CHECK(gl_forget_endpoint(f.c,1)==GL_OK);f.device(7,key1);near(f.diagnostics().bias.x,4.5,.05);
        CHECK(gl_associate_endpoint(f.c,7,999)==GL_OK); // pairing is not sensor identity
        f.tick({4.5f,.2f,-.3f});near(f.diagnostics().bias.x,4.5,.05);
    }
    {
        // Fresh context, load before discovery, different endpoint IDs/order.
        Fixture f;CHECK(gl_load_settings(f.c,ini.string().c_str())==GL_OK);
        f.device(52,key2);near(f.diagnostics().bias.x,-2,.01);
        f.device(51,key1);near(f.diagnostics().bias.x,4.5,.05);
        f.tick({4.5f,.2f,-.3f});near(f.output.pitch_degrees,0);
        gl_reset_settings(f.c);near(f.diagnostics().bias.x,4.5,.05); // preferences != sensor measurement
        CHECK(gl_capture_recommended_settings(f.c)==GL_OK);CHECK(gl_apply_recommended_settings(f.c)==GL_OK);
        near(f.diagnostics().bias.x,4.5,.05);
        // A Steam stream cannot receive even an otherwise matching manual key.
        f.device(99,0,GL_SOURCE_STEAM);
        CHECK(gl_set_endpoint_calibration_identity(f.c,99,key1)==GL_INVALID);
        for(int n=0;n<100;++n)f.tick({1,0,0});
        near(f.diagnostics().bias.x,0);CHECK(f.diagnostics().calibration_state==GL_CAL_EXTERNAL);
    }
    {
        // Discovery before load is supported too; rejected files are transactional.
        Fixture f;f.device(8,key1);near(f.diagnostics().bias.x,0);
        CHECK(gl_load_settings(f.c,ini.string().c_str())==GL_OK);f.tick({4.5f,.2f,-.3f});near(f.diagnostics().bias.x,4.5,.05);
        const auto valid=read(ini),entry=row(valid);const auto equals=entry.find('=');
        for(const char* invalid:{"nan,0,0,0","11,0,0,0","0,0,0,-1","0,0,0,9","0,0,0","0,0,0,0,1"}){
            auto text=valid;text.replace(text.find(entry),entry.size(),entry.substr(0,equals+1)+invalid);
            write(bad,text);CHECK(gl_load_settings(f.c,bad.string().c_str())==GL_INVALID);
            near(f.diagnostics().bias.x,4.5,.05);CHECK(read(ini)==valid);
        }
        // Duplicate claimed serial: neither sensor may inherit the shared record.
        f.device(9,key1);near(f.diagnostics().bias.x,0);
        CHECK(gl_select_device(f.c,8)==GL_OK);f.id=8;for(int n=0;n<4;++n)f.tick();near(f.diagnostics().bias.x,0);
        CHECK(gl_set_endpoint_calibration_identity(f.c,8,key1)==GL_OK);
        f.calibrate(1);CHECK(read(ini).find(entry)==std::string::npos);
    }
    {
        // Every new manual request measures raw data again. A failed attempt
        // retains the old reference; success replaces both bias and noise.
        Fixture f;f.device(1,key1);const auto repeat=root/"repeat.ini";
        CHECK(gl_save_settings(f.c,repeat.string().c_str())==GL_OK);f.calibrate(2);
        const auto old=row(read(repeat));CHECK(gl_begin_calibration(f.c)==GL_OK);
        for(int n=0;n<2000;++n)f.tick({25,0,0});
        CHECK(f.diagnostics().calibration_state!=GL_CAL_COMPLETE);
        CHECK(row(read(repeat))==old);near(f.diagnostics().bias.x,2);
        f.calibrate(5,1);CHECK(row(read(repeat))!=old);near(f.diagnostics().bias.x,5,.05);
        Fixture restored;CHECK(gl_load_settings(restored.c,repeat.string().c_str())==GL_OK);
        restored.device(7,key1);near(restored.diagnostics().bias.x,5,.05);
        const auto saved=read(repeat);
        CHECK(gl_set_settings_path(f.c,(root/"missing"/"gyrolib.ini").string().c_str())==GL_OK);
        f.calibrate(3);near(f.diagnostics().bias.x,3,.05);
        CHECK(gl_get_settings_save_result(f.c)==GL_IO_ERROR&&read(repeat)==saved);
        CHECK(gl_save_settings(f.c,repeat.string().c_str())==GL_OK);
        CHECK(gl_load_settings(restored.c,repeat.string().c_str())==GL_OK);restored.tick();near(restored.diagnostics().bias.x,3,.05);
        // Forward-compatible unknown metadata survives a normal round trip.
        const auto future="calibration.device.1122334455667788.future=keep\n";
        write(repeat,read(repeat)+future);
        CHECK(gl_load_settings(restored.c,repeat.string().c_str())==GL_OK);
        CHECK(gl_save_settings(restored.c,repeat.string().c_str())==GL_OK);
        CHECK(read(repeat).find(future)!=std::string::npos);
    }
    {
        // Missing serial: usable manual calibration in-session, no unsafe reuse.
        Fixture f;f.device(1,0);const auto anonymous=root/"anonymous.ini";
        CHECK(gl_save_settings(f.c,anonymous.string().c_str())==GL_OK);f.calibrate(2);
        CHECK(read(anonymous).find("calibration.device.")==std::string::npos);
        CHECK(gl_forget_endpoint(f.c,1)==GL_OK);f.device(2,0);near(f.diagnostics().bias.x,0);
    }
    std::cout<<"Manual calibration: per-sensor persistence, residual learning, identity isolation, cancellation, malformed files and Steam separation passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
