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
    CHECK(std::string(GL_SETTINGS_FILENAME)=="gyrolib.ini");
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
    CHECK(gl_get_gamepad_menu_shortcut(c.get())==1);
    CHECK(gl_set_gamepad_menu_shortcut(c.get(),0)==GL_OK);
    CHECK(read(fs::u8path(path)).find("ui.gamepad_menu_shortcut=0\n")!=std::string::npos);
    CHECK(gl_load_settings(c.get(),path.c_str())==GL_OK&&!gl_get_gamepad_menu_shortcut(c.get()));
    gl_reset_settings(c.get());CHECK(!gl_get_gamepad_menu_shortcut(c.get()));
    for(const char* invalid:{"", "2", "-1", "true", "01", "1.0"}){
        write(fs::u8path(path),std::string("schema=0.2.0\nui.menu_key=F1\nui.gamepad_menu_shortcut=")+invalid+'\n');
        CHECK(gl_load_settings(c.get(),path.c_str())==GL_INVALID);
        CHECK(gl_get_menu_key(c.get())==0&&!gl_get_gamepad_menu_shortcut(c.get()));
    }
    // Invalid shortcuts reject the entire transaction, including other edits.
    CHECK(gl_set_settings_path(c.get(),"")==GL_OK);CHECK(gl_setting_set(c.get(),"context.1.sensitivity_x",6)==GL_OK);
    for(const char* invalid:{"F0","F25","F01","F1.0","10","None","Space","F-1","F1x","F 2","F1 # comment"}){
        write(fs::u8path(path),std::string("schema=0.2.0\ncontext.1.sensitivity_x=15\nui.menu_key=")+invalid+'\n');
        CHECK(gl_load_settings(c.get(),path.c_str())==GL_INVALID);CHECK(gl_get_menu_key(c.get())==0);
        double value{};gl_setting_get(c.get(),"context.1.sensitivity_x",&value);CHECK(value==6);
    }
    write(fs::u8path(path),"schema=0.2.0\nui.menu_key= f24 \n");
    CHECK(gl_load_settings(c.get(),path.c_str())==GL_OK);CHECK(gl_get_menu_key(c.get())==24);CHECK(gl_get_gamepad_menu_shortcut(c.get())==1);
    write(fs::u8path(path),"schema=0.2.0\ncontext.1.sensitivity_x=7\n");
    CHECK(gl_load_settings(c.get(),path.c_str())==GL_OK);CHECK(gl_get_menu_key(c.get())==10);
    // A first-run import retains profiles, language and unknown keys, leaving the old file intact.
    fs::remove(fs::u8path(path));
    const std::string old="schema=0.2.0\nui.language=fr\ncontext.205.sensitivity_x=9\nfuture.mod.setting=keep\n";
    write(fs::u8path(legacy),old);
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),legacy.c_str())==GL_OK);
    double value{};gl_setting_get(c.get(),"context.1.sensitivity_x",&value);CHECK(value==7); // importing another saved view leaves this one unchanged
    CHECK(read(fs::u8path(legacy))==old);CHECK(read(fs::u8path(path)).find("future.mod.setting=keep")!=std::string::npos);
    CHECK(read(fs::u8path(path)).find("context.205.sensitivity_x=9")!=std::string::npos);
    CHECK(gl_set_menu_key(c.get(),0)==GL_OK);CHECK(gl_setting_set(c.get(),"context.1.sensitivity_x",11)==GL_OK);
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),legacy.c_str())==GL_OK);
    gl_setting_get(c.get(),"context.1.sensitivity_x",&value);CHECK(value==11);CHECK(gl_get_menu_key(c.get())==0);
    // Existing malformed/newer files must never be replaced by a legacy import or subsequent GUI edits.
    for(const auto& entry:{std::pair{"schema=0.2.0\nui.menu_key=typo\n",GL_INVALID},std::pair{"schema=999.0.0\nfuture=keep\n",GL_NEWER_SCHEMA}}){
        write(fs::u8path(path),entry.first);
        CHECK(gl_initialize_settings(c.get(),folder.c_str(),legacy.c_str())==entry.second);
        CHECK(!*gl_get_settings_path(c.get()));CHECK(gl_get_settings_save_result(c.get())==entry.second);
        CHECK(gl_set_menu_key(c.get(),3)==GL_OK);gl_reset_settings(c.get());CHECK(read(fs::u8path(path))==entry.first);
    }
    fs::remove(fs::u8path(path));write(fs::u8path(legacy),"schema=999.0.0\n");
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),legacy.c_str())==GL_NEWER_SCHEMA);
    CHECK(!fs::exists(fs::u8path(path)));CHECK(!*gl_get_settings_path(c.get()));
    CHECK(gl_initialize_settings(c.get(),"",nullptr)==GL_INVALID);
    CHECK(gl_initialize_settings(c.get(),utf8(directory/"missing").c_str(),nullptr)==GL_IO_ERROR);
    CHECK(gl_initialize_settings(c.get(),legacy.c_str(),nullptr)==GL_IO_ERROR);
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),nullptr)==GL_OK);
    CHECK(gl_get_settings_save_result(c.get())==GL_OK);
    // The INI contains only shared preferences and explicit view profiles.
    const auto clean=[&](){const auto text=read(fs::u8path(path));
        CHECK(text.find("schema=0.2.0\n")!=std::string::npos);
        for(const char* prefix:{"\ngyro.","\nactivation.","\nflick.","\nui.scale=","\nmigration.profile."})
            CHECK(text.find(prefix)==std::string::npos);
        CHECK(text.find("context.1.sensitivity_x=")!=std::string::npos);
    };clean();
    // The same format keeps named views and shared preferences without a rewrite.
    const std::string previous="schema=0.2.0\nui.menu_key=\nui.language=fr\ncalibration.automatic=2\n"
        "context.1.sensitivity_x=4.7\ncontext.1.gyro.activation=4\ncontext.205.sensitivity_x=9\n";
    write(fs::u8path(path),previous);
    CHECK(gl_initialize_settings(c.get(),folder.c_str(),legacy.c_str())==GL_OK);
    CHECK(read(fs::u8path(path))==previous);
    CHECK(gl_get_menu_key(c.get())==0);CHECK(std::string(gl_get_language(c.get()))=="fr");
    gl_setting_get(c.get(),"context.1.sensitivity_x",&value);CHECK(value==4.7);
    gl_setting_get(c.get(),"context.1.gyro.activation",&value);CHECK(value==double(GL_TOGGLE));
    gl_setting_get(c.get(),"calibration.automatic",&value);CHECK(value==2);
    CHECK(gl_save_settings(c.get(),path.c_str())==GL_OK);clean();
    gyrolib::Context camera;CHECK(gl_register_gameplay_context(camera.get(),&view)==GL_OK);
    // A current file is only read at startup; ordinary startup preserves comments/order.
    const auto current=read(fs::u8path(path))+"# preserved until an edit\n";write(fs::u8path(path),current);
    CHECK(gl_initialize_settings(camera.get(),folder.c_str(),nullptr)==GL_OK);CHECK(read(fs::u8path(path))==current);
    // No implicit schema and no retained pre-release migrations. Every refusal
    // is transactional and also protects an explicit save to that destination.
    CHECK(gl_set_settings_path(camera.get(),"")==GL_OK);
    CHECK(gl_setting_set(camera.get(),"context.1.sensitivity_x",6)==GL_OK);
    const auto refuses=[&](const std::string& data,int expected){
        write(fs::u8path(path),data);
        CHECK(gl_initialize_settings(camera.get(),folder.c_str(),nullptr)==expected);
        CHECK(!*gl_get_settings_path(camera.get()));
        CHECK(gl_setting_get(camera.get(),"context.1.sensitivity_x",&value)==GL_OK&&value==6);
        CHECK(gl_save_settings(camera.get(),path.c_str())==expected);
        CHECK(read(fs::u8path(path))==data);
    };
    for(int old=1;old<=18;++old)refuses("schema="+std::to_string(old)+"\ncontext.1.sensitivity_x=19\n",GL_INVALID);
    for(const char* invalid:{"", "0.1.0", "0.2", "0.2.0.0", "00.2.0", "0.-2.0", "0.2.0-dev", "x.y.z", "0.4294967296.0"})
        refuses(std::string("schema=")+invalid+"\ncontext.1.sensitivity_x=19\n",GL_INVALID);
    refuses("context.1.sensitivity_x=19\n",GL_INVALID);
    refuses("schema=0.2.0\nschema=0.2.0\n",GL_INVALID);
    for(const char* newer:{"0.2.1","0.10.0","1.0.0"})
        refuses(std::string("schema=")+newer+"\ncontext.1.sensitivity_x=19\n",GL_NEWER_SCHEMA);
    fs::remove(fs::u8path(path));fs::remove(fs::u8path(legacy));fs::remove(directory);fs::remove(root);
    std::cout<<"gyrolib.ini: function keys, disabled shortcut, format validation, destination precedence and failure protection passed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
