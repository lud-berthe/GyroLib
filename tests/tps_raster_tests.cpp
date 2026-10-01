#include "../examples/tps/raster.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace tps;
#define CHECK(x) do{if(!(x))throw std::runtime_error("Depth regression at line "+std::to_string(__LINE__));}while(0)
constexpr uint32_t background=0xff101010,red=0xff0000ff,blue=0xffff0000;
static RenderVertex vertex(float x,float y,float depth){return {(x-32)*depth/32,(32-y)*depth/32,depth};}
static void begin(Raster& r){r.begin(64,64,32,background,background);}
int main(){try{
    Raster r;begin(r);
    auto near=[&]{r.triangle({-1,-1,2},{1,-1,2},{0,1,2},red);};
    auto far=[&]{r.triangle({-2,-2,4},{2,-2,4},{0,2,4},blue);};
    near();far();CHECK(r.pixels()[32*64+32]==red);auto ordered=r.pixels();
    begin(r);far();near();CHECK(r.pixels()==ordered); // later geometry cannot cover nearer geometry
    auto left=[&]{r.triangle(vertex(8,8,1),vertex(56,8,5),vertex(32,56,3),red);};
    auto right=[&]{r.triangle(vertex(8,8,5),vertex(56,8,1),vertex(32,56,3),blue);};
    begin(r);left();right();CHECK(r.pixels()[16*64+20]==red);CHECK(r.pixels()[16*64+44]==blue);
    ordered=r.pixels();begin(r);right();left();CHECK(r.pixels()==ordered); // intersecting planes: no object/face sort can suffice
    begin(r);left();r.triangle(vertex(8,8,2.5f),vertex(56,8,2.5f),vertex(32,56,2.5f),blue);
    CHECK(r.pixels()[18*64+30]==red);CHECK(r.inverse_depth()[18*64+30]>.4f); // perspective-correct reciprocal depth
    begin(r);r.triangle({-.6f,-.3f,.05f},{.6f,-.3f,1},{0,.6f,1},red);
    CHECK(std::count(r.pixels().begin(),r.pixels().end(),red)>0); // clip crossing triangles instead of dropping the face
    for(float q:r.inverse_depth())CHECK(std::isfinite(q)&&q>=0&&q<=1/Raster::near_plane+.001f);
    begin(r);r.triangle({-1,-1,.1f},{1,-1,.1f},{0,1,.1f},red);
    CHECK(std::count(r.pixels().begin(),r.pixels().end(),red)==0);
    r.triangle({0,0,1},{0,0,1},{0,0,1},red);
    r.triangle({std::numeric_limits<float>::infinity(),0,1},{1,0,1},{0,1,1},red);
    CHECK(std::count(r.pixels().begin(),r.pixels().end(),red)==0);
    begin(r);r.triangle({-1,-1,2},{1,-1,2},{1,1,2},red);r.triangle({-1,-1,2},{1,1,2},{-1,1,2},red);
    for(int y=16;y<48;++y)for(int x=16;x<48;++x)CHECK(r.pixels()[y*64+x]==red); // shared edge without cracks
    r.begin(13,7,7,red,blue);CHECK(r.pixels().size()==91&&r.inverse_depth().size()==91);
    CHECK(r.pixels().front()==red&&r.pixels().back()==blue);CHECK(r.inverse_depth()[30]==0);
    std::cout<<"TPS depth: order-independent occlusion, intersecting geometry, perspective, near clipping and resize passed.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
