#ifndef GYROLIB_PANEL_H
#define GYROLIB_PANEL_H
#include "gyrolib.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct gl_panel gl_panel;
/* Static optional Dear ImGui frontend. Host owns the ImGui context, frame,
 * backend and renderer. Use the bundled version consistently; do not mix ABIs.
 * Functions never create windows, render, hook, or capture system inputs. */
gl_panel* GL_CALL gl_panel_create(gl_context*,const char* utf8_settings_path);
void GL_CALL gl_panel_destroy(gl_panel*);
/* Forward host F1..F24 events as function_number=1..24. Only the configured
 * key toggles the panel on a non-repeated key-down. Empty ui.menu_key disables
 * all shortcut events. Host/native menu visibility APIs remain independent. */
void GL_CALL gl_panel_function_key(gl_panel*,uint32_t function_number,uint32_t pressed,uint32_t repeat);
void GL_CALL gl_panel_draw(gl_panel*,float viewport_width,float viewport_height,float dpi_scale);
int32_t GL_CALL gl_panel_last_result(const gl_panel*);
#ifdef __cplusplus
}
#endif
#endif
