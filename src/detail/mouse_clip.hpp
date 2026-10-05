#pragma once
#include <algorithm>

namespace gyrolib_detail {
struct ClipRect {
    long left{},top{},right{},bottom{};
    bool operator==(const ClipRect&) const = default;
};
// Backend injection keeps tests away from the user's real desktop. Callers
// serialize access. Never replace a restriction changed by the host meanwhile.
struct MouseClip {
    bool owned{},unrestricted{};
    ClipRect previous{},applied{};
    template<class Backend> void release(Backend& os){
        if(!owned)return;
        ClipRect current{};
        if(!os.get(current))return;
        if(current==applied&&!os.set(unrestricted?nullptr:&previous))return;
        owned=false;
    }
    template<class Backend> bool acquire(Backend& os,ClipRect client){
        ClipRect current{};
        if(!os.get(current))return false;
        if(owned&&current!=applied){owned=false;return false;}
        const auto base=owned?previous:current;
        ClipRect desired{(std::max)(client.left,base.left),(std::max)(client.top,base.top),
            (std::min)(client.right,base.right),(std::min)(client.bottom,base.bottom)};
        if(desired.left>=desired.right||desired.top>=desired.bottom){release(os);return false;}
        if(owned&&desired==applied)return true;
        // A host-owned restriction already tighter than ours needs no ownership.
        if(!owned&&desired==current)return true;
        if(!os.set(&desired))return false;
        if(!owned){previous=current;unrestricted=current==os.desktop();}
        applied=desired;owned=true;return true;
    }
};
}
