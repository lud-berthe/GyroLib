#include <gyrolib/gyrolib.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("settings:")+std::to_string(__LINE__)+": " #x);}while(0)
static std::string utf8(const fs::path& path){auto bytes=path.u8string();return {bytes.begin(),bytes.end()};}
static std::string read(const fs::path& path){std::ifstream f(path);return {(std::istreambuf_iterator<char>(f)),{}};}
static void write(const fs::path& path,const std::string& text){std::ofstream f(path);f<<text;CHECK(f.good());}
int main() try {
    const auto root=fs::current_path()/("settings-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto directory=root/fs::path(u8"dossier avec espaces é");fs::create_directories(directory);
    const auto folder=utf8(directory),path=utf8(directory/GL_SETTINGS_FILENAME),legacy=utf8(root/"settings.ini");
    gyrolib::Context c;CHECK(c);const gl_gameplay_context view{1,"Camera","",0};CHECK(gl_register_gameplay_context(c.get(),&view)==GL_OK);CHECK(gl_get_menu_key(c.get())==10);
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),nullptr)==GL_OK);
    CHECK(fs::equivalent(fs::u8path(gl_get_settings_path(c.get())),fs::u8path(path)));
    CHECK(read(fs::u8path(path)).find("ui.menu_key=F10\n")!=std::string::npos);
    for(unsigned key=1;key<=24;++key){
        CHECK(gl_set_menu_key(c.get(),key)==GL_OK);
        gyrolib::Context restored;CHECK(gl_load_settings(restored.get(),path.c_str())==GL_OK);
        CHECK(gl_get_menu_key(restored.get())==key);
    }
    CHECK(gl_set_menu_key(c.get(),0)==GL_OK);CHECK(gl_set_language(c.get(),"fr")==GL_OK);
    CHECK(gl_setting_set(c.get(),"context.1.sensitivity_x",12.5)==GL_OK);gl_reset_settings(c.get());
    CHECK(gl_get_menu_key(c.get())==0);CHECK(read(fs::u8path(path)).find("ui.menu_key=\n")!=std::string::npos);
    CHECK(gl_load_settings(c.get(),path.c_str())==GL_OK);CHECK(gl_get_menu_key(c.get())==0);
    CHECK(gl_set_menu_key(c.get(),25)==GL_INVALID);CHECK(gl_get_menu_key(c.get())==0);
    // Invalid shortcuts reject the entire transaction, including other edits.
    CHECK(gl_set_settings_path(c.get(),"")==GL_OK);CHECK(gl_setting_set(c.get(),"context.1.sensitivity_x",6)==GL_OK);
    for(const char* invalid:{"F0","F25","F01","F1.0","10","None","Space","F-1","F1x","F 2","F1 # comment"}){
        write(fs::u8path(path),std::string("schema=9\ngyro.sensitivity_x=15\nui.menu_key=")+invalid+'\n');
        CHECK(gl_load_settings(c.get(),path.c_str())==GL_INVALID);CHECK(gl_get_menu_key(c.get())==0);
        double value{};gl_setting_get(c.get(),"context.1.sensitivity_x",&value);CHECK(value==6);
    }
    write(fs::u8path(path),"schema=9\nui.menu_key= f24 \n");
    CHECK(gl_load_settings(c.get(),path.c_str())==GL_OK);CHECK(gl_get_menu_key(c.get())==24);
    write(fs::u8path(path),"schema=8\ngyro.sensitivity_x=7\n");
    CHECK(gl_load_settings(c.get(),path.c_str())==GL_OK);CHECK(gl_get_menu_key(c.get())==10);
    // A first-run import retains profiles, language and unknown keys, leaving the old file intact.
    fs::remove(fs::u8path(path));
    const std::string old="schema=8\nui.language=fr\ngyro.sensitivity_x=8\ncontext.205.sensitivity_x=9\nfuture.mod.setting=keep\n";
    write(fs::u8path(legacy),old);
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),legacy.c_str())==GL_OK);
    double value{};gl_setting_get(c.get(),"context.1.sensitivity_x",&value);CHECK(value==7); // flat legacy values never override an existing named-view configuration
    CHECK(read(fs::u8path(legacy))==old);CHECK(read(fs::u8path(path)).find("future.mod.setting=keep")!=std::string::npos);
    CHECK(read(fs::u8path(path)).find("context.205.sensitivity_x=9")!=std::string::npos);
    CHECK(gl_set_menu_key(c.get(),0)==GL_OK);CHECK(gl_setting_set(c.get(),"context.1.sensitivity_x",11)==GL_OK);
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),legacy.c_str())==GL_OK);
    gl_setting_get(c.get(),"context.1.sensitivity_x",&value);CHECK(value==11);CHECK(gl_get_menu_key(c.get())==0);
    // Existing malformed/newer files must never be replaced by a legacy import or subsequent GUI edits.
    for(const auto& entry:{std::pair{"schema=9\nui.menu_key=typo\n",GL_INVALID},std::pair{"schema=999\nfuture=keep\n",GL_NEWER_SCHEMA}}){
        write(fs::u8path(path),entry.first);
        CHECK(gl_initialize_settings(c.get(),folder.c_str(),legacy.c_str())==entry.second);
        CHECK(!*gl_get_settings_path(c.get()));CHECK(gl_get_settings_save_result(c.get())==entry.second);
        CHECK(gl_set_menu_key(c.get(),3)==GL_OK);gl_reset_settings(c.get());CHECK(read(fs::u8path(path))==entry.first);
    }
    fs::remove(fs::u8path(path));write(fs::u8path(legacy),"schema=999\n");
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),legacy.c_str())==GL_NEWER_SCHEMA);
    CHECK(!fs::exists(fs::u8path(path)));CHECK(!*gl_get_settings_path(c.get()));
    CHECK(gl_initialize_settings(c.get(),"",nullptr)==GL_INVALID);
    CHECK(gl_initialize_settings(c.get(),utf8(directory/"missing").c_str(),nullptr)==GL_IO_ERROR);
    CHECK(gl_initialize_settings(c.get(),legacy.c_str(),nullptr)==GL_IO_ERROR);
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),nullptr)==GL_OK);
    CHECK(gl_get_settings_save_result(c.get())==GL_OK);
    // The INI contains only shared preferences and explicit view profiles.
    const auto clean=[&](){const auto text=read(fs::u8path(path));
        CHECK(text.find("schema=17\n")!=std::string::npos);
        for(const char* prefix:{"\ngyro.","\nactivation.","\nflick.","\nui.scale=","\nmigration.profile."})
            CHECK(text.find(prefix)==std::string::npos);
        CHECK(text.find("context.1.sensitivity_x=")!=std::string::npos);
    };clean();
    // Upgrade an existing file immediately, preserving every named view and the empty shortcut.
    const std::string previous="schema=9\nui.menu_key=\nui.language=fr\nui.scale=2\ncalibration.automatic=2\n"
        "gyro.sensitivity_x=20\ngyro.context=1\nflick.context=1\nactivation.button=5\n"
        "context.1.sensitivity_x=4.7\ncontext.1.gyro.activation=4\ncontext.205.sensitivity_x=9\n";
    write(fs::u8path(path),previous);
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),legacy.c_str())==GL_OK);clean();
    CHECK(gl_get_menu_key(c.get())==0);CHECK(std::string(gl_get_language(c.get()))=="fr");
    gl_setting_get(c.get(),"context.1.sensitivity_x",&value);CHECK(value==4.7);
    gl_setting_get(c.get(),"context.1.gyro.activation",&value);CHECK(value==double(GL_TOGGLE));
    gl_setting_get(c.get(),"calibration.automatic",&value);CHECK(value==2);
    CHECK(read(fs::u8path(path)).find("context.205.sensitivity_x=9")!=std::string::npos);
    // An implicit-only legacy config must never be assigned to an arbitrary view.
    gyrolib::Context camera;
    const std::string single="schema=9\nui.menu_key=F8\ngyro.sensitivity_x=13\ngyro.smoothing_ms=65\ngyro.activation=4\n";
    write(fs::u8path(path),single);
    CHECK(gl_initialize_settings(camera.get(),folder.c_str(),nullptr)==GL_UNAVAILABLE);
    CHECK(read(fs::u8path(path))==single);
    CHECK(gl_register_gameplay_context(camera.get(),&view)==GL_OK);
    const gl_gameplay_context other{2,"Scope","",1};CHECK(gl_register_gameplay_context(camera.get(),&other)==GL_OK);
    CHECK(gl_initialize_settings(camera.get(),folder.c_str(),nullptr)==GL_UNAVAILABLE);
    CHECK(!*gl_get_settings_path(camera.get()));CHECK(read(fs::u8path(path))==single);
    CHECK(gl_unregister_gameplay_context(camera.get(),2)==GL_OK);
    CHECK(gl_initialize_settings(camera.get(),folder.c_str(),nullptr)==GL_OK);clean();
    gl_setting_get(camera.get(),"context.1.sensitivity_x",&value);CHECK(value==13);
    gl_setting_get(camera.get(),"context.1.gyro.smoothing_ms",&value);CHECK(value==65);
    gl_setting_get(camera.get(),"context.1.gyro.activation",&value);CHECK(value==double(GL_TOGGLE));
    CHECK(gl_get_menu_key(camera.get())==8);
    // A current file is only read at startup; ordinary startup preserves comments/order.
    const auto current=read(fs::u8path(path))+"# preserved until an edit\n";write(fs::u8path(path),current);
    CHECK(gl_initialize_settings(camera.get(),folder.c_str(),nullptr)==GL_OK);CHECK(read(fs::u8path(path))==current);
    fs::remove(fs::u8path(path));fs::remove(fs::u8path(legacy));fs::remove(directory);fs::remove(root);
    std::cout<<"girolib.ini: function keys, disabled shortcut, migration, destination precedence and failure protection passed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
