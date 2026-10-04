// Public-API regressions for menu input continuity and opening state.
#include <gyrolib/gyrolib.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(0)
struct Input {
    gyrolib::Context owner;gl_context* c=owner.get();uint64_t now=1000000000;
    gl_endpoint e{};gl_host_state host{};
    Input(){e.id=1;e.physical_id=7;e.source=GL_SOURCE_SDL;e.connected=1;e.caps.buttons=(1u<<4)|(1u<<6);
        host.focused=1;CHECK(gl_register_endpoint(c,&e)==GL_OK);frame(0);}
    void frame(uint32_t buttons,uint64_t dt=16000000,uint64_t endpoint=1,uint64_t age=0){now+=dt;gl_controls ctl{};ctl.timestamp_ns=now-age;ctl.buttons=buttons;
        CHECK(gl_submit_controls(c,endpoint,&ctl)==GL_OK);gl_output out{};CHECK(gl_update(c,now,&host,&out)==GL_OK);}
    void recover(){frame(0);frame((1u<<4)|(1u<<6));CHECK(gl_panel_open(c));gl_set_panel_open(c,0);frame(0);}
};
int main(int argc,char** argv)try{
    CHECK(argc==2);const std::string scenario=argv[1];Input f;constexpr auto chord=(1u<<4)|(1u<<6);
    if(scenario=="reconnect"){
        CHECK(gl_disconnect_endpoint(f.c,1)==GL_OK);CHECK(gl_register_endpoint(f.c,&f.e)==GL_OK);
        f.frame(chord);std::cout<<"same-identity reconnect, panel="<<gl_panel_open(f.c)<<" expected=0\n";
        CHECK(!gl_panel_open(f.c));
        f.recover();
        CHECK(gl_forget_endpoint(f.c,1)==GL_OK);CHECK(gl_register_endpoint(f.c,&f.e)==GL_OK);
        f.frame(chord);CHECK(!gl_panel_open(f.c));f.recover();
        f.e.caps.buttons=0;CHECK(gl_register_endpoint(f.c,&f.e)==GL_OK);
        f.e.caps.buttons=chord;CHECK(gl_register_endpoint(f.c,&f.e)==GL_OK);
        f.frame(chord);CHECK(!gl_panel_open(f.c));f.recover();
        auto other=f.e;other.id=2;other.source=GL_SOURCE_STEAM;
        CHECK(gl_register_endpoint(f.c,&other)==GL_OK);
        CHECK(gl_set_endpoint_control_authority(f.c,2,GL_CONTROL_BUTTONS)==GL_OK);
        f.frame(chord,16000000,2);CHECK(!gl_panel_open(f.c));
        f.frame(0,16000000,2);f.frame(chord,16000000,2);CHECK(gl_panel_open(f.c));
        gl_set_panel_open(f.c,0);f.frame(0,16000000,2);
        other.id=3;other.physical_id=8;CHECK(gl_register_endpoint(f.c,&other)==GL_OK);
        CHECK(gl_select_device(f.c,8)==GL_OK);CHECK(gl_select_device(f.c,7)==GL_OK);
        f.frame(chord,16000000,2);CHECK(!gl_panel_open(f.c));
    }else if(scenario=="gap"){
        f.frame(chord,2000000000ull);std::cout<<"two-second input/update gap, panel="<<gl_panel_open(f.c)<<" expected=0\n";
        CHECK(!gl_panel_open(f.c));
        f.recover();
        f.frame(chord,150000000);CHECK(!gl_panel_open(f.c));f.recover();
        // Fresh current packet after a sample gap, even with short update gaps.
        for(int n=0;n<8;++n)f.frame(0,16000000,1,16000000ull*(n+1));
        f.frame(chord,30000000);CHECK(!gl_panel_open(f.c));f.recover();
        // A slow but continuous stream below the timeout must still work.
        f.frame(chord,149000000);CHECK(gl_panel_open(f.c));
    }else if(scenario=="opening"){
        uint32_t view=99;CHECK(gl_get_panel_opening(nullptr,&view)==0&&view==0);
        CHECK(gl_get_panel_opening(f.c,&view)==0&&view==0);
        const gl_gameplay_context a{1,"Explore","",0},b{2,"Aim","",1};
        CHECK(gl_register_gameplay_context(f.c,&a)==GL_OK);CHECK(gl_register_gameplay_context(f.c,&b)==GL_OK);
        CHECK(gl_set_gameplay_context_state(f.c,2,1,1)==GL_OK);
        gl_set_panel_open(f.c,1);CHECK(gl_get_panel_opening(f.c,&view)==1&&view==2);
        CHECK(gl_set_gameplay_context_state(f.c,2,0,1)==GL_OK);
        gl_set_panel_open(f.c,1);CHECK(gl_get_panel_opening(f.c,&view)==1&&view==2);
        gl_set_panel_open(f.c,0);CHECK(gl_get_panel_opening(f.c,&view)==1&&view==2);
        CHECK(gl_set_gameplay_context_state(f.c,1,1,1)==GL_OK);
        gl_set_panel_open(f.c,1);CHECK(gl_get_panel_opening(f.c,&view)==2&&view==1);
        CHECK(gl_get_panel_opening(f.c,nullptr)==2);
    }else throw std::runtime_error("unknown scenario");
    return 0;
}catch(const std::exception& e){std::cerr<<"Audit F: "<<e.what()<<'\n';return 1;}
