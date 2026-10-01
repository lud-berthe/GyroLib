#pragma once
#include <string>
// Empty on failure, with an actionable error. Only present in single-DLL builds.
std::string gyrolib_runtime_worker(std::string& error);
