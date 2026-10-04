#pragma once
#include <gyrolib/panel.h>
namespace gyrolib_panel_detail {
enum CommandType { Setting, Action, Language, Device, Sensor, Parent, Inherit };
struct Command { CommandType type{}; const char* id{}; double value{}; uint64_t physical{},sensor{}; };
using CommandSink=int32_t (*)(void*,const Command&);
// Private frontend plumbing, never exported as part of the C ABI.
void set_sink(gl_panel*,CommandSink,void*);
void set_context(gl_panel*,gl_context*);
void set_result(gl_panel*,int32_t);
}
