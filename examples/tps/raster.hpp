#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tps {
struct RenderVertex {float x,y,z;}; // camera coordinates, positive Z forward
// Small CPU rasterizer for the standalone demo, independent of GyroLib/SDL/ImGui.
// Inverse camera depth is affine in screen space, so intersecting triangles are
// resolved per pixel. Clipping precedes projection, including at the near plane.
class Raster {
    struct Screen {float x,y,q;};
    int width_{},height_{};
    float focal_{};
    std::vector<uint32_t> pixels_;
    std::vector<float> inverse_depth_;
    static float edge(Screen a,Screen b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
    Screen project(RenderVertex p)const{return {width_*.5f+p.x*focal_/p.z,height_*.5f-p.y*focal_/p.z,1/p.z};}
    void screen_triangle(Screen a,Screen b,Screen c,uint32_t tint){
        float area=edge(a,b,c.x,c.y);if(std::abs(area)<1e-6f)return;
        if(area<0){std::swap(b,c);area=-area;}
        const float left=std::min({a.x,b.x,c.x}),right=std::max({a.x,b.x,c.x});
        const float top=std::min({a.y,b.y,c.y}),bottom=std::max({a.y,b.y,c.y});
        if(right<0||bottom<0||left>=width_||top>=height_)return;
        const int x0=int(std::max(0.f,std::floor(left))),x1=int(std::min(float(width_-1),std::ceil(right)));
        const int y0=int(std::max(0.f,std::floor(top))),y1=int(std::min(float(height_-1),std::ceil(bottom)));
        const float dx0=b.y-c.y,dx1=c.y-a.y,dx2=a.y-b.y;
        const float dy0=c.x-b.x,dy1=a.x-c.x,dy2=b.x-a.x;
        float row0=edge(b,c,x0+.5f,y0+.5f),row1=edge(c,a,x0+.5f,y0+.5f),row2=edge(a,b,x0+.5f,y0+.5f);
        const float qa=a.q/area,qb=b.q/area,qc=c.q/area;
        const float dqdx=dx0*qa+dx1*qb+dx2*qc,dqdy=dy0*qa+dy1*qb+dy2*qc;
        float rowq=row0*qa+row1*qb+row2*qc;
        for(int y=y0;y<=y1;++y){
            float w0=row0,w1=row1,w2=row2,q=rowq;
            auto pixel=size_t(y)*width_+x0;
            for(int x=x0;x<=x1;++x,++pixel){
                if(w0>=0&&w1>=0&&w2>=0&&q>inverse_depth_[pixel]){inverse_depth_[pixel]=q;pixels_[pixel]=tint;}
                w0+=dx0;w1+=dx1;w2+=dx2;q+=dqdx;
            }
            row0+=dy0;row1+=dy1;row2+=dy2;rowq+=dqdy;
        }
    }
public:
    static constexpr float near_plane=.15f;
    void begin(int width,int height,float focal,uint32_t top,uint32_t bottom){
        width_=std::max(1,width);height_=std::max(1,height);focal_=focal;
        const auto count=size_t(width_)*height_;pixels_.resize(count);inverse_depth_.assign(count,0);
        for(int y=0;y<height_;++y){
            const float t=float(y)/std::max(1,height_-1);uint32_t tint=0;
            for(unsigned shift=0;shift<32;shift+=8){const auto a=(top>>shift)&255,b=(bottom>>shift)&255;
                tint|=uint32_t(float(a)+(float(b)-float(a))*t)<<shift;}
            std::fill_n(pixels_.data()+size_t(y)*width_,width_,tint);
        }
    }
    void triangle(RenderVertex a,RenderVertex b,RenderVertex c,uint32_t tint){
        const std::array<RenderVertex,3> input={a,b,c};std::array<RenderVertex,4> clipped{};unsigned count=0;
        for(const auto& v:input)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return;
        auto previous=input.back();bool previous_inside=previous.z>=near_plane;
        for(auto current:input){
            const bool inside=current.z>=near_plane;
            if(inside!=previous_inside){
                const float t=(near_plane-previous.z)/(current.z-previous.z);
                clipped[count++]={previous.x+(current.x-previous.x)*t,previous.y+(current.y-previous.y)*t,near_plane};
            }
            if(inside)clipped[count++]=current;
            previous=current;previous_inside=inside;
        }
        for(unsigned i=1;i+1<count;++i)screen_triangle(project(clipped[0]),project(clipped[i]),project(clipped[i+1]),tint);
    }
    int width()const{return width_;}int height()const{return height_;}
    const std::vector<uint32_t>& pixels()const{return pixels_;}
    const std::vector<float>& inverse_depth()const{return inverse_depth_;}
};
}
