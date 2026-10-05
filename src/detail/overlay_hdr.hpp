#pragma once
// HDR composition belongs to GyroLib, not to the vendored ImGui backend.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <cstdint>

namespace gyrolib_hdr {
inline bool supported(DXGI_FORMAT format, uint32_t color) {
    return (color == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 &&
            (format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_R10G10B10A2_UNORM)) ||
           (color == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 && format == DXGI_FORMAT_R16G16B16A16_FLOAT) ||
           (color == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 && format == DXGI_FORMAT_R10G10B10A2_UNORM);
}
inline uint32_t automatic_space(DXGI_FORMAT format, uint32_t output_space) {
    if(format==DXGI_FORMAT_R16G16B16A16_FLOAT)return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    if(format==DXGI_FORMAT_R10G10B10A2_UNORM && output_space==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
        return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
}
inline HRESULT resolve_space(IDXGISwapChain3* swapchain, DXGI_FORMAT format, uint32_t requested, uint32_t& resolved) {
    if(requested!=UINT32_MAX){resolved=requested;return S_OK;}
    if(format!=DXGI_FORMAT_R10G10B10A2_UNORM){resolved=automatic_space(format,0);return S_OK;}
    Microsoft::WRL::ComPtr<IDXGIOutput> output;Microsoft::WRL::ComPtr<IDXGIOutput6> advanced;
    HRESULT result=swapchain->GetContainingOutput(&output);
    if(FAILED(result)){
        // WARP and hybrid-adapter swapchains may have no directly associated
        // output. Find the physical output containing the host window instead.
        DXGI_SWAP_CHAIN_DESC window{};swapchain->GetDesc(&window);
        const auto monitor=MonitorFromWindow(window.OutputWindow,MONITOR_DEFAULTTONEAREST);
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))){
            for(UINT i=0;!output;++i){Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
                if(FAILED(factory->EnumAdapters1(i,&adapter)))break;
                for(UINT j=0;;++j){Microsoft::WRL::ComPtr<IDXGIOutput> candidate;
                    if(FAILED(adapter->EnumOutputs(j,&candidate)))break;
                    DXGI_OUTPUT_DESC d{};if(SUCCEEDED(candidate->GetDesc(&d))&&d.Monitor==monitor){output=candidate;break;}
                }
            }
        }
    }
    // Headless/remote sessions may expose no Advanced Color output. Use SDR
    // there; hosts with known HDR encodings can always supply an explicit space.
    if(!output || FAILED(output.As(&advanced))){resolved=0;return S_OK;}
    DXGI_OUTPUT_DESC1 desc{};result=advanced->GetDesc1(&desc);if(FAILED(result)){resolved=0;return S_OK;}
    resolved=automatic_space(format,desc.ColorSpace);return S_OK;
}

// ImGui's SDR target holds premultiplied sRGB after blending over transparent
// black. Unpremultiply before decoding; compose with the scene in linear light.
// Pixels with no UI are discarded, preserving the original backbuffer bits.
inline constexpr char shader[] = R"hlsl(
Texture2D<float4> panel : register(t0);
Texture2D<float4> scene : register(t1);
cbuffer Settings : register(b0) { float whiteNits; uint pq; };
float4 vs(uint id : SV_VertexID) : SV_Position {
    return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);
}
float3 srgbToLinear(float3 c) {
    return float3(c.r <= .04045 ? c.r / 12.92 : pow((c.r + .055) / 1.055, 2.4),
                  c.g <= .04045 ? c.g / 12.92 : pow((c.g + .055) / 1.055, 2.4),
                  c.b <= .04045 ? c.b / 12.92 : pow((c.b + .055) / 1.055, 2.4));
}
float3 pqToNits(float3 c) {
    float3 p = pow(max(c, 0), 1.0 / 78.84375);
    return 10000 * pow(max(p - .8359375, 0) / max(18.8515625 - 18.6875 * p, 1e-6), 1.0 / .1593017578125);
}
float3 nitsToPq(float3 c) {
    float3 p = pow(saturate(c / 10000), .1593017578125);
    return pow((.8359375 + 18.8515625 * p) / (1 + 18.6875 * p), 78.84375);
}
float4 ps(float4 position : SV_Position) : SV_Target {
    int3 at = int3(position.xy, 0);
    float4 ui = panel.Load(at);
    if (ui.a == 0) discard;
    float4 bg = scene.Load(at);
    float3 color = srgbToLinear(saturate(ui.rgb / ui.a)) * whiteNits;
    if (pq != 0) {
        // Linear Rec.709 -> Rec.2020, D65 white in both spaces.
        color = mul(float3x3(.627404, .329283, .043313,
                            .069097, .919540, .011362,
                            .016391, .088013, .895595), color);
        color = nitsToPq(color * ui.a + pqToNits(bg.rgb) * (1 - ui.a));
    } else {
        // scRGB 1.0 = 80 cd/m2. Preserve negative and >1 scene values.
        color = color / 80 * ui.a + bg.rgb * (1 - ui.a);
    }
    return float4(color, bg.a);
}
)hlsl";

struct Composer {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D12Resource> panel, scene;
    Ptr<ID3D12DescriptorHeap> rtv, srv;
    Ptr<ID3D12RootSignature> root;
    Ptr<ID3D12PipelineState> pipeline;
    uint32_t color{};
    float white_nits{203};

    static void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                           D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
        list->ResourceBarrier(1, &b);
    }
    HRESULT init(ID3D12Device* device, DXGI_FORMAT format, uint32_t space) {
        color = space;
        D3D12_DESCRIPTOR_RANGE range{}; range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 2;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = {1, &range}; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants = {0, 0, 2}; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters = 2; desc.pParameters = params;
        Ptr<ID3DBlob> signature, errors, vs, ps;
        HRESULT result = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
        if (FAILED(result)) return result;
        result = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root));
        if (FAILED(result)) return result;
        result = D3DCompile(shader, sizeof(shader)-1, "GyroLib HDR", nullptr, nullptr, "vs", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &errors);
        if (FAILED(result)) return result;
        result = D3DCompile(shader, sizeof(shader)-1, "GyroLib HDR", nullptr, nullptr, "ps", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &errors);
        if (FAILED(result)) return result;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{}; pso.pRootSignature = root.Get();
        pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()}; pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
        pso.SampleMask = UINT_MAX; pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1; pso.RTVFormats[0] = format; pso.SampleDesc.Count = 1;
        result = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline));
        if (FAILED(result)) return result;
        D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        result = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtv)); if (FAILED(result)) return result;
        heap = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        return device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srv));
    }
    void release_targets() { panel.Reset(); scene.Reset(); }
    HRESULT resize(ID3D12Device* device, const D3D12_RESOURCE_DESC& buffer) {
        release_targets();
        auto desc = buffer; desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        HRESULT result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&scene));
        if (FAILED(result)) return result;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format;
        result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&panel));
        if (FAILED(result)) { release_targets(); return result; }
        device->CreateRenderTargetView(panel.Get(), nullptr, rtv->GetCPUDescriptorHandleForHeapStart());
        auto cpu = srv->GetCPUDescriptorHandleForHeapStart();
        device->CreateShaderResourceView(panel.Get(), nullptr, cpu);
        cpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CreateShaderResourceView(scene.Get(), nullptr, cpu);
        return S_OK;
    }
    void begin(ID3D12GraphicsCommandList* list, ID3D12Resource* buffer) {
        transition(list, buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(list, scene.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(scene.Get(), buffer);
        transition(list, scene.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transition(list, buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float clear[4]{}; auto target = rtv->GetCPUDescriptorHandleForHeapStart();
        list->ClearRenderTargetView(target, clear, 0, nullptr);
        list->OMSetRenderTargets(1, &target, FALSE, nullptr);
    }
    void finish(ID3D12GraphicsCommandList* list, ID3D12Resource* buffer, D3D12_CPU_DESCRIPTOR_HANDLE target) {
        transition(list, panel.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->OMSetRenderTargets(1, &target, FALSE, nullptr);
        ID3D12DescriptorHeap* heaps[]{srv.Get()}; list->SetDescriptorHeaps(1, heaps);
        list->SetGraphicsRootSignature(root.Get()); list->SetPipelineState(pipeline.Get());
        list->SetGraphicsRootDescriptorTable(0, srv->GetGPUDescriptorHandleForHeapStart());
        struct {float white; uint32_t pq;} constants{white_nits, color == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020};
        list->SetGraphicsRoot32BitConstants(1, 2, &constants, 0);
        auto size = buffer->GetDesc();
        D3D12_VIEWPORT viewport{0, 0, float(size.Width), float(size.Height), 0, 1};
        D3D12_RECT rect{0, 0, LONG(size.Width), LONG(size.Height)};
        list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &rect);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); list->DrawInstanced(3, 1, 0, 0);
        transition(list, panel.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition(list, buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    }
};
}
