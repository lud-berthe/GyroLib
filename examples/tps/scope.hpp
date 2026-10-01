#pragma once
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace tps {
inline void scope_overlay(ImDrawList* draw,ImVec2 size){
    const float scale=std::clamp(std::min(size.x/1440.f,size.y/900.f),.65f,2.6f);
    const ImVec2 center(size.x*.5f,size.y*.5f);
    const float radius=std::min(size.x*.46f,size.y*.40f);
    const ImU32 black=IM_COL32(4,7,10,255);
    draw->AddRectFilled({0,0},{size.x,center.y-radius},black);
    draw->AddRectFilled({0,center.y+radius},size,black);
    draw->AddRectFilled({0,center.y-radius},{center.x-radius,center.y+radius},black);
    draw->AddRectFilled({center.x+radius,center.y-radius},{size.x,center.y+radius},black);
    // Fill the four corners outside the lens; the world remains untouched inside.
    constexpr int steps=48;
    const auto point=[&](float angle){return ImVec2(center.x+radius*std::cos(angle),center.y+radius*std::sin(angle));};
    for(int quadrant=0;quadrant<4;++quadrant){
        const ImVec2 corner(center.x+(quadrant==0||quadrant==3?radius:-radius),
                            center.y+(quadrant<2?radius:-radius));
        for(int i=0;i<steps;++i){
            const float a=(quadrant+float(i)/steps)*std::numbers::pi_v<float>/2;
            const float b=(quadrant+float(i+1)/steps)*std::numbers::pi_v<float>/2;
            draw->AddTriangleFilled(corner,point(a),point(b),black);
        }
    }
    draw->AddCircle(center,radius+4*scale,IM_COL32(24,31,37,255),192,9*scale);
    draw->AddCircle(center,radius-2*scale,IM_COL32(109,126,132,180),192,1.5f*scale);
    draw->AddCircle(center,radius-7*scale,IM_COL32(4,12,19,150),192,8*scale);
    const ImU32 ink=IM_COL32(9,17,21,255),outline=IM_COL32(226,240,239,105);
    const float reach=radius-15*scale;
    const auto line=[&](ImVec2 a,ImVec2 b,float weight){
        draw->AddLine(a,b,outline,weight+1.6f*scale);
        draw->AddLine(a,b,ink,weight);
    };
    line({center.x-reach,center.y},{center.x+reach,center.y},1.1f*scale);
    line({center.x,center.y-reach},{center.x,center.y+reach},1.1f*scale);
    for(int sign:{-1,1}){
        line({center.x+sign*radius*.24f,center.y},{center.x+sign*reach,center.y},3*scale);
        line({center.x,center.y+sign*radius*.24f},{center.x,center.y+sign*reach},3*scale);
        for(int i=1;i<=3;++i){
            const float distance=sign*i*radius*.065f,tick=(i==2?5.f:3.f)*scale;
            line({center.x+distance,center.y-tick},{center.x+distance,center.y+tick},scale);
            line({center.x-tick,center.y+distance},{center.x+tick,center.y+distance},scale);
        }
    }
    draw->AddCircleFilled(center,1.8f*scale,IM_COL32(233,112,80,255));
}
}
