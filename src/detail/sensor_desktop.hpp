#pragma once
#ifdef _WIN32
#include "sensor_wire.hpp"
// Windows process isolation. Explorer launches only our acquisition executable;
// no code is injected and neither Steam nor the game is patched.
#include <windows.h>
#include <shlobj.h>
#include <shldisp.h>
#include <exdisp.h>
#include <servprov.h>
#include <wrl/client.h>
#include <sddl.h>
#include <filesystem>
#include <string>
#include <vector>

struct DesktopSensor {
    HANDLE pipe=INVALID_HANDLE_VALUE,process{};
    std::wstring executable;bool connected{};
};
inline HRESULT sensor_shell_execute(const std::wstring& exe,const std::wstring& args){
    using Microsoft::WRL::ComPtr;
    const auto init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return init;
    struct CoCleanup{bool owned;~CoCleanup(){if(owned)CoUninitialize();}} cleanup{SUCCEEDED(init)};
    ComPtr<IShellWindows> windows;HRESULT hr=CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&windows));
    if(FAILED(hr))return hr;
    VARIANT location{},empty{};location.vt=VT_I4;location.lVal=CSIDL_DESKTOP;long hwnd=0;ComPtr<IDispatch> desktop;
    hr=windows->FindWindowSW(&location,&empty,SWC_DESKTOP,&hwnd,SWFO_NEEDDISPATCH,&desktop);if(FAILED(hr)||!desktop)return E_FAIL;
    ComPtr<IServiceProvider> provider;hr=desktop.As(&provider);if(FAILED(hr))return hr;
    ComPtr<IShellBrowser> browser;hr=provider->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser));if(FAILED(hr))return hr;
    ComPtr<IShellView> view;hr=browser->QueryActiveShellView(&view);if(FAILED(hr))return hr;
    ComPtr<IDispatch> background;hr=view->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&background));if(FAILED(hr))return hr;
    ComPtr<IShellFolderViewDual> folder;hr=background.As(&folder);if(FAILED(hr))return hr;
    ComPtr<IDispatch> app;hr=folder->get_Application(&app);if(FAILED(hr))return hr;
    ComPtr<IShellDispatch2> shell;hr=app.As(&shell);if(FAILED(hr))return hr;
    VARIANT parameters{},directory{},verb{},show{};parameters.vt=directory.vt=verb.vt=VT_BSTR;show.vt=VT_I4;show.lVal=SW_HIDE;
    parameters.bstrVal=SysAllocString(args.c_str());directory.bstrVal=SysAllocString(std::filesystem::path(exe).parent_path().c_str());verb.bstrVal=SysAllocString(L"open");
    auto file=SysAllocString(exe.c_str());
    hr=file&&parameters.bstrVal&&directory.bstrVal&&verb.bstrVal?shell->ShellExecute(file,parameters,directory,verb,show):E_OUTOFMEMORY;
    SysFreeString(file);VariantClear(&parameters);VariantClear(&directory);VariantClear(&verb);return hr;
}
inline bool sensor_desktop_start(DesktopSensor& d,const std::string& path,std::string& error){
    d.executable=std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())).lexically_normal().make_preferred().native();
    if(GetFileAttributesW(d.executable.c_str())==INVALID_FILE_ATTRIBUTES){error="Sensor worker executable missing";return false;}
    GUID id{};if(FAILED(CoCreateGuid(&id))){error="Cannot create sensor pipe identity";return false;}
    wchar_t guid[40]{};StringFromGUID2(id,guid,40);
    const std::wstring name=std::wstring(L"\\\\.\\pipe\\GyroLib-sensor-")+guid;
    // Restrict this unpredictable local pipe to the current user and SYSTEM.
    HANDLE token{};DWORD bytes=0;PSECURITY_DESCRIPTOR descriptor{};LPWSTR sid{};
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){error="Cannot obtain sensor pipe owner";return false;}
    GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<unsigned char> user(bytes);
    bool security=GetTokenInformation(token,TokenUser,user.data(),bytes,&bytes)&&ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid);
    if(security){const std::wstring sddl=std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;")+sid+L")";security=ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=0;}
    if(sid)LocalFree(sid);CloseHandle(token);
    if(!security){error="Cannot restrict sensor pipe access";return false;}
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
    d.pipe=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_NOWAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&attributes);
    LocalFree(descriptor);
    if(d.pipe==INVALID_HANDLE_VALUE){error="Cannot create sensor pipe: "+std::to_string(GetLastError());return false;}
    ConnectNamedPipe(d.pipe,nullptr);
    auto hr=sensor_shell_execute(d.executable,L"--named-pipe-v1 "+name);
    if(FAILED(hr)){CloseHandle(d.pipe);d.pipe=INVALID_HANDLE_VALUE;error="Cannot launch isolated reader through desktop: "+std::to_string(static_cast<uint32_t>(hr));return false;}
    return true;
}
inline size_t sensor_desktop_read(DesktopSensor& d,void* data,size_t size){
    if(!d.connected){
        ULONG pid=0;if(!GetNamedPipeClientProcessId(d.pipe,&pid))return 0;
        auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE|PROCESS_TERMINATE,FALSE,pid);
        if(!process)return 0;
        wchar_t path[32768];DWORD count=32768;
        if(!QueryFullProcessImageNameW(process,0,path,&count)||CompareStringOrdinal(path,count,d.executable.c_str(),static_cast<int>(d.executable.size()),TRUE)!=CSTR_EQUAL){CloseHandle(process);return 0;}
        d.process=process;d.connected=true;
    }
    DWORD read=0;return ReadFile(d.pipe,data,static_cast<DWORD>(size),&read,nullptr)?read:0;
}
inline bool sensor_desktop_exited(const DesktopSensor& d){return d.process&&WaitForSingleObject(d.process,0)==WAIT_OBJECT_0;}
inline void sensor_desktop_stop(DesktopSensor& d){
    if(d.pipe!=INVALID_HANDLE_VALUE){
        const gyrolib_sensor::Command q{.kind=gyrolib_sensor::Quit};DWORD written=0;WriteFile(d.pipe,&q,sizeof(q),&written,nullptr);
        // Closing the pipe also unblocks a writer if the host stopped polling.
        CloseHandle(d.pipe);d.pipe=INVALID_HANDLE_VALUE;
    }
    if(d.process){if(WaitForSingleObject(d.process,100)==WAIT_TIMEOUT){TerminateProcess(d.process,0);WaitForSingleObject(d.process,100);}CloseHandle(d.process);d.process=nullptr;}
    d.connected=false;
}
#endif
