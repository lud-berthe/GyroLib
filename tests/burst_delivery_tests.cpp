#include <gyrolib/gyrolib.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x);}while(0)
static void near(double a,double b){CHECK(std::isfinite(a)&&std::abs(a-b)<1e-5);}
struct Fixture {
    gyrolib::Context owner;gl_context* c=owner.get();
    uint64_t now=1000000000,sensor=8000000000;
    gl_host_state host{};
    Fixture(){
        host.focused=host.camera_allowed=1;
        gl_gameplay_context view{1,"Camera","",0};CHECK(gl_register_gameplay_context(c,&view)==GL_OK);
        gl_endpoint e{};e.id=e.physical_id=1;e.source=GL_SOURCE_SDL;e.connected=1;e.caps.gyro=e.caps.accelerometer=1;
        CHECK(gl_register_endpoint(c,&e)==GL_OK);
        for(const char* key:{"context.1.sensitivity_x","context.1.sensitivity_y"})CHECK(gl_setting_set(c,key,1)==GL_OK);
        CHECK(gl_setting_set(c,"context.1.gyro.space",GL_SPACE_LOCAL_YAW)==GL_OK);
    }
    void samples(int count,float yaw=0,float pitch=0,bool grouped=true){
        for(int i=0;i<count;++i){sensor+=5000000;
            gl_sample s{sensor,grouped?now:now-uint64_t(count-1-i)*5000000,{pitch,yaw,0},{0,1,0}};
            CHECK(gl_submit_sample(c,1,&s)==GL_OK);}
    }
    gl_output update(){CHECK(gl_set_gameplay_context_state(c,1,1,1)==GL_OK);
        gl_output out{};CHECK(gl_update(c,now,&host,&out)==GL_OK);return out;}
    void warm(){for(int i=0;i<180;++i){now+=5000000;if(i%6==0)samples(6);update();}}
};
static void steady_bursts(){
    Fixture f;f.warm();double yaw=0,pitch=0,sum=0,square=0;unsigned zero=0,count=0;
    for(int i=0;i<1200;++i){f.now+=5000000;if(i%6==0)f.samples(6,90,60);
        const auto out=f.update();yaw+=out.yaw_degrees;pitch+=out.pitch_degrees;
        if(i>60){const double speed=-out.yaw_degrees/.005;sum+=speed;square+=speed*speed;++count;if(speed<1e-6)++zero;}
        // A second message-pump update at the same time cannot replay output.
        near(f.update().yaw_degrees,0);
    }
    for(int i=0;i<20;++i){f.now+=5000000;const auto out=f.update();yaw+=out.yaw_degrees;pitch+=out.pitch_degrees;}
    near(yaw,-540);near(pitch,360);
    const double mean=sum/count,cv=std::sqrt(std::max(0.,square/count-mean*mean))/mean;
    std::printf("30 ms bursts: zero %.1f%%, speed CV %.3f\n",100.*zero/count,cv);
    CHECK(zero<count/10);CHECK(cv<.15);
    f.now+=5000000;near(f.update().yaw_degrees,0);
}
static void regular_stream_has_no_tail(){
    for(bool grouped:{false,true}){
        Fixture f;
        for(int i=0;i<100;++i){f.now+=30000000;f.samples(6,i<5?0:90,0,grouped);const auto out=f.update();
            if(i>=5)near(out.yaw_degrees,-2.7);}
        f.now+=5000000;near(f.update().yaw_degrees,0);
    }
}
static void gates_discard_backlog(){
    for(int gate=0;gate<6;++gate){Fixture f;f.warm();f.now+=30000000;f.samples(6,90);f.update();
        if(gate==0)f.host.focused=0;
        if(gate==1)f.host.paused=1;
        if(gate==2)f.host.camera_allowed=0;
        if(gate==3)gl_set_panel_open(f.c,1);
        if(gate==4)CHECK(gl_set_gyro_override(f.c,0)==GL_OK);
        if(gate==5)CHECK(gl_setting_set(f.c,"context.1.gyro.activation",GL_GYRO_OFF)==GL_OK);
        f.now+=5000000;near(f.update().yaw_degrees,0);
        f.host.focused=f.host.camera_allowed=1;f.host.paused=0;gl_set_panel_open(f.c,0);
        CHECK(gl_setting_set(f.c,"context.1.gyro.activation",GL_ALWAYS)==GL_OK);
        for(int i=0;i<10;++i){f.now+=5000000;near(f.update().yaw_degrees,0);}
    }
}
static void irregular_bursts(){
    for(uint64_t frame_step:{4166667ull,8333333ull,16666667ull}){
        Fixture f;f.warm();const uint64_t start=f.now;
        const int gaps[]={15,30,5,60,25,65,10,35};
        int next_burst=15,index=0,queued=0;uint64_t next_frame=frame_step;
        double total=0,sum=0,square=0,expected=0;unsigned count=0,zero=0;
        for(int ms=1;ms<=5000;++ms){f.now=start+uint64_t(ms)*1000000;
            if(ms<=4500&&ms%5==0)++queued;
            if(ms==next_burst){
                if(queued){f.samples(queued,90);expected-=queued*.005*90;queued=0;}
                next_burst+=gaps[++index%8];
            }
            if(uint64_t(ms)*1000000>=next_frame){const auto out=f.update();total+=out.yaw_degrees;
                if(ms>500&&ms<4400){const double angle=-out.yaw_degrees;sum+=angle;square+=angle*angle;++count;if(angle<1e-6)++zero;}
                next_frame+=frame_step;
            }
        }
        near(total,expected);CHECK(queued==0);
        const double mean=sum/count,cv=std::sqrt(std::max(0.,square/count-mean*mean))/mean;
        std::printf("Irregular bursts at %.0f Hz: zero %.1f%%, CV %.3f\n",1e9/frame_step,100.*zero/count,cv);
        CHECK(zero<count/5);CHECK(cv<.75);
    }
}
static void transitions_discard_backlog(){
    for(int transition=0;transition<5;++transition){
        Fixture f;f.warm();f.now+=30000000;f.samples(6,90);f.update();
        if(transition==0){
            gl_gameplay_context view{2,"Another camera","",1};CHECK(gl_register_gameplay_context(f.c,&view)==GL_OK);
            CHECK(gl_set_gameplay_context_state(f.c,2,1,1)==GL_OK);
        }
        if(transition==1)CHECK(gl_setting_set(f.c,"context.1.sensitivity_x",2)==GL_OK);
        if(transition==2)CHECK(gl_set_gyro_modifiers(f.c,GL_MOD_INVERT_X)==GL_OK);
        if(transition==3){gl_endpoint e{};CHECK(gl_get_endpoint(f.c,0,&e)==GL_OK);e.connected=0;CHECK(gl_register_endpoint(f.c,&e)==GL_OK);}
        if(transition==4)f.now+=200000000; // host stall
        f.now+=5000000;near(f.update().yaw_degrees,0);
        for(int i=0;i<10;++i){f.now+=5000000;near(f.update().yaw_degrees,0);}
    }
}
int main()try{irregular_bursts();transitions_discard_backlog();steady_bursts();regular_stream_has_no_tail();gates_discard_backlog();return 0;}
catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
