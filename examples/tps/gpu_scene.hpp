#pragma once
#include "raster.hpp"
#include <SDL3/SDL.h>
#include <cstddef>
#include <cstring>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif
namespace tps {
struct SceneVertex {RenderVertex position;uint32_t color;};
// Demo renderer only. GyroLib's DLL never creates these D3D11 resources.
class GPUScene {
#ifdef _WIN32
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext> commands,immediate;
    Ptr<ID3D11VertexShader> vertex,background;
    Ptr<ID3D11PixelShader> pixel;
    Ptr<ID3D11InputLayout> layout;
    Ptr<ID3D11Buffer> vertices,constants;
    Ptr<ID3D11RasterizerState> raster;
    Ptr<ID3D11DepthStencilState> depth_state,no_depth;
    Ptr<ID3D11Texture2D> depth;
    Ptr<ID3D11DepthStencilView> depth_view;
    Ptr<ID3D11RenderTargetView> target;
    size_t capacity{};
    struct Timing {Ptr<ID3D11Query> disjoint,start,end;bool pending{};};
    std::array<Timing,8> timings;unsigned timing_index{};uint64_t gpu_ns{},gpu_sample{};
    struct Uniform {float projection[4],top[4],bottom[4];};
    static constexpr const char* shader=R"(
cbuffer Scene : register(b0) { float4 projection; float4 top; float4 bottom; };
struct Output { float4 position : SV_Position; float4 color : COLOR0; };
Output geometry(float3 position : POSITION, float4 color : COLOR0) {
    Output o; o.position=float4(position.x*projection.x,position.y*projection.y,position.z-projection.z,position.z);
    o.color=color; return o;
}
Output sky(uint id : SV_VertexID) {
    float2 p=float2((id<<1)&2,id&2); Output o;
    o.position=float4(p.x*2-1,1-p.y*2,0,1);o.color=lerp(top,bottom,p.y);return o;
}
float4 shade(Output input) : SV_Target { return input.color; }
)";
    bool compile(const char* entry,const char* profile,Ptr<ID3DBlob>& code){Ptr<ID3DBlob> error;
        const auto result=D3DCompile(shader,std::strlen(shader),"GyroLib demo scene",nullptr,nullptr,entry,profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(FAILED(result))SDL_Log("Demo shader %s: %s",entry,error?static_cast<const char*>(error->GetBufferPointer()):"compile failed");
        return SUCCEEDED(result);}
    static void rgba(uint32_t tint,float* out){for(unsigned i=0;i<4;++i)out[i]=float((tint>>(i*8))&255)/255.f;}
#endif
public:
    uint64_t measured_ns()const{
#ifdef _WIN32
        return gpu_ns;
#else
        return 0;
#endif
    }
    uint64_t measured_sample()const{
#ifdef _WIN32
        return gpu_sample;
#else
        return 0;
#endif
    }
    bool supported(SDL_Renderer* renderer)const{
#ifdef _WIN32
        return SDL_GetPointerProperty(SDL_GetRendererProperties(renderer),SDL_PROP_RENDERER_D3D11_DEVICE_POINTER,nullptr)!=nullptr;
#else
        return false;
#endif
    }
    void release(){
#ifdef _WIN32
        for(auto& t:timings)t={};gpu_ns=gpu_sample=0;timing_index=0;
        target.Reset();depth_view.Reset();depth.Reset();vertices.Reset();constants.Reset();layout.Reset();
        vertex.Reset();background.Reset();pixel.Reset();raster.Reset();depth_state.Reset();no_depth.Reset();
        commands.Reset();immediate.Reset();device.Reset();capacity=0;
#endif
    }
    bool initialize(SDL_Renderer* renderer,SDL_Texture* texture,int width,int height){
#ifdef _WIN32
        release();device=static_cast<ID3D11Device*>(SDL_GetPointerProperty(SDL_GetRendererProperties(renderer),SDL_PROP_RENDERER_D3D11_DEVICE_POINTER,nullptr));
        auto* surface=static_cast<ID3D11Texture2D*>(SDL_GetPointerProperty(SDL_GetTextureProperties(texture),SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER,nullptr));
        if(!device||!surface)return false;
        device->GetImmediateContext(&immediate);
        if(FAILED(device->CreateDeferredContext(0,&commands))||FAILED(device->CreateRenderTargetView(surface,nullptr,&target)))return false;
        D3D11_TEXTURE2D_DESC ds{};ds.Width=width;ds.Height=height;ds.MipLevels=ds.ArraySize=1;
        ds.Format=DXGI_FORMAT_D32_FLOAT;ds.SampleDesc.Count=1;ds.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        if(FAILED(device->CreateTexture2D(&ds,nullptr,&depth))||FAILED(device->CreateDepthStencilView(depth.Get(),nullptr,&depth_view)))return false;
        Ptr<ID3DBlob> code;
        if(!compile("geometry","vs_4_0",code)||FAILED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex)))return false;
        const D3D11_INPUT_ELEMENT_DESC attributes[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
        if(FAILED(device->CreateInputLayout(attributes,2,code->GetBufferPointer(),code->GetBufferSize(),&layout)))return false;
        code.Reset();if(!compile("sky","vs_4_0",code)||FAILED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&background)))return false;
        code.Reset();if(!compile("shade","ps_4_0",code)||FAILED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&pixel)))return false;
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Uniform);cb.Usage=D3D11_USAGE_DYNAMIC;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(device->CreateBuffer(&cb,nullptr,&constants)))return false;
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
        if(FAILED(device->CreateRasterizerState(&rs,&raster)))return false;
        D3D11_DEPTH_STENCIL_DESC zs{};zs.DepthEnable=TRUE;zs.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;zs.DepthFunc=D3D11_COMPARISON_LESS;
        if(FAILED(device->CreateDepthStencilState(&zs,&depth_state)))return false;
        zs.DepthEnable=FALSE;if(FAILED(device->CreateDepthStencilState(&zs,&no_depth)))return false;
        for(auto& t:timings){D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
            if(FAILED(device->CreateQuery(&q,&t.disjoint)))break;q.Query=D3D11_QUERY_TIMESTAMP;
            if(FAILED(device->CreateQuery(&q,&t.start))||FAILED(device->CreateQuery(&q,&t.end))){t={};break;}}
        return true;
#else
        return false;
#endif
    }
    bool draw(SDL_Renderer* renderer,int width,int height,float focal,const std::vector<SceneVertex>& mesh,uint32_t top,uint32_t bottom){
#ifdef _WIN32
        if(!commands||!SDL_FlushRenderer(renderer))return false;
        if(mesh.size()>capacity){capacity=std::max(mesh.size(),capacity*2);vertices.Reset();
            D3D11_BUFFER_DESC b{};b.ByteWidth=UINT(capacity*sizeof(SceneVertex));b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            if(FAILED(device->CreateBuffer(&b,nullptr,&vertices)))return false;}
        Uniform u{{2*focal/width,2*focal/height,Raster::near_plane,0},{},{}};rgba(top,u.top);rgba(bottom,u.bottom);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(commands->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
        std::memcpy(mapped.pData,&u,sizeof(u));commands->Unmap(constants.Get(),0);
        if(!mesh.empty()){if(FAILED(commands->Map(vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
            std::memcpy(mapped.pData,mesh.data(),mesh.size()*sizeof(SceneVertex));commands->Unmap(vertices.Get(),0);}
        auto* rtv=target.Get();commands->OMSetRenderTargets(1,&rtv,depth_view.Get());
        commands->ClearDepthStencilView(depth_view.Get(),D3D11_CLEAR_DEPTH,1,0);
        commands->OMSetBlendState(nullptr,nullptr,~0u);commands->RSSetState(raster.Get());
        D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1};commands->RSSetViewports(1,&vp);
        auto* cb=constants.Get();commands->VSSetConstantBuffers(0,1,&cb);commands->PSSetShader(pixel.Get(),nullptr,0);
        commands->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);commands->IASetInputLayout(nullptr);
        commands->OMSetDepthStencilState(no_depth.Get(),0);commands->VSSetShader(background.Get(),nullptr,0);commands->Draw(3,0);
        if(!mesh.empty()){auto* vb=vertices.Get();const UINT stride=sizeof(SceneVertex),offset=0;
            commands->IASetVertexBuffers(0,1,&vb,&stride,&offset);commands->IASetInputLayout(layout.Get());
            commands->OMSetDepthStencilState(depth_state.Get(),0);commands->VSSetShader(vertex.Get(),nullptr,0);commands->Draw(UINT(mesh.size()),0);}
        Ptr<ID3D11CommandList> list;if(FAILED(commands->FinishCommandList(FALSE,&list)))return false;
        // Preserve SDL's cached immediate-context state, including ImGui bindings.
        // Nonblocking GPU timestamps: never wait for a query or force a flush.
        for(auto& t:timings)if(t.pending){D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};UINT64 start{},end{};
            if(immediate->GetData(t.disjoint.Get(),&frequency,sizeof(frequency),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&
               immediate->GetData(t.start.Get(),&start,sizeof(start),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&
               immediate->GetData(t.end.Get(),&end,sizeof(end),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK){
                t.pending=false;if(!frequency.Disjoint&&frequency.Frequency&&end>=start){gpu_ns=uint64_t(double(end-start)*1e9/double(frequency.Frequency));++gpu_sample;}}}
        auto& timing=timings[timing_index++%timings.size()];const bool timed=!timing.pending&&timing.start&&timing.end&&timing.disjoint;
        if(timed){immediate->Begin(timing.disjoint.Get());immediate->End(timing.start.Get());}
        immediate->ExecuteCommandList(list.Get(),TRUE);
        if(timed){immediate->End(timing.end.Get());immediate->End(timing.disjoint.Get());timing.pending=true;}
        return SUCCEEDED(device->GetDeviceRemovedReason());
#else
        return false;
#endif
    }
};
}
