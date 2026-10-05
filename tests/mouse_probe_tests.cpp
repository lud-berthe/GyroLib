#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/detail/mouse_probe.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main()try{
    const auto root=std::filesystem::temp_directory_path()/("gyrolib-mouse-probe-test-"+std::to_string(GetCurrentProcessId()));
    CHECK(std::filesystem::create_directory(root));
    const auto settings=(root/"gyrolib.ini").string();
    const auto log=root/("gyrolib-mouse-probe-"+std::to_string(GetCurrentProcessId())+".csv");
    {
        gyrolib_detail::MouseProbe disabled;disabled.configure(settings);
        disabled.message(WM_KEYDOWN,VK_F7,0);disabled.message(WM_MOUSEMOVE,0,MAKELPARAM(1,2));
        CHECK(!std::filesystem::exists(log));
    }
    const auto marker=root/"gyrolib-mouse-probe.enable";{std::ofstream file(marker);file<<"1\n";}
    {
        gyrolib_detail::MouseProbe probe;probe.configure("");probe.configure(settings);
        probe.message(WM_MOUSEMOVE,0,MAKELPARAM(99,98)); // no recording before F7
        probe.message(WM_KEYDOWN,VK_F7,0);probe.message(WM_MOUSEMOVE,0,MAKELPARAM(10,20));
        probe.message(WM_KEYDOWN,VK_F8,0);probe.message(WM_MOUSEMOVE,0,MAKELPARAM(30,40));
        probe.message(WM_CHAR,'X',0); // never logs keyboard text
        probe.message(WM_INPUT,0,0); // invalid raw handle cannot break the probe
        probe.message(WM_KEYDOWN,VK_F7,1ll<<30); // a repeat cannot restart recording
        probe.message(WM_KEYDOWN,VK_F8,0);
    }
    std::ifstream file(log);std::string line;unsigned records=0;bool phase1=false,phase2=false;
    while(std::getline(file,line))if(!line.empty()&&line[0]>='0'&&line[0]<='9'){
        ++records;phase1|=line.starts_with("1,")&&line.find(",10,20,")!=std::string::npos;
        phase2|=line.starts_with("2,")&&line.find(",30,40,")!=std::string::npos;
    }
    CHECK(records==2&&phase1&&phase2);file.close();
    std::filesystem::remove(log);std::filesystem::remove(marker);CHECK(std::filesystem::remove(root));
    std::cout<<"Opt-in passive mouse probe, deferred settings, phases and invalid input passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
