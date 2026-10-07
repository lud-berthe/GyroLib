#pragma once
#include <gyrolib/gyrolib.h>
#include <string>

namespace gyrolib_sdl_detail {
// A reusable port/path or a model name cannot identify an individual sensor.
// Hash the hardware serial with VID/PID so no serial is written to the INI.
inline uint64_t calibration_identity(uint32_t vendor,uint32_t product,const char* serial){
    if(!serial||!*serial)return 0;
    const auto key="gyrolib-motion-v1:"+std::to_string(vendor)+":"+std::to_string(product)+":"+serial;
    uint64_t hash=14695981039346656037ull;
    for(unsigned char byte:key){hash^=byte;hash*=1099511628211ull;}
    return hash?hash:1;
}
// A Steam handle is authoritative metadata, even when it arrives after opening
// the SDL device. Do not overwrite a different pairing supplied by the host.
inline void refresh_steam_identity(gl_context* context,gl_endpoint& endpoint,
    uint64_t native_identity,uint64_t previous_handle,uint64_t current_handle){
    if(current_handle==previous_handle)return;
    if(endpoint.physical_id!=(previous_handle?previous_handle:native_identity))return;
    const auto physical=current_handle?current_handle:native_identity;
    if(gl_associate_endpoint(context,endpoint.id,physical)==GL_OK)endpoint.physical_id=physical;
}
}
