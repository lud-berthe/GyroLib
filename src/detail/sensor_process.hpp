#pragma once
#include <gyrolib/gyrolib.h>
struct SensorProcess;
SensorProcess* sensor_process_create(gl_context*);
void sensor_process_destroy(SensorProcess*);
int sensor_process_path(SensorProcess*,const char*);
void sensor_process_poll(SensorProcess*,uint64_t now,bool needed);
const char* sensor_process_error(const SensorProcess*);
int sensor_process_feedback(SensorProcess*,uint64_t endpoint);
int sensor_process_feedback_result(const SensorProcess*);
