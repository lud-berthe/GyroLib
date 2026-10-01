#include <gyrolib/runtime.h>
#include "detail/runtime.hpp"
#if defined(GL_SINGLE_DLL)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <sddl.h>
#include <filesystem>
#include <array>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>
#include "runtime_payload.hpp"

namespace {
namespace fs=std::filesystem;
thread_local std::string last_error,directory_text;
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    Handle()=default;explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    HANDLE release(){const auto h=value;value=INVALID_HANDLE_VALUE;return h;}
};
struct Runtime {
    std::mutex mutex;
    fs::path directory;
    HMODULE sdl{};
    Handle sdl_file,worker_file;
};
Runtime& runtime(){static Runtime instance;return instance;}
[[noreturn]] void fail(const char* operation){
    throw std::runtime_error(std::string(operation)+" (Windows "+std::to_string(GetLastError())+")");
}
std::string utf8(const fs::path& path){const auto s=path.u8string();return {s.begin(),s.end()};}
struct Payload {const unsigned char* bytes;DWORD size;};
Payload payload(int id){
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&gl_runtime_prepare),&module))fail("Cannot locate GyroLib resources");
    const auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
    const auto size=resource?SizeofResource(module,resource):0;
    const auto loaded=resource?LoadResource(module,resource):nullptr;
    const auto bytes=loaded?static_cast<const unsigned char*>(LockResource(loaded)):nullptr;
    if(!bytes||!size)throw std::runtime_error("GyroLib runtime payload is missing");
    return {bytes,size};
}
struct UserSecurity {
    PSECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
    UserSecurity(){
        HANDLE raw{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))fail("Cannot read cache owner");
        Handle token(raw);DWORD size{};GetTokenInformation(raw,TokenUser,nullptr,0,&size);
        std::vector<unsigned char> data(size);
        if(!GetTokenInformation(raw,TokenUser,data.data(),size,&size))fail("Cannot read cache owner");
        LPWSTR sid{};
        if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))fail("Cannot read cache SID");
        const std::wstring rules=std::wstring(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;")+sid+L")";LocalFree(sid);
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(rules.c_str(),SDDL_REVISION_1,&descriptor,nullptr))fail("Cannot protect runtime cache");
        attributes.lpSecurityDescriptor=descriptor;
    }
    ~UserSecurity(){if(descriptor)LocalFree(descriptor);}
};
fs::path cache_root(){
    // Explicit development/portable override; it can only select the destination
    // of these exact resources, never an alternate library or executable.
    DWORD count=GetEnvironmentVariableW(L"GYROLIB_RUNTIME_CACHE",nullptr,0);
    fs::path root;
    if(count){
        std::vector<wchar_t> value(count);
        if(!GetEnvironmentVariableW(L"GYROLIB_RUNTIME_CACHE",value.data(),count))fail("Cannot read runtime cache override");
        root=value.data();
    }else{
        PWSTR local{};
        if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&local)))throw std::runtime_error("Cannot locate LocalAppData for GyroLib");
        root=fs::path(local)/L"GyroLib"/L"runtime";CoTaskMemFree(local);
    }
    if(!root.is_absolute()||root.native().starts_with(L"\\\\"))throw std::runtime_error("GyroLib runtime cache must be an absolute local path");
    return root.lexically_normal();
}
void ensure_directory(const fs::path& path,UserSecurity& security){
    fs::path current=path.root_path();
    for(const auto& part:path.relative_path()){
        current/=part;
        if(!CreateDirectoryW(current.c_str(),&security.attributes)&&GetLastError()!=ERROR_ALREADY_EXISTS)fail("Cannot create GyroLib runtime cache");
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(attributes&FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("GyroLib runtime cache contains a redirect or a non-directory");
    }
}
HANDLE verified_file(const fs::path& path,Payload data){
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(file.value==INVALID_HANDLE_VALUE)return INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER length{};
    if(!GetFileInformationByHandle(file.value,&info)||info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)||
       !GetFileSizeEx(file.value,&length)||length.QuadPart!=data.size)return INVALID_HANDLE_VALUE;
    std::array<unsigned char,16384> buffer{};DWORD offset=0;
    while(offset<data.size){
        const DWORD size=static_cast<DWORD>((std::min)(buffer.size(),static_cast<size_t>(data.size-offset)));DWORD read{};
        if(!ReadFile(file.value,buffer.data(),size,&read,nullptr)||read!=size||std::memcmp(buffer.data(),data.bytes+offset,size))return INVALID_HANDLE_VALUE;
        offset+=size;
    }
    return file.release(); // Deny replacement/writes while this version is in use.
}
HANDLE materialize(Runtime& state,int id,const wchar_t* name){
    UserSecurity security;
    if(state.directory.empty())state.directory=cache_root()/GL_RUNTIME_HASH;
    ensure_directory(state.directory,security);
    const auto path=state.directory/name;const auto data=payload(id);
    if(auto file=verified_file(path,data);file!=INVALID_HANDLE_VALUE)return file;
    // Cross-process serialization with a bounded wait. No global mutex, service
    // or registry setting, and no overwrite of another version's payload.
    Handle lock;const auto deadline=GetTickCount64()+5000;
    do{
        lock.value=CreateFileW((state.directory/L"runtime.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,&security.attributes,
                              OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(lock.value!=INVALID_HANDLE_VALUE)break;
        if(GetLastError()!=ERROR_SHARING_VIOLATION||GetTickCount64()>=deadline)fail("Cannot lock GyroLib runtime cache");
        Sleep(10);
    }while(true);
    BY_HANDLE_FILE_INFORMATION lock_info{};
    if(!GetFileInformationByHandle(lock.value,&lock_info)||lock_info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        throw std::runtime_error("GyroLib runtime cache lock is redirected");
    if(auto file=verified_file(path,data);file!=INVALID_HANDLE_VALUE)return file;
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot create runtime cache temporary identity");
    wchar_t suffix[40]{};StringFromGUID2(guid,suffix,40);
    const auto temp=state.directory/(std::wstring(name)+suffix+L".tmp");
    struct TempCleanup {fs::path path;~TempCleanup(){DeleteFileW(path.c_str());}} cleanup{temp};
    {
        Handle output(CreateFileW(temp.c_str(),GENERIC_WRITE,0,&security.attributes,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(output.value==INVALID_HANDLE_VALUE)fail("Cannot write GyroLib runtime cache");
        DWORD written{};
        if(!WriteFile(output.value,data.bytes,data.size,&written,nullptr)||written!=data.size||!FlushFileBuffers(output.value))fail("Cannot finish GyroLib runtime cache file");
    }
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))fail("Cannot replace damaged GyroLib runtime cache file");
    auto file=verified_file(path,data);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("GyroLib runtime cache verification failed");
    return file;
}
bool prepare(Runtime& state){
    if(state.sdl)return true;
    if(state.sdl_file.value==INVALID_HANDLE_VALUE)state.sdl_file.value=materialize(state,101,L"SDL3.dll");
    // Absolute path and system dependencies only. Do not change the game's DLL
    // search path, load its SDL by basename, or execute extraction in DllMain.
    state.sdl=LoadLibraryExW((state.directory/L"SDL3.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!state.sdl)fail("Cannot load bundled SDL3");
    return true;
}
}
extern "C" {
int32_t GL_CALL gl_runtime_prepare(void) try {
    auto& state=runtime();std::lock_guard lock(state.mutex);last_error.clear();prepare(state);return GL_OK;
}catch(const std::exception& e){last_error=e.what();return GL_IO_ERROR;}catch(...){last_error="Cannot prepare GyroLib runtime";return GL_LIMIT;}
const char* GL_CALL gl_runtime_error(void){return last_error.c_str();}
const char* GL_CALL gl_runtime_directory(void) try {
    auto& state=runtime();std::lock_guard lock(state.mutex);directory_text=utf8(state.directory);return directory_text.c_str();
}catch(...){return "";}
void* GL_CALL gl_runtime_sdl_handle(void){
    if(gl_runtime_prepare()!=GL_OK)return nullptr;return runtime().sdl;
}
int32_t GL_CALL gl_runtime_prepare_sensor_worker(void){
    return gyrolib_runtime_worker(last_error).empty()?GL_IO_ERROR:GL_OK;
}
}
std::string gyrolib_runtime_worker(std::string& error) try {
    auto& state=runtime();std::lock_guard lock(state.mutex);
    if(state.worker_file.value==INVALID_HANDLE_VALUE)state.worker_file.value=materialize(state,102,L"gyrolib_sensor_worker.exe");
    // Worker imports this exact SDL by the standard executable-directory rule.
    if(!state.sdl)prepare(state);
    error.clear();return utf8(state.directory/L"gyrolib_sensor_worker.exe");
}catch(const std::exception& e){error=e.what();return {};}catch(...){error="Cannot prepare isolated sensor reader";return {};}
#else
extern "C" {
int32_t GL_CALL gl_runtime_prepare(void){return GL_UNAVAILABLE;}
const char* GL_CALL gl_runtime_error(void){return "Bundled runtime is not enabled in this build";}
const char* GL_CALL gl_runtime_directory(void){return "";}
void* GL_CALL gl_runtime_sdl_handle(void){return nullptr;}
int32_t GL_CALL gl_runtime_prepare_sensor_worker(void){return GL_UNAVAILABLE;}
}
#endif
