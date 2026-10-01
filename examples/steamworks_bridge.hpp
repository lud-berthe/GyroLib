#pragma once
// Optional example compiled by YOUR host against YOUR licensed Steamworks SDK.
// GyroLib does not redistribute the SDK or own the Steam service lifecycle.
#include <steam/isteaminput.h>
#include <gyrolib/steam.h>
#include <algorithm>
#include <array>
#include <map>
class BorrowedSteamInput {
    ISteamInput* input_; // already initialized and updated by the host
    std::map<std::pair<uint64_t,uint32_t>,EInputActionOrigin> origins_;
    static uint32_t GL_CALL handles(void* user,uint64_t* output,uint32_t capacity) {
        auto& self=*static_cast<BorrowedSteamInput*>(user);
        std::array<InputHandle_t,STEAM_INPUT_MAX_COUNT> connected{};
        int count=self.input_->GetConnectedControllers(connected.data());
        if(count<0||count>static_cast<int>(connected.size()))return 0;
        auto n=std::min(capacity,static_cast<uint32_t>(count));
        for(uint32_t i=0;i<n;++i)output[i]=static_cast<uint64_t>(connected[i]);return n;
    }
    static uint32_t GL_CALL motion(void* user,uint64_t handle,gl_steam_motion* out) {
        auto& self=*static_cast<BorrowedSteamInput*>(user);
        auto m=self.input_->GetMotionData(static_cast<InputHandle_t>(handle));
        *out={m.posAccelX,m.posAccelY,m.posAccelZ,m.rotVelX,m.rotVelY,m.rotVelZ};
        // The adapter validates finite/range/gravity and connected-handle membership.
        // Steam exposes latest values, not a hardware packet timestamp.
        return 1;
    }
    static const char* GL_CALL label(void* user,uint64_t handle,uint32_t button) {
        auto& self=*static_cast<BorrowedSteamInput*>(user);
        auto explicit_origin=self.origins_.find({handle,button});
        EInputActionOrigin origin=k_EInputActionOrigin_None;
        if(explicit_origin!=self.origins_.end())origin=explicit_origin->second;
        else {
            // Standard normalized SDL/Xbox-equivalent buttons only. Extras need
            // the actual origin from the host's existing action, never a guess.
            constexpr EXboxOrigin equivalent[]={k_EXboxOrigin_A,k_EXboxOrigin_B,k_EXboxOrigin_X,k_EXboxOrigin_Y,
                k_EXboxOrigin_View,k_EXboxOrigin_Count,k_EXboxOrigin_Menu,k_EXboxOrigin_LeftStick_Click,
                k_EXboxOrigin_RightStick_Click,k_EXboxOrigin_LeftBumper,k_EXboxOrigin_RightBumper,
                k_EXboxOrigin_DPad_North,k_EXboxOrigin_DPad_South,k_EXboxOrigin_DPad_West,k_EXboxOrigin_DPad_East};
            if(button<std::size(equivalent)&&equivalent[button]!=k_EXboxOrigin_Count)
                origin=self.input_->GetActionOriginFromXboxOrigin(static_cast<InputHandle_t>(handle),equivalent[button]);
        }
        return origin==k_EInputActionOrigin_None?nullptr:self.input_->GetStringForActionOrigin(origin);
    }
public:
    explicit BorrowedSteamInput(ISteamInput& input):input_(&input){}
    gl_steam_provider provider(){return {this,handles,motion,nullptr};}
    void attach_labels(gl_steam* reader){gl_steam_set_button_label_provider(reader,label,this);}
    void set_button_origin(uint64_t handle,uint32_t button,EInputActionOrigin origin){
        if(button<32)origins_[{handle,button}]=origin;
    }
    void clear_button_origins(uint64_t handle){
        for(auto i=origins_.begin();i!=origins_.end();)if(i->first.first==handle)i=origins_.erase(i);else ++i;
    }
    // Optional controls callback: read existing host actions with verified origins.
    // Only report stick/grip touch capabilities for genuine contact actions.
    // Do not create or activate an action set on the host's behalf.
};
