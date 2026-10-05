#pragma once
#include <gyrolib/gyrolib.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace gyrolib_panel_detail {
// Group only identities already associated by acquisition/the host. Prefer a
// named SDL device over the generic Steam endpoint regardless of reconnect order.
inline std::vector<gl_endpoint> panel_devices(const gl_context* c) {
    std::vector<gl_endpoint> devices;
    for(uint32_t i=0;i<gl_endpoint_count(c);++i){
        gl_endpoint e{};gl_get_endpoint(c,i,&e);
        if(!e.connected||gl_is_motion_companion(c,e.id))continue;
        auto existing=std::find_if(devices.begin(),devices.end(),[&](const auto& d){return d.physical_id==e.physical_id;});
        if(existing==devices.end())devices.push_back(e);
        else if(*e.name&&(!*existing->name||(e.source==GL_SOURCE_SDL&&existing->source!=GL_SOURCE_SDL)))*existing=e;
    }
    // A verified, bound physical sensor can name a Steam-only group. An unbound
    // sensor must never lend its name to another controller.
    for(auto& device:devices)if(device.source==GL_SOURCE_STEAM){
        const auto sensor=gl_get_motion_sensor(c,device.physical_id);
        if(!sensor)continue;
        for(uint32_t i=0;i<gl_endpoint_count(c);++i){gl_endpoint e{};gl_get_endpoint(c,i,&e);
            if(e.id==sensor&&e.connected&&*e.name){std::memcpy(device.name,e.name,sizeof(device.name));break;}}
    }
    return devices;
}
}
