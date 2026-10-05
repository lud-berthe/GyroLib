#pragma once
#include <gyrolib/gyrolib.h>
namespace gyrolib_detail {
// Private renderer-independent bridge shared by DLL adapters. Context owner
// creates/destroys; messages/raw packets may arrive on the window thread.
struct MouseBridge;
GL_API MouseBridge* mouse_bridge_create(gl_context*);
GL_API void mouse_bridge_destroy(MouseBridge*);
GL_API void mouse_bridge_window(MouseBridge*, void*);
GL_API void mouse_bridge_stop(MouseBridge*);
GL_API void mouse_bridge_panel(MouseBridge*,bool);
GL_API bool mouse_bridge_message(MouseBridge*,void*,uint32_t,uint64_t,int64_t,bool read_raw=true);
GL_API bool mouse_bridge_raw(MouseBridge*,uint64_t,bool,uint16_t,int32_t,int32_t);
}
