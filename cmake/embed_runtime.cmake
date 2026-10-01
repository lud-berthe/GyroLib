# Runs after the worker has linked. Payload hashes also version the cache, so
# concurrent games and older library versions never overwrite each other's code.
file(MAKE_DIRECTORY "${OUT}")
file(SHA256 "${SDL}" sdl_hash)
file(SHA256 "${WORKER}" worker_hash)
string(SHA256 bundle_hash "GyroLib-runtime-v1;${sdl_hash};${worker_hash}")
file(WRITE "${OUT}/runtime_payload.hpp.tmp" "#pragma once\n#define GL_RUNTIME_HASH \"${bundle_hash}\"\n")
configure_file("${OUT}/runtime_payload.hpp.tmp" "${OUT}/runtime_payload.hpp" COPYONLY)
file(TO_CMAKE_PATH "${SDL}" sdl_path)
file(TO_CMAKE_PATH "${WORKER}" worker_path)
file(WRITE "${OUT}/runtime_payload.rc" "#pragma code_page(65001)\n101 RCDATA \"${sdl_path}\"\n102 RCDATA \"${worker_path}\"\n")
