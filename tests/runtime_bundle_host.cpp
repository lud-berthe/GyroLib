#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
// No GyroLib or SDL import: models an ordinary game loading its mod.
int wmain(int argc,wchar_t** argv){
    const std::wstring wide_mode=argc>1?argv[1]:L"normal";
    std::string mode;for(wchar_t ch:wide_mode){if(ch>127)return 7;mode.push_back(static_cast<char>(ch));}
    HMODULE host_sdl{};
    using WasInit=unsigned(__cdecl*)(unsigned);
    using SetHint=bool(__cdecl*)(const char*,const char*);
    using GetHint=const char*(__cdecl*)(const char*);
    WasInit was_init{};GetHint get_hint{};
    if(mode=="coexist"){
        if(argc!=3)return 3;
        host_sdl=LoadLibraryExW(argv[2],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!host_sdl)return 4;
        was_init=reinterpret_cast<WasInit>(GetProcAddress(host_sdl,"SDL_WasInit"));
        auto set_hint=reinterpret_cast<SetHint>(GetProcAddress(host_sdl,"SDL_SetHint"));
        get_hint=reinterpret_cast<GetHint>(GetProcAddress(host_sdl,"SDL_GetHint"));
        if(!was_init||!set_hint||!get_hint||was_init(0)||!set_hint("GYROLIB_HOST_SENTINEL","untouched"))return 5;
    }
    const auto mod=LoadLibraryExW(L"le_mod.dll",nullptr,LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!mod){std::fprintf(stderr,"Mod load failed: %lu\n",GetLastError());return 1;}
    using Run=int(*)(const char*);
    auto run=reinterpret_cast<Run>(GetProcAddress(mod,"mod_smoke"));
    const auto result=run?run(mode.c_str()):2;
    FreeLibrary(mod);
    if(host_sdl){
        const auto value=get_hint("GYROLIB_HOST_SENTINEL");
        if(was_init(0)||!value||std::strcmp(value,"untouched"))return 6;
        FreeLibrary(host_sdl);
    }
    return result;
}
