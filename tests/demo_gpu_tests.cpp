#include "../examples/tps/gpu_scene.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
using namespace tps;
static constexpr uint32_t red=0xff0000ff,blue=0xffff0000;
int main()try{
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D_THREADSAFE,"1");CHECK(SDL_Init(SDL_INIT_VIDEO));
    auto* window=SDL_CreateWindow("GPU depth test",64,64,SDL_WINDOW_HIDDEN);CHECK(window);
    auto* renderer=SDL_CreateRenderer(window,"direct3d11");if(!renderer){SDL_DestroyWindow(window);SDL_Quit();return 77;}
    auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,64,64);CHECK(texture);
    GPUScene gpu;CHECK(gpu.initialize(renderer,texture,64,64));
    std::vector<SceneVertex> mesh;
    auto triangle=[&](float z,uint32_t tint){mesh.insert(mesh.end(),{{{-z*.5f,-z*.5f,z},tint},{{z*.5f,-z*.5f,z},tint},{{0,z*.5f,z},tint}});};
    auto render=[&]{CHECK(gpu.draw(renderer,64,64,32,mesh,0xff101010,0xff101010));
        CHECK(SDL_SetRenderDrawColor(renderer,0,0,0,255));CHECK(SDL_RenderClear(renderer));CHECK(SDL_RenderTexture(renderer,texture,nullptr,nullptr));
        auto* raw=SDL_RenderReadPixels(renderer,nullptr);CHECK(raw);auto* rgba=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);CHECK(rgba);
        std::vector<uint32_t> result(4096);for(int y=0;y<64;++y)std::memcpy(result.data()+y*64,static_cast<const char*>(rgba->pixels)+y*rgba->pitch,256);
        SDL_DestroySurface(rgba);SDL_DestroySurface(raw);return result;};
    triangle(2,red);triangle(4,blue);auto first=render();CHECK(first[32*64+32]==red);
    mesh.clear();triangle(4,blue);triangle(2,red);CHECK(render()==first);
    // Intersecting triangles require per-pixel depth, not object sorting.
    auto v=[](float x,float y,float z){return RenderVertex{(x-32)*z/32,(32-y)*z/32,z};};
    mesh={ {v(8,8,1),red},{v(56,8,5),red},{v(32,56,3),red},
           {v(8,8,5),blue},{v(56,8,1),blue},{v(32,56,3),blue} };
    first=render();CHECK(first[16*64+20]==red);CHECK(first[16*64+44]==blue);
    std::rotate(mesh.begin(),mesh.begin()+3,mesh.end());CHECK(render()==first);
    mesh={{{-.6f,-.3f,.05f},red},{{.6f,-.3f,1},red},{{0,.6f,1},red}};
    first=render();CHECK(std::count(first.begin(),first.end(),red)>0);
    mesh.clear();triangle(.1f,red);first=render();CHECK(std::count(first.begin(),first.end(),red)==0);
    // Repeated passes, SDL readback/state restoration and resource recreation.
    for(int i=0;i<3;++i){gpu.release();CHECK(gpu.initialize(renderer,texture,64,64));render();}
    gpu.release();SDL_DestroyTexture(texture);
    texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,127,93);CHECK(texture);CHECK(gpu.initialize(renderer,texture,127,93));
    CHECK(gpu.draw(renderer,127,93,40,mesh,red,blue));gpu.release();SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"GPU scene: depth ordering, intersections, clipping, SDL coexistence and resource resizing passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
