#include "detail/internal.hpp"
#include <filesystem>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
namespace fs=std::filesystem;
// A private data address identifies this module even through import/delay thunks.
const char module_anchor{};
fs::path module_directory(){
#ifdef _WIN32
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&module_anchor),&module))return {};
    std::wstring path(32768,L'\0');
    const auto count=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if(!count||count>=path.size())return {};
    path.resize(count);return fs::path(path).parent_path();
#else
    Dl_info info{};
    if(!dladdr(&module_anchor,&info)||!info.dli_fname)return {};
    return fs::absolute(info.dli_fname).parent_path();
#endif
}
}

extern "C" int32_t GL_CALL gl_initialize_settings(gl_context* c,const char* directory,const char* legacy) try {
    if(!c)return GL_INVALID;
    // Never save over a file whose load failed, nor back to the legacy location.
    c->settings_path.clear();c->settings_save_result=GL_OK;
    const auto finish=[c](int result){
        if(result!=GL_OK)c->settings_path.clear();
        c->settings_save_result=result;return result;
    };
    if(directory&&!*directory)return finish(GL_INVALID);
    const auto parent=directory?fs::absolute(fs::path(reinterpret_cast<const char8_t*>(directory))):module_directory();
    if(parent.empty()||!fs::is_directory(parent))return finish(GL_IO_ERROR);
    const auto target=parent/GL_SETTINGS_FILENAME;const auto path=target.u8string();
    const auto* utf8=reinterpret_cast<const char*>(path.c_str());
    if(fs::exists(target)){
        return finish(gl_load_settings(c,utf8));
    }
    if(legacy&&*legacy&&fs::exists(fs::path(reinterpret_cast<const char8_t*>(legacy)))){
        const auto result=gl_load_settings(c,legacy);if(result!=GL_OK)return finish(result);
        c->settings_path.clear();
    }
    return finish(gl_save_settings(c,utf8));
}catch(...){
    if(c){c->settings_path.clear();c->settings_save_result=GL_IO_ERROR;}
    return GL_IO_ERROR;
}
