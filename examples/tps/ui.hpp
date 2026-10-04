#pragma once
#include "scene.hpp"
#include <cstdio>
#include <string>

namespace tps {
inline void text(ImDrawList* draw,ImVec2 p,float size,ImU32 tint,const char* value,float wrap=0){
    draw->AddText(ImGui::GetFont(),size,p,tint,value,nullptr,wrap);
}
inline void rect(ImDrawList* draw,ImVec2 p,ImVec2 size,ImU32 tint,float rounding=8){
    draw->AddRectFilled(p,{p.x+size.x,p.y+size.y},tint,rounding);
}
inline void weapon_icon(ImDrawList* draw,ImVec2 p,float s,int weapon,ImU32 tint){
    if(weapon==Pistol){
        rect(draw,{p.x+30*s,p.y+10*s},{72*s,17*s},tint,3*s);
        rect(draw,{p.x+36*s,p.y+24*s},{18*s,33*s},color(74,91,105),3*s);
        rect(draw,{p.x+54*s,p.y+28*s},{23*s,15*s},color(74,91,105),3*s);
        rect(draw,{p.x+59*s,p.y+28*s},{13*s,9*s},color(22,36,50),2*s);
        return;
    }
    rect(draw,{p.x,p.y+18*s},{27*s,20*s},color(93,115,124),3*s);
    rect(draw,{p.x+23*s,p.y+14*s},{63*s,19*s},tint,3*s);
    rect(draw,{p.x+76*s,p.y+19*s},{(weapon==Rifle?38.f:74.f)*s,7*s},color(151,170,176),2*s);
    rect(draw,{p.x+39*s,p.y+32*s},{10*s,23*s},color(93,115,124),2*s);
    rect(draw,{p.x+60*s,p.y+31*s},{(weapon==Shotgun?18.f:12.f)*s,22*s},color(67,85,98),2*s);
    if(weapon==Shotgun){
        rect(draw,{p.x+84*s,p.y+27*s},{40*s,11*s},tint,2*s);
        for(int i=0;i<4;++i)rect(draw,{p.x+(90+i*8)*s,p.y+28*s},{2*s,8*s},color(64,64,66),1);
    }
    if(weapon==Sniper){rect(draw,{p.x+47*s,p.y+5*s},{6*s,10*s},color(110,134,141),1);
        rect(draw,{p.x+32*s,p.y},{45*s,11*s},color(125,154,165),3*s);
        rect(draw,{p.x+74*s,p.y-1*s},{7*s,13*s},color(90,198,210),2*s);}
}
inline void inventory_ui(ImDrawList* draw,Host& host,ImVec2 view,float s,bool confirm,const char* confirm_button){
    const ImVec2 origin(48*s,191*s),size(view.x-96*s,view.y-276*s);
    rect(draw,origin,size,color(12,22,34,249),12*s);
    text(draw,{origin.x+24*s,origin.y+22*s},25*s,color(241,247,249),"INVENTORY");
    const float gap=20*s,cell_w=(size.x-48*s-gap)/2,cell_h=(size.y-96*s-gap)/2;
    const ImVec2 mouse(float(host.cursor_x*view.x),float(host.cursor_y*view.y));host.hovered=-1;
    const ImU32 accents[]={color(87,213,192),color(230,175,95),color(137,175,240),color(215,133,101)};
    for(int i=0;i<int(weapons.size());++i){
        const ImVec2 p(origin.x+24*s+(i%2)*(cell_w+gap),origin.y+72*s+(i/2)*(cell_h+gap));
        const bool hovered=mouse.x>=p.x&&mouse.x<p.x+cell_w&&mouse.y>=p.y&&mouse.y<p.y+cell_h;
        if(hovered)host.hovered=i;
        const ImU32 tint=accents[i];
        rect(draw,p,{cell_w,cell_h},hovered?color(30,55,68):color(22,36,50),8*s);
        draw->AddRect(p,{p.x+cell_w,p.y+cell_h},hovered?tint:color(48,67,82),8*s,hovered?2*s:1.f);
        text(draw,{p.x+22*s,p.y+20*s},24*s,color(240,247,250),weapons[i].name);
        if(host.equipped==i)text(draw,{p.x+cell_w-110*s,p.y+26*s},13*s,tint,"EQUIPPED");
        const float icon_scale=std::min({cell_w/(190*s),(cell_h/s-110)/57,2.f})*s;
        weapon_icon(draw,{p.x+(cell_w-152*icon_scale)/2,p.y+62*s},icon_scale,i,tint);
        char rounds[64];std::snprintf(rounds,sizeof(rounds),"%d / %d rounds",host.magazines[i],weapons[i].capacity);
        text(draw,{p.x+22*s,p.y+cell_h-35*s},16*s,color(174,197,211),rounds);
        if(hovered&&host.equipped!=i){const auto equip="Click / "+std::string(confirm_button)+": equip";
            const float width=ImGui::GetFont()->CalcTextSizeA(14*s,10000,0,equip.c_str()).x;
            text(draw,{p.x+cell_w-22*s-width,p.y+cell_h-35*s},14*s,tint,equip.c_str());}
    }
    if(confirm&&host.hovered>=0)host.equip(host.hovered);
    draw->AddCircle(mouse,14*s,color(76,220,196,130),32,1.5f*s);
    ImVec2 arrow[]={{mouse.x,mouse.y},{mouse.x+5*s,mouse.y+22*s},{mouse.x+10*s,mouse.y+13*s},{mouse.x+20*s,mouse.y+10*s}};
    draw->AddConvexPolyFilled(arrow,4,color(245,255,253));
    draw->AddPolyline(arrow,4,color(9,22,29),1.5f*s,ImDrawFlags_Closed);
}
inline void ui(ImDrawList* draw,Host& host,gl_context* c,ImVec2 view,bool synthetic,const char* inventory_button,
               const char* aim_button,const char* fire_button,const char* reload_button,const char* confirm_button,bool confirm){
    const float s=std::clamp(std::min(view.x/1440.f,view.y/900.f),.65f,2.6f);
    rect(draw,{0,0},{view.x,73*s},color(10,20,31,238),0);
    text(draw,{32*s,20*s},30*s,color(243,249,249),"GyroLib Demo");
    const char* source=synthetic?"Simulated gyro":host.output.source==GL_SOURCE_SDL?"SDL gyro":host.output.source==GL_SOURCE_STEAM?"Steam gyro":"Waiting for motion";
    text(draw,{view.x-418*s,29*s},15*s,color(130,188,184),source);
    if(const auto key=gl_get_menu_key(c)){
        char shortcut[32];std::snprintf(shortcut,sizeof(shortcut),"F%u  Settings",key);
        text(draw,{view.x-188*s,27*s},17*s,color(220,231,236),shortcut);
    }
    const uint32_t modes[]={Explore,AimStandard,AimSniper,Inventory};
    const char* labels[]={"EXPLORATION","AIM STANDARD","AIM SNIPER","INVENTORY"};
    for(int i=0;i<4;++i){
        const bool in_scope=host.scoped()&&!host.inventory;
        if(in_scope&&i!=2)continue;
        const ImVec2 p((32+(in_scope?0:i)*219)*s,92*s);const bool active=host.mode()==modes[i]&&!host.paused;
        const ImU32 accent=i==1?color(91,218,194):i==2?color(236,178,92):i==3?color(139,175,248):color(122,196,210);
        rect(draw,p,{207*s,72*s},color(13,26,38,231),7*s);
        if(active)rect(draw,p,{3*s,72*s},accent,2*s);
        text(draw,{p.x+15*s,p.y+12*s},14*s,active?accent:color(148,168,183),labels[i]);
        double x=0,y=0;gl_setting_get(c,("context."+std::to_string(modes[i])+".sensitivity_x").c_str(),&x);
        gl_setting_get(c,("context."+std::to_string(modes[i])+".sensitivity_y").c_str(),&y);
        char axes[64];std::snprintf(axes,sizeof(axes),"X %.2f / Y %.2f",x,y);
        text(draw,{p.x+15*s,p.y+39*s},17*s,color(229,242,246),axes);
    }
    const ImVec2 stats(view.x-351*s,92*s);rect(draw,stats,{319*s,46*s},color(13,26,38,232),7*s);
    char ammo[96];std::snprintf(ammo,sizeof(ammo),"%s   %02d / %02d",host.weapon().name,host.ammo(),host.weapon().capacity);
    text(draw,{stats.x+16*s,stats.y+12*s},19*s,color(232,241,241),ammo);
    if(host.reloading()&&!host.inventory){
        const ImVec2 p(view.x*.5f-73*s,view.y*.5f+43*s);
        text(draw,p,15*s,color(210,226,231),"Reloading");
        rect(draw,{p.x,p.y+24*s},{146*s,3*s},color(34,56,69),1);
        rect(draw,{p.x,p.y+24*s},{146*s*float(host.reload_progress()),3*s},color(92,217,192),1);
    }
    if(host.inventory)inventory_ui(draw,host,view,s,confirm&&!host.paused&&!gl_panel_open(c),confirm_button);
    if(host.scoped()&&!host.inventory){
        rect(draw,{32*s,174*s},{207*s,66*s},color(13,26,38,231),7*s);
        text(draw,{47*s,183*s},17*s,color(236,178,92),host.sniper_zoom?"ZOOM 2":"ZOOM 1");
        const auto hint="V / wheel / "+std::string(confirm_button);
        text(draw,{47*s,209*s},14*s,color(180,204,214),hint.c_str(),177*s);
    }
    if(host.paused)rect(draw,{0,0},view,color(4,11,20,180),0);
    rect(draw,{0,view.y-65*s},{view.x,65*s},color(10,20,31,239),0);
    std::string commands=host.inventory?"Gyro / mouse / right stick: cursor   |   Click / "+std::string(confirm_button)+": equip":
        "WASD / left stick: move   |   RMB / "+std::string(aim_button)+": aim   |   Click / "+fire_button+": fire";
    text(draw,{32*s,view.y-52*s},14*s,color(180,204,214),commands.c_str());
    commands="Tab / "+std::string(inventory_button)+(host.inventory?": close":": inventory   |   R / "+std::string(reload_button)+": reload");
    if(!host.inventory){double button=0;gl_setting_get(c,("context."+std::to_string(host.mode())+".camera.recenter_button").c_str(),&button);
        commands+="   |   Home";if(button>0)commands+=" / "+std::string(gl_get_button_label(c,uint32_t(button)-1));commands+=": recenter";}
    text(draw,{32*s,view.y-28*s},14*s,color(180,204,214),commands.c_str());
    if(host.notification_time>0)text(draw,{view.x*.5f,view.y-28*s},14*s,color(234,184,105),host.notification.c_str());
}
}
