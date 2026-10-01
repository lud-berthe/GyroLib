#pragma once
#include <gyrolib/gyrolib.h>

namespace gyrolib_sdl_detail {
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
