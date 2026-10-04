// Link statically so the injected allocator also covers library allocations.
#include <gyrolib/gyrolib.h>
#include <cstdio>
#include <cstdlib>
#include <new>
static bool fail_allocation;
void* operator new(std::size_t n){if(fail_allocation)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
#define CHECK(x) do{if(!(x)){fail_allocation=false;std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    gl_context* c=gl_create(GL_ABI_VERSION);CHECK(c);
    gl_gameplay_context view{1,"Allocation failure","",0};CHECK(gl_register_gameplay_context(c,&view)==GL_OK);
    CHECK(gl_setting_set(c,"context.1.sensitivity_x",7)==GL_OK);
    bool escaped=false;double value{};
    fail_allocation=true;
    try{
        CHECK(gl_setting_get(c,"context.1.flick.touchpad_release_threshold",&value)==GL_OK);
        CHECK(gl_setting_get_effective(c,"context.1.flick.touchpad_release_threshold",&value)==GL_OK);
        gl_setting_inheritance_info info{};CHECK(gl_setting_inheritance(c,"context.1.flick.touchpad_release_threshold",&info)==GL_OK);
        CHECK(gl_setting_advanced_group("context.1.flick.touchpad_release_threshold")==GL_ADVANCED_FLICK);
        CHECK(gl_action(c,"settings.reset")==GL_LIMIT);
        CHECK(gl_setting_get(c,"context.1.sensitivity_x",&value)==GL_OK&&value==7);
        CHECK(gl_setting_set(c,"context.1.sensitivity_x",8)==GL_LIMIT);
        CHECK(gl_setting_get(c,"context.1.sensitivity_x",&value)==GL_OK&&value==7);
        for(unsigned i=0;i<300;++i)CHECK(gl_set_gameplay_context_zoom_available(c,1,(i+1)%2)==GL_OK);
        gl_event_ex e{};unsigned count=0;while(gl_poll_event_ex(c,&e)>0)++count;CHECK(count==128);
        gl_set_recenter_callback(c,nullptr,nullptr);gl_set_panel_open(c,1);gl_cancel_calibration(c);
    }catch(...){escaped=true;}
    fail_allocation=false;CHECK(!escaped);
    CHECK(gl_action(c,"settings.reset")==GL_OK); // a previous allocation error must not persist
    CHECK(gl_setting_get(c,"context.1.sensitivity_x",&value)==GL_OK&&value!=7);
    fail_allocation=true;gl_destroy(c);fail_allocation=false;return 0;
}
