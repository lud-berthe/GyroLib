#include "../src/detail/mouse_hook.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
using namespace gyrolib_detail;
int main()try{
    // Model both OS paths: returning nonzero from WH_MOUSE_LL prevents legacy
    // motion/cursor updates, but the anonymous raw packet is still delivered.
    // No automated test injects input into the user's desktop.
    MouseRoute route;MouseHookGate hook;double yaw=0,pitch=0;
    int cursor_x=0,legacy_moves=0;
    auto update=[&](uint64_t now,uint32_t mode,bool allowed=true,uint32_t view=1,uint64_t device=7,bool panel=false,bool focus=true){
        route.consume(now,allowed,view,device,false,mode,focus,true,yaw,pitch,!panel);
        hook.publish(route,now);
    };
    auto movement=[&](uint64_t now,int x,bool physical=false,bool foreground=true){
        const bool stopped=hook.movement(now,!physical,foreground);
        if(!stopped){cursor_x+=x;++legacy_moves;}
        hook.corroborate(route,now);
        const bool consumed=route.raw(now,physical?1234:0,false,0,x,0);
        hook.publish(route,now);
        return std::pair{stopped,consumed};
    };
    update(100,2);
    for(int i=0;i<3;++i)CHECK(!movement(101+i,20).first); // initial corroboration
    CHECK(route.detected);const auto initial_cursor=cursor_x,initial_messages=legacy_moves;
    for(int i=0;i<8;++i){const auto result=movement(104+i,20);CHECK(result.first&&result.second);}
    CHECK(cursor_x==initial_cursor&&legacy_moves==initial_messages);
    update(115,2);CHECK(std::abs(yaw-9)<1e-9); // raw counts once, no loss after cursor suppression
    update(116,2);CHECK(std::abs(yaw-9)<1e-9); // no second conversion from hook evidence
    CHECK(!movement(117,10,true).first);CHECK(cursor_x==initial_cursor+10);
    CHECK(!route.raw(118,0,true,0,20,0));CHECK(!route.raw(118,0,false,1,20,0));
    update(120,1);CHECK(movement(121,20).first);update(122,1);CHECK(yaw==9);
    // Menu/camera veto blocks both input paths, but never creates delayed output.
    update(130,2,false,2);CHECK(movement(131,20).first);update(140,2,true,2);CHECK(yaw==9);
    update(150,0);CHECK(!movement(151,20).first);update(152,0);CHECK(yaw==9);
    update(160,2,true,1,7,true);CHECK(!movement(161,20).first);update(162,2);CHECK(yaw==9);
    CHECK(!hook.movement(163,true,false)); // foreground checked for every event
    CHECK(!hook.movement(163,false,true)); // hardware is never intercepted
    CHECK(!hook.movement(263,true,true)); // stopped host cannot trap the mouse
    update(280,2,true,0);CHECK(!movement(281,20).first); // unknown view
    update(290,2,true,1,7,false,false);CHECK(!movement(291,20).first);
    update(300,2,true,1,8);CHECK(!route.detected);CHECK(!movement(301,20).first);
    hook.clear();CHECK(!hook.movement(302,true,true));CHECK(hook.injected_at==0);

    // Evidence is bounded; old injection cannot identify a new raw-only stream.
    MouseRoute stale;MouseHookGate stale_hook;double y=0,p=0;
    stale.consume(100,true,1,1,false,1,true,true,y,p);stale_hook.publish(stale,100);
    CHECK(!stale_hook.movement(101,true,true));
    stale.consume(300,true,1,1,false,1,true,true,y,p);stale_hook.publish(stale,300);
    for(int i=0;i<3;++i){stale_hook.corroborate(stale,301+i);CHECK(!stale.raw(301+i,0,false,0,20,0));}
    CHECK(!stale.detected);
    // Contact-only fallback expires even if the last published update is newer.
    stale.detected=true;stale.last_packet=300;stale.consume(440,true,1,1,false,1,true,false,y,p);
    stale_hook.publish(stale,440);CHECK(stale_hook.movement(450,true,true));CHECK(!stale_hook.movement(451,true,true));
#ifdef _WIN32
    // Exercise actual hook ownership/message-loop teardown with no attached
    // window: it cannot intercept anything on the real desktop.
    MouseHook first,second;
    for(int i=0;i<3;++i){CHECK(first.start());CHECK(first.start());CHECK(second.start());first.stop();second.stop();}
#endif
    std::cout<<"Steam mouse hook: cursor suppression, raw conversion once, menu veto, physical input, panel/focus/stale release and lifecycle passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
