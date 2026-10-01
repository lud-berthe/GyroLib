// Stand-in for a game's mod DLL. Deliberately imports no SDL functions.
#include <gyrolib/gyrolib.h>
#include <gyrolib/sdl.h>
#include <gyrolib/runtime.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/detail/sensor_desktop.hpp"
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <string>
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"bundle:%d: %s (%s)\n",__LINE__,#x,gl_runtime_error());return __LINE__;}}while(0)
extern "C" __declspec(dllexport) int mod_smoke(const char* mode){
    // Core remains usable even if cache preparation fails.
    auto* context=gl_create(GL_ABI_VERSION);CHECK(context);
    const gl_gameplay_context view{1,"Camera","",0};CHECK(gl_register_gameplay_context(context,&view)==GL_OK);
    CHECK(gl_setting_set(context,"context.1.sensitivity_x",7)==GL_OK);
    if(std::strcmp(mode,"settings")==0){
        CHECK(gl_initialize_settings(context,nullptr,nullptr)==GL_OK);
        wchar_t library[32768]{};CHECK(GetModuleFileNameW(GetModuleHandleW(L"gyrolib.dll"),library,32768));
        const auto expected=fs::path(library).parent_path()/GL_SETTINGS_FILENAME;
        CHECK(fs::equivalent(fs::u8path(gl_get_settings_path(context)),expected));
        CHECK(gl_set_menu_key(context,0)==GL_OK);
        gl_destroy(context);context=gl_create(GL_ABI_VERSION);CHECK(context);
        CHECK(gl_initialize_settings(context,nullptr,nullptr)==GL_OK);CHECK(gl_get_menu_key(context)==0);
        CHECK(gl_register_gameplay_context(context,&view)==GL_OK);
        double value{};CHECK(gl_setting_get(context,"context.1.sensitivity_x",&value)==GL_OK&&value==7);
        gl_destroy(context);return 0;
    }
    if(std::strcmp(mode,"failure")==0){
        CHECK(gl_runtime_prepare()==GL_IO_ERROR);CHECK(std::strlen(gl_runtime_error())>0);
        double value{};CHECK(gl_setting_get(context,"context.1.sensitivity_x",&value)==GL_OK&&value==7);
        CHECK(gl_sdl_create(context,0)==nullptr);gl_destroy(context);return 0;
    }
    CHECK(gl_runtime_prepare()==GL_OK);
    const fs::path directory=fs::u8path(gl_runtime_directory());CHECK(!directory.empty());
    auto module=static_cast<HMODULE>(gl_runtime_sdl_handle());CHECK(module);
    wchar_t path[32768]{};CHECK(GetModuleFileNameW(module,path,32768));
    CHECK(fs::equivalent(directory/L"SDL3.dll",path));
    if(std::strcmp(mode,"lazy")==0)CHECK(!fs::exists(directory/L"gyrolib_sensor_worker.exe"));
    // Entry through only the C APIs: no hidden requirement to initialize SDL
    // from the mod, nor an eager SDL import beside the game.
    auto* reader=gl_sdl_create(context,0);CHECK(reader);
    CHECK(gl_sdl_pump_events(reader)==GL_OK);
    CHECK(gl_sdl_poll(reader,1000000000)==GL_OK);
    gl_sdl_destroy(reader);gl_destroy(context);
    if(std::strcmp(mode,"lazy")==0)return 0;
    CHECK(gl_runtime_prepare_sensor_worker()==GL_OK);
    const auto worker=directory/L"gyrolib_sensor_worker.exe";CHECK(fs::exists(worker));
    if(std::strcmp(mode,"hold")==0){Sleep(400);return 0;}
    // Exercise the exact extracted production worker over the protected named
    // pipe used for Steam launches. No fixture can mask missing dependencies.
    DesktopSensor sensor;std::string error;
    const auto bytes=worker.u8string();const std::string executable(bytes.begin(),bytes.end());
    const bool desktop=std::strcmp(mode,"desktop")==0;
    PROCESS_INFORMATION child{};
    if(desktop){
        const bool launched=sensor_desktop_start(sensor,executable,error);
        if(!launched)std::fprintf(stderr,"Desktop launch: %s\n",error.c_str());CHECK(launched);
    }
    else {
        // Same authenticated named-pipe transport, direct hidden launch. Shell
        // launch is covered separately so ordinary CI does not need Explorer.
        GUID id{};CHECK(SUCCEEDED(CoCreateGuid(&id)));wchar_t text[40]{};StringFromGUID2(id,text,40);
        const std::wstring pipe=std::wstring(L"\\\\.\\pipe\\GyroLib-sensor-")+text;
        sensor.pipe=CreateNamedPipeW(pipe.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_NOWAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,nullptr);
        CHECK(sensor.pipe!=INVALID_HANDLE_VALUE);ConnectNamedPipe(sensor.pipe,nullptr);
        sensor.executable=worker.lexically_normal().make_preferred().native();
        auto command=L"\""+sensor.executable+L"\" --named-pipe-v1 "+pipe;
        STARTUPINFOW startup{sizeof(startup)};
        CHECK(CreateProcessW(sensor.executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,
            directory.c_str(),&startup,&child));CloseHandle(child.hThread);
    }
    gyrolib_sensor::Record record{};size_t received{};const auto deadline=GetTickCount64()+5000;
    while(received<sizeof(record)&&GetTickCount64()<deadline){
        received+=sensor_desktop_read(sensor,reinterpret_cast<char*>(&record)+received,sizeof(record)-received);Sleep(1);
    }
    const bool hello=received==sizeof(record)&&record.signature==gyrolib_sensor::magic&&record.revision==gyrolib_sensor::version&&record.kind==gyrolib_sensor::Hello;
    HANDLE observed{};if(sensor.process)DuplicateHandle(GetCurrentProcess(),sensor.process,GetCurrentProcess(),&observed,SYNCHRONIZE,FALSE,0);
    sensor_desktop_stop(sensor);
    const bool stopped=observed&&WaitForSingleObject(observed,1000)==WAIT_OBJECT_0;
    if(observed)CloseHandle(observed);if(child.hProcess)CloseHandle(child.hProcess);
    if(!error.empty())std::fprintf(stderr,"%s\n",error.c_str());CHECK(hello);CHECK(stopped);
    std::printf("Single-DLL mod, verified SDL and bundled reader passed (%s)\n",mode);return 0;
}
