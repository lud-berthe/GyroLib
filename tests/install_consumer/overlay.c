/* Plain C consumer: no Windows, SDL or ImGui headers needed for lifecycle. */
#include <gyrolib/overlay.h>
int main(void) {
    gl_context* context=gl_create(GL_ABI_VERSION);
    gl_overlay* overlay;
    if(!context)return 1;
    overlay=gl_overlay_create(context,GL_OVERLAY_ABI_VERSION);
    if(!overlay)return 2;
    if(gl_overlay_process(overlay)!=GL_OK)return 3;
    if(gl_overlay_capture(overlay)!=0)return 4;
    if(gl_overlay_detach(overlay)!=GL_OK)return 5;
    if(gl_overlay_destroy(overlay)!=GL_OK)return 6;
    gl_destroy(context);
    return 0;
}
