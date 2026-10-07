#include "detail/internal.hpp"
using namespace gyrolib;

void gl_context::restore_calibrations(){
    for(auto& e:endpoints)if(e.info.source==GL_SOURCE_SDL&&e.calibration_identity){
        auto found=manual_calibrations.find(e.calibration_identity);
        e.motion.restore_calibration(found!=manual_calibrations.end()&&!ambiguous_calibrations.count(e.calibration_identity)
            ?std::optional<ManualCalibration>(found->second):std::nullopt);
    }
}
void gl_context::remember_calibration(EndpointState& e){
    const auto key=e.calibration_identity;
    if(!key||e.info.source!=GL_SOURCE_SDL||ambiguous_calibrations.count(key)||!e.motion.manual_reference())return;
    if(!manual_calibrations.count(key)&&manual_calibrations.size()>=128){settings_save_result=GL_LIMIT;return;}
    // Save only explicitly measured manual references, never automatic drift.
    manual_calibrations[key]=*e.motion.manual_reference();
    save_change();
}
extern "C" int32_t GL_CALL gl_set_endpoint_calibration_identity(gl_context* c,uint64_t id,uint64_t identity) try {
    auto* e=c?c->endpoint(id):nullptr;
    if(!e||e->info.source!=GL_SOURCE_SDL)return GL_INVALID;
    const bool changed=e->calibration_identity!=identity;
    if(changed&&e->calibration_identity)e->motion.restore_calibration(std::nullopt);
    e->calibration_identity=identity;
    if(!identity)return GL_OK;
    for(const auto& other:c->endpoints)if(&other!=e&&other.info.connected&&other.calibration_identity==identity){
        if(!c->ambiguous_calibrations.insert(identity).second)return GL_OK;
        c->manual_calibrations.erase(identity);
        for(auto& duplicate:c->endpoints)if(duplicate.calibration_identity==identity)
            duplicate.motion.restore_calibration(std::nullopt);
        return c->save_change();
    }
    if(!changed||c->ambiguous_calibrations.count(identity))return GL_OK;
    if(auto found=c->manual_calibrations.find(identity);found!=c->manual_calibrations.end())
        e->motion.restore_calibration(found->second);
    return GL_OK;
}catch(...){return GL_LIMIT;}
