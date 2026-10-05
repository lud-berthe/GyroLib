/* Minimal consumer, no ImGui headers/library, no automatic hooks. The host
 * supplies its existing window/Present/Resize callbacks and thread transport.
 * Compile with the prebuilt SDK: link only GyroLib::gyrolib. Stop callbacks
 * before detach/shutdown/destroy; never initialize this from DllMain. */
#include <gyrolib/overlay.h>
#include <gyrolib/sdl.h>
typedef struct gyro_mod {
    gl_context* context;
    gl_sdl* reader;
    gl_overlay* overlay;
} gyro_mod;
static int gyro_mod_start_error(gyro_mod* mod,int result){
    gl_sdl_destroy(mod->reader);gl_destroy(mod->context);
    mod->reader=0;mod->context=0;return result;
}
int gyro_mod_start(gyro_mod* mod,gl_camera_callback rotate,void* camera_user){
    const gl_gameplay_context view={1,"Camera","Look around.",0};
    int result;
    mod->context=gl_create(GL_ABI_VERSION);mod->reader=0;mod->overlay=0;
    if(!mod->context)return GL_LIMIT;
    gl_set_camera_callback(mod->context,rotate,camera_user);
    if((result=gl_register_gameplay_context(mod->context,&view))!=GL_OK)return gyro_mod_start_error(mod,result);
    if((result=gl_set_gameplay_context_output_target(mod->context,1,GL_OUTPUT_CAMERA))!=GL_OK)return gyro_mod_start_error(mod,result);
    /* Declare optional native-stick/filter/menu hooks ONLY after wiring them. */
    if((result=gl_initialize_settings(mod->context,0,0))!=GL_OK)return gyro_mod_start_error(mod,result);
    mod->reader=gl_sdl_create(mod->context,0);if(!mod->reader)return gyro_mod_start_error(mod,GL_UNAVAILABLE);
    mod->overlay=gl_overlay_create(mod->context,GL_OVERLAY_ABI_VERSION);
    return mod->overlay?GL_OK:gyro_mod_start_error(mod,GL_LIMIT);
}
/* Context owner / SDL main thread. The camera callback applies deltas once. */
int gyro_mod_update(gyro_mod* mod,uint64_t now,const gl_host_state* state){
    gl_output output;int result;
    if((result=gl_sdl_pump_events(mod->reader))!=GL_OK)return result;
    if((result=gl_sdl_poll(mod->reader,now))!=GL_OK)return result;
    if((result=gl_overlay_process(mod->overlay))!=GL_OK)return result;
    gl_set_gameplay_context_state(mod->context,1,1,1);
    return gl_update(mod->context,now,state,&output);
}
/* Render thread. Objects come from the host renderer, e.g. REFramework's public
 * renderer data. swapchain MUST implement IDXGISwapChain3; do QueryInterface
 * in the host rather than assuming an IDXGISwapChain* has that vtable. */
/* Use GL_OVERLAY_COLOR_SPACE_AUTO for standard SDR/HDR swapchains, or the
 * actual DXGI space known by the host: SDR=0, scRGB=1, HDR10=12.
 * Reinitialize on format changes or changes to an explicit color space. */
int gyro_mod_renderer_ready(gyro_mod* mod,void* hwnd,void* swapchain3,void* direct_queue,uint32_t color_space){
    const gl_overlay_dx12_desc desc={sizeof(desc),GL_OVERLAY_ABI_VERSION,color_space,0,hwnd,swapchain3,direct_queue};
    return gl_overlay_dx12_init(mod->overlay,&desc);
}
/* Forward actual Win32 messages; return is a capture mask, not an LRESULT.
 * Host forwards lifecycle messages normally and gates its own relative/raw
 * mouse and gamepad gameplay using gl_overlay_capture as well. */
uint32_t gyro_mod_message(gyro_mod* mod,void* hwnd,uint32_t msg,uint64_t wp,int64_t lp){
    return gl_overlay_win32_message(mod->overlay,hwnd,msg,wp,lp);
}
int gyro_mod_before_present(gyro_mod* mod,double dt,float dpi){
    return gl_overlay_dx12_render(mod->overlay,dt,dpi);
}
int gyro_mod_before_resize(gyro_mod* mod){return gl_overlay_dx12_before_resize(mod->overlay);}
/* Stop producers first. Detach on owner; shutdown on render; then finish on
 * owner. Keep mod/context alive until both thread steps have completed. */
int gyro_mod_detach(gyro_mod* mod){return mod->overlay?gl_overlay_detach(mod->overlay):GL_OK;}
int gyro_mod_renderer_shutdown(gyro_mod* mod){return gl_overlay_dx12_shutdown(mod->overlay);}
int gyro_mod_finish(gyro_mod* mod){
    int result=gl_overlay_destroy(mod->overlay);if(result!=GL_OK)return result;
    gl_sdl_destroy(mod->reader);gl_destroy(mod->context);
    mod->overlay=0;mod->reader=0;mod->context=0;return GL_OK;
}
