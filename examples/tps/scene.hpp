#pragma once
#include "host.hpp"
#include "raster.hpp"
#include "scope.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <vector>

namespace tps {
inline ImU32 color(int r,int g,int b,int a=255){return IM_COL32(r,g,b,a);}
struct Projector {
    V3 eye,right,up,forward;double focal;ImVec2 size;
    Projector(const Host& h,ImVec2 viewport):eye(h.eye()),right{std::cos(h.yaw*rad),0,-std::sin(h.yaw*rad)},
        up{-std::sin(h.yaw*rad)*std::sin(h.view_pitch()*rad),std::cos(h.view_pitch()*rad),-std::cos(h.yaw*rad)*std::sin(h.view_pitch()*rad)},
        forward(h.forward()),focal(viewport.y/(2*std::tan(h.fov()*rad/2))),size(viewport){}
    RenderVertex view(V3 point)const{
        const auto delta=point-eye;return {float(dot(delta,right)),float(dot(delta,up)),float(dot(delta,forward))};
    }
};
struct Face {std::array<V3,4> points;ImU32 tint;};
struct Scene {
    std::vector<Face> faces;
    Raster raster;
    SDL_Texture* texture{};
    Scene()=default;Scene(const Scene&)=delete;Scene& operator=(const Scene&)=delete;
    ~Scene(){release();}
    void release(){if(texture){SDL_DestroyTexture(texture);texture=nullptr;}}
    void quad(std::array<V3,4> points,ImU32 tint){faces.push_back({points,tint});}
    void box(V3 base,V3 size,ImU32 tint,double yaw=0){
        std::array<V3,8> p;for(int i=0;i<8;++i){
            double x=((i&1)?.5:-.5)*size.x,z=((i&2)?.5:-.5)*size.z;
            p[i]=base+V3{x*std::cos(yaw)+z*std::sin(yaw),(i&4)?size.y:0,-x*std::sin(yaw)+z*std::cos(yaw)};
        }
        constexpr int sides[][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
        const float shades[]={.55f,1.12f,.92f,.7f,.8f,1.0f};
        auto rgba=ImGui::ColorConvertU32ToFloat4(tint);
        for(unsigned i=0;i<6;++i){auto c=rgba;c.x*=shades[i];c.y*=shades[i];c.z*=shades[i];
            quad({p[sides[i][0]],p[sides[i][1]],p[sides[i][2]],p[sides[i][3]]},ImGui::ColorConvertFloat4ToU32(c));}
    }
    void weapon_box(const Host& host,V3 base,V3 size,ImU32 tint){
        std::array<V3,8> p;for(int i=0;i<8;++i)p[i]=host.weapon_point(base+V3{((i&1)?.5:-.5)*size.x,(i&4)?size.y:0,((i&2)?.5:-.5)*size.z});
        constexpr int sides[][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
        for(auto& side:sides)quad({p[side[0]],p[side[1]],p[side[2]],p[side[3]]},tint);
    }
    void limb(V3 a,V3 b,double radius,ImU32 tint){
        const auto delta=b-a;const double length=std::sqrt(dot(delta,delta));if(length<1e-6)return;
        const auto axis=delta*(1/length);
        const auto cross=[](V3 x,V3 y){return V3{x.y*y.z-x.z*y.y,x.z*y.x-x.x*y.z,x.x*y.y-x.y*y.x};};
        auto side=cross(axis,std::abs(axis.y)<.9?V3{0,1,0}:V3{1,0,0});side=side*(radius/std::sqrt(dot(side,side)));
        const auto up=cross(axis,side);
        constexpr int sides=6;
        for(int i=0;i<sides;++i){const double x=i*2*std::numbers::pi/sides,y=(i+1)*2*std::numbers::pi/sides;
            const auto p=side*std::cos(x)+up*std::sin(x),q=side*std::cos(y)+up*std::sin(y);
            quad({a+p,a+q,b+q,b+p},tint);
        }
    }
    bool draw(ImDrawList* draw,SDL_Renderer* renderer,const Host& host,ImVec2 size){
        faces.clear();
        // Bound CPU work in this small demo. UI stays at native viewport/DPI;
        // only the 3D image is scaled above 1080p, using the same depth buffer.
        const float render_scale=std::min({1.f,1920.f/std::max(size.x,1.f),1080.f/std::max(size.y,1.f)});
        const int width=std::max(1,int(size.x*render_scale)),height=std::max(1,int(size.y*render_scale));
        Projector camera(host,{float(width),float(height)});
        if(texture&&(width!=raster.width()||height!=raster.height()))release();
        if(!texture){
            texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,width,height);
            if(!texture)return false;
            SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR);
        }
        raster.begin(width,height,float(camera.focal),color(17,29,45),color(63,88,101));
        for(int z=-14;z<46;z+=2)for(int x=-24;x<26;x+=2){
            const bool stripe=x==0;const int checker=((x+z)/2)&1;
            quad({V3{double(x),-.02,double(z)},V3{double(x+2),-.02,double(z)},V3{double(x+2),-.02,double(z+2)},V3{double(x),-.02,double(z+2)}},
                stripe?color(40,76,81):checker?color(42,54,67):color(46,59,71));
        }
        box({0,0,43},{50,7,1},color(62,78,91));
        box({-17,0,17},{1,4,50},color(66,82,92));box({17,0,17},{1,4,50},color(66,82,92));
        for(int z=5;z<41;z+=9)for(double x:{-13.,13.}){
            box({x,0,double(z)},{1.2,5.5,1.2},color(92,110,118));
            box({x,4.7,double(z)-.63},{1.22,.28,.1},color(49,217,191));
        }
        for(auto p:crates){
            box(p,{2.2,1.15,1.6},color(113,89,59),.15);
            box(p+V3{0,1.15,0},{1.95,.12,1.45},color(193,139,63),.15);
        }
        for(const auto& target:host.targets){
            // Fixed vertical disk at Z; the support stays physically behind it.
            box({target.position.x,0,target.position.z+.20},{.14,1.5,.14},color(142,160,169));
            box({target.position.x,0,target.position.z},{1.6,.09,.75},color(24,35,48));
        }
        // Procedural third-person character: no game assets or engine dependency.
        // Hide the local body/weapon in the scope or when camera collision
        // brings it too close; it must not fill the image or cover the reticle.
        if(host.show_character()){
        const double angle=host.yaw*rad;
        auto offset=[&](double x,double y,double z){return host.player+V3{x*std::cos(angle)+z*std::sin(angle),y,-x*std::sin(angle)+z*std::cos(angle)};};
        auto body=[&](double x,double y,double z,V3 dims,ImU32 tint){box(offset(x,y,z),dims,tint,angle);};
        const double stride=std::sin(host.walk)*.13;
        body(-.18,.04,stride,{.23,.73,.28},color(25,34,48));body(.18,.04,-stride,{.23,.73,.28},color(25,34,48));
        body(0,.75,0,{.65,.75,.38},color(43,126,139));body(0,1.18,-.25,{.5,.36,.15},color(18,44,61));
        body(0,1.53,0,{.40,.40,.38},color(156,175,173));body(0,1.65,.21,{.36,.12,.05},color(36,239,199));
        const double reload=host.reload_pose(),progress=host.reload_progress();
        const ImU32 accents[]={color(75,178,167),color(202,151,83),color(111,152,204),color(187,104,72)};
        const auto accent=accents[host.equipped];const bool pistol=host.equipped==Pistol;
        const double pump=host.pump_pose();
        if(pistol){
            const double slide=host.last_fired_weapon==Pistol?.055*host.cooldown/host.weapon().shot_interval:0;
            weapon_box(host,{0,.005,.29-slide},{.11,.10,.34},accent);
            weapon_box(host,{0,-.21,.17},{.10,.22,.12},color(39,49,60));
            weapon_box(host,{0,.02,.44},{.055,.05,.12},color(30,38,43));
        }else{
            weapon_box(host,{0,-.05,.27},{.19,.15,.53},accent);
            weapon_box(host,{0,-.11,-.04},{.16,.18,.22},color(36,43,49));
            const double length=host.weapon().muzzle_z-.46;
            weapon_box(host,{0,.015,.46+length/2},{.07,.07,length},color(41,49,56));
            if(host.equipped==Sniper){weapon_box(host,{0,.13,.29},{.09,.08,.32},color(29,40,51));
                weapon_box(host,{0,.14,.45},{.10,.06,.025},color(80,174,190));}
            if(host.equipped==Shotgun){
                weapon_box(host,{0,-.10,.71},{.08,.07,.43},color(40,48,54));
                weapon_box(host,{0,-.13,.65-.22*pump},{.17,.12,.25},accent);
                for(int i=0;i<4;++i)weapon_box(host,{0,-.14,.57+i*.05-.22*pump},{.18,.13,.015},color(70,58,49));
            }
        }
        const double magazine_drop=host.reloading()?std::sin(std::numbers::pi*std::clamp((progress-.16)/.66,0.0,1.0))*.37:0;
        weapon_box(host,{-.05*reload,-.27-magazine_drop,pistol?.17:.27},{pistol?.075:.12,.22,pistol?.09:.13},color(50,61,69));
        const auto right_elbow=offset(.55+.12*reload,1.13-.17*reload,.08);
        const auto left_elbow=offset(-.35,1.01-.15*reload,.29);
        const V3 left_grip=host.reloading()?V3{-.12,-.15-magazine_drop,pistol?.17:.28}:
            pistol?V3{-.085,-.12,.17}:host.equipped==Shotgun?V3{-.12,-.11,.65-.22*pump}:V3{-.12,-.02,.48};
        limb(offset(.35,1.39,0),right_elbow,.105,color(53,80,95));
        limb(right_elbow,host.weapon_point({.04,-.08,.10}),.09,color(75,106,117));
        limb(offset(-.35,1.39,0),left_elbow,.105,color(53,80,95));
        limb(left_elbow,host.weapon_point(left_grip),.09,color(75,106,117));
        weapon_box(host,left_grip,{.13,.12,.15},color(110,143,147));
        }
        if(!host.scoped()&&host.shot_flash>0){
            const auto muzzle=host.shot_start;const double radius=host.equipped==Pistol?.065:host.equipped==Shotgun?.16:host.equipped==Sniper?.13:.095;
            quad({muzzle-camera.right*radius,muzzle+V3{0,radius*1.5,0},muzzle+camera.right*radius,muzzle-V3{0,radius,0}},color(255,219,146));
            // Short tracer lives in the same depth buffer as targets and cover.
            const auto side=camera.right*.008;
            for(int i=0;i<host.impact_count;++i){const auto end=host.shot_impacts[i].position;
                quad({host.shot_start-side,host.shot_start+side,end+side,end-side},color(232,204,150));}
        }
        // Targets are world geometry too: they must never overlay an occluder.
        for(const auto& target:host.targets){
            constexpr int segments=64;const double radii[]={0,Target::radius/3,Target::radius*2/3,Target::radius};
            auto point=[&](double radius,double angle,double depth=0){return target.position+V3{radius*std::cos(angle),radius*std::sin(angle),depth};};
            const ImVec4 colors[]={{.85f,.64f,.32f,1},{.65f,.40f,.24f,1},{.40f,.48f,.52f,1}};
            for(int band=0;band<3;++band)for(int segment=0;segment<segments;++segment){
                const double a=segment*2*std::numbers::pi/segments,b=(segment+1)*2*std::numbers::pi/segments;
                auto lit=colors[band];const float flash=float(std::min(1.0,target.flash[band]/.25));
                lit.x+=(1-lit.x)*flash;lit.y+=(.94f-lit.y)*flash;lit.z+=(.68f-lit.z)*flash;
                const ImU32 tint=ImGui::ColorConvertFloat4ToU32(lit);
                quad({point(radii[band],a),point(radii[band+1],a),point(radii[band+1],b),point(radii[band],b)},tint);
            }
            for(double radius:{Target::radius/3,Target::radius*2/3,Target::radius})for(int segment=0;segment<segments;++segment){
                const double a=segment*2*std::numbers::pi/segments,b=(segment+1)*2*std::numbers::pi/segments;
                quad({point(radius-.009,a,-.002),point(radius+.009,a,-.002),point(radius+.009,b,-.002),point(radius-.009,b,-.002)},color(24,35,41));
            }
        }
        if(host.hit_flash>0&&host.last_hit_zone>=0){
            for(int i=0;i<host.impact_count;++i){const auto& hit=host.shot_impacts[i];if(hit.target<0)continue;
                const auto p=hit.position+V3{0,0,-.015};const double r=.024+.025*(host.hit_flash/.18);
                quad({p+V3{-r,0,0},p+V3{0,r,0},p+V3{r,0,0},p+V3{0,-r,0}},color(255,249,211));}
        }
        for(const auto& face:faces){
            const auto a=camera.view(face.points[0]),b=camera.view(face.points[1]),c=camera.view(face.points[2]),d=camera.view(face.points[3]);
            raster.triangle(a,b,c,face.tint);raster.triangle(a,c,d,face.tint);
        }
        if(!SDL_UpdateTexture(texture,nullptr,raster.pixels().data(),width*int(sizeof(uint32_t))))return false;
        draw->AddImage(ImTextureID(reinterpret_cast<intptr_t>(texture)),{0,0},size);
        if(host.scoped())scope_overlay(draw,size);
        if(!host.inventory&&!host.paused){
            const float scale=std::clamp(std::min(size.x/1440.f,size.y/900.f),.65f,2.6f);
            ImVec2 center(size.x/2,size.y/2);auto tint=host.aiming?color(255,193,100):color(235,244,242);
            if(!host.scoped()){
            float gap=((host.aiming?5.f:11.f)+float(host.kick*7))*scale,length=(host.aiming?9.f:13.f)*scale;
            if(host.equipped==Shotgun){
                const float spread=float(size.y*.5/std::tan(host.fov()*rad/2)*std::tan(host.weapon().spread_degrees*rad));
                gap=std::max(gap,spread+3*scale);
                draw->AddCircle(center,spread,color(235,244,242,100),64,scale);
            }
            for(int sign:{-1,1}){draw->AddLine({center.x+sign*gap,center.y},{center.x+sign*(gap+length),center.y},tint,2*scale);
                draw->AddLine({center.x,center.y+sign*gap},{center.x,center.y+sign*(gap+length)},tint,2*scale);}
            draw->AddCircleFilled(center,2*scale,tint);
            }
            if(host.hit_flash>0){const auto hit=host.last_hit_zone==0?color(255,210,106):color(241,248,238);
                for(int x:{-1,1})for(int y:{-1,1})draw->AddLine({center.x+x*10.f*scale,center.y+y*10.f*scale},{center.x+x*16.f*scale,center.y+y*16.f*scale},hit,2*scale);}
        }
        return true;
    }
};
}
