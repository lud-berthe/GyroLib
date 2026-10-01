#pragma once
#include <gyrolib/gyrolib.h>
// Acquisition fixtures still need a host-owned camera view to test rotation.
inline bool declare_test_view(gl_context* c){
    const gl_gameplay_context view{1,"Test camera","",0};
    return gl_register_gameplay_context(c,&view)==GL_OK;
}
inline int32_t update_test_view(gl_context* c,uint64_t now,const gl_host_state* host,gl_output* out){
    if(gl_set_gameplay_context_state(c,1,1,1)!=GL_OK)return GL_INVALID;
    return gl_update(c,now,host,out);
}
