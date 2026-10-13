/* Independent, fenced GPU check of the packed formats used by FH4.
 * Writes vertex attributes through a pixel UAV; never modifies game resources. */
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#define CHECK(call) do { HRESULT hr_ = (call); if (FAILED(hr_)) { printf("FAIL line %d hr=%08lx\n", __LINE__, (unsigned long)hr_); fflush(stdout); return 1; } } while (0)
#define RELEASE(p) do { if (p) (p)->lpVtbl->Release(p); } while (0)
typedef HRESULT (WINAPI *Compiler)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *, ID3DInclude *, const char *, const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);
typedef HRESULT (WINAPI *Serializer)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **, ID3DBlob **);
typedef HRESULT (WINAPI *Creator)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
static const char *shader =
"struct Input {float4 p:POSITION; float4 ip:I_POS; float4 uv:I_UVSCALE; float4 x:I_XAXIS; float4 y:I_YAXIS; float4 c:I_COLOUR; uint v:SV_VertexID; uint i:SV_InstanceID;};\n"
"struct V {float4 p:SV_Position; nointerpolation float4 a:TEXCOORD0; nointerpolation float4 b:TEXCOORD1; nointerpolation float4 c:TEXCOORD2; nointerpolation float4 d:TEXCOORD3; nointerpolation float4 e:TEXCOORD4; nointerpolation float4 f:TEXCOORD5; nointerpolation uint index:TEXCOORD6; nointerpolation uint2 ids:TEXCOORD7;};\n"
"V vs(Input a) {V o; uint physical=(uint)round(a.ip.x*65535.0); o.index=physical*4+a.v; o.p=float4(-1.0+(o.index+0.5)*2.0/64.0,0,0.5,1); o.a=a.p; o.b=a.ip; o.c=a.uv; o.d=a.x; o.e=a.y; o.f=a.c; o.ids=uint2(a.v,a.i); return o;}\n"
"RWStructuredBuffer<float4> dst:register(u1);\n"
"float4 ps(V a):SV_Target {uint i=a.index*8; dst[i]=a.a; dst[i+1]=a.b; dst[i+2]=a.c; dst[i+3]=a.d; dst[i+4]=a.e; dst[i+5]=a.f; dst[i+6]=float4(a.ids,0,1); dst[i+7]=float4(a.index,0,0,1); return float4(0,0,0,1);}\n";

static HRESULT make_buffer(ID3D12Device *dev, UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_FLAGS flags,
                           D3D12_RESOURCE_STATES state, ID3D12Resource **out)
{
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = type;
    D3D12_RESOURCE_DESC rd = {0}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size; rd.Height = rd.DepthOrArraySize = rd.MipLevels = rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; rd.Flags = flags;
    return dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, state, NULL, &IID_ID3D12Resource, (void **)out);
}
static float snorm(int16_t x) { float v = (float)x / 32767.f; return v < -1.f ? -1.f : v; }
static int compare(float actual, float expected, const char *label, UINT vertex, UINT instance, UINT channel)
{
    if (isfinite(actual) && fabsf(actual - expected) < 0.00001f) return 0;
    printf("MISMATCH %s vertex=%u physical_instance=%u channel=%u actual=%.9g expected=%.9g\n", label, vertex, instance, channel, actual, expected);
    return 1;
}
int main(int argc, char **argv)
{
    HMODULE dll = LoadLibraryA("d3d12.dll"), compiler_dll = LoadLibraryA("d3dcompiler_47.dll");
    if (!dll || !compiler_dll) { puts("FAIL loading D3D12/compiler DLL"); return 1; }
    Creator create = (Creator)GetProcAddress(dll, "D3D12CreateDevice");
    Serializer serialize = (Serializer)GetProcAddress(dll, "D3D12SerializeRootSignature");
    Compiler compile = (Compiler)GetProcAddress(compiler_dll, "D3DCompile");
    if (!create || !serialize || !compile) return 1;
    ID3DBlob *vs = NULL, *ps = NULL, *error = NULL;
    HRESULT hr = compile(shader, strlen(shader), "packed-input-probe", NULL, NULL, "vs", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &error);
    if (error) { puts((const char *)error->lpVtbl->GetBufferPointer(error)); RELEASE(error); error = NULL; }
    CHECK(hr);
    hr = compile(shader, strlen(shader), "packed-input-probe", NULL, NULL, "ps", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &error);
    if (error) { puts((const char *)error->lpVtbl->GetBufferPointer(error)); RELEASE(error); error = NULL; }
    CHECK(hr);
    ID3D12Device *dev = NULL; CHECK(create(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev));
    D3D12_ROOT_PARAMETER param = {0}; param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    param.Descriptor.ShaderRegister = 1; param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs = {1, &param, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ID3DBlob *rs_blob = NULL; CHECK(serialize(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &error));
    ID3D12RootSignature *root = NULL; CHECK(dev->lpVtbl->CreateRootSignature(dev, 0, rs_blob->lpVtbl->GetBufferPointer(rs_blob), rs_blob->lpVtbl->GetBufferSize(rs_blob), &IID_ID3D12RootSignature, (void **)&root));
    RELEASE(rs_blob);
    ID3D12Resource *vertices = NULL, *instances = NULL, *output = NULL, *readback = NULL;
    CHECK(make_buffer(dev, 32, D3D12_HEAP_TYPE_UPLOAD, 0, D3D12_RESOURCE_STATE_GENERIC_READ, &vertices));
    CHECK(make_buffer(dev, 8*24, D3D12_HEAP_TYPE_UPLOAD, 0, D3D12_RESOURCE_STATE_GENERIC_READ, &instances));
    CHECK(make_buffer(dev, 64*8*16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &output));
    CHECK(make_buffer(dev, 64*8*16, D3D12_HEAP_TYPE_READBACK, 0, D3D12_RESOURCE_STATE_COPY_DEST, &readback));
    int16_t packed[4][4] = {{-32768,-32767,-16384,0},{1,8192,16384,32767},{-1,-4096,24576,-24576},{12345,-12345,30000,-30000}};
    D3D12_RANGE empty = {0,0}; void *mapped = NULL;
    CHECK(vertices->lpVtbl->Map(vertices, 0, &empty, &mapped)); memcpy(mapped, packed, sizeof packed); vertices->lpVtbl->Unmap(vertices,0,NULL);
    BYTE instance_data[8*24] = {0};
    for (UINT i=0;i<8;i++) {
        uint16_t ip[4] = {(uint16_t)i,(uint16_t)(10000+i*1000),(uint16_t)(20000+i*500),(uint16_t)(65535-i*1000)};
        BYTE uv[4] = {(BYTE)(10+i),(BYTE)(20+i),(BYTE)(30+i),(BYTE)(40+i)};
        uint32_t x = (i+10) | ((i+100)<<10) | ((i+200)<<20) | ((i%4)<<30);
        uint32_t y = (i+300) | ((i+400)<<10) | ((i+500)<<20) | (((i+1)%4)<<30);
        BYTE colour[4] = {(BYTE)(50+i),(BYTE)(60+i),(BYTE)(70+i),(BYTE)(80+i)};
        memcpy(instance_data+i*24,ip,8); memcpy(instance_data+i*24+8,uv,4);
        memcpy(instance_data+i*24+12,&x,4); memcpy(instance_data+i*24+16,&y,4); memcpy(instance_data+i*24+20,colour,4);
    }
    CHECK(instances->lpVtbl->Map(instances,0,&empty,&mapped)); memcpy(mapped,instance_data,sizeof instance_data); instances->lpVtbl->Unmap(instances,0,NULL);
    D3D12_RESOURCE_DESC rt = {0}; rt.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rt.Width=64; rt.Height=1; rt.DepthOrArraySize=rt.MipLevels=rt.SampleDesc.Count=1;
    rt.Format=DXGI_FORMAT_R8G8B8A8_UNORM; rt.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES default_heap={0}; default_heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ID3D12Resource *target=NULL; CHECK(dev->lpVtbl->CreateCommittedResource(dev,&default_heap,0,&rt,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void **)&target));
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,0,0};
    ID3D12DescriptorHeap *heap=NULL; CHECK(dev->lpVtbl->CreateDescriptorHeap(dev,&hd,&IID_ID3D12DescriptorHeap,(void **)&heap));
    D3D12_CPU_DESCRIPTOR_HANDLE rtv={0}; heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap,&rtv);
    dev->lpVtbl->CreateRenderTargetView(dev,target,NULL,rtv);
    D3D12_INPUT_ELEMENT_DESC input[] = {
        {"POSITION",0,DXGI_FORMAT_R16G16B16A16_SNORM,0,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"I_POS",0,DXGI_FORMAT_R16G16B16A16_UNORM,2,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"I_UVSCALE",0,DXGI_FORMAT_R8G8B8A8_UNORM,2,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"I_XAXIS",0,DXGI_FORMAT_R10G10B10A2_UNORM,2,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"I_YAXIS",0,DXGI_FORMAT_R10G10B10A2_UNORM,2,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"I_COLOUR",0,DXGI_FORMAT_R8G8B8A8_UNORM,2,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}
    };
    int explicit_offsets = argc > 1 && !strcmp(argv[1], "--explicit");
    if (explicit_offsets) {
        UINT offsets[] = {0,0,8,12,16,20};
        for (UINT i=0;i<6;i++) input[i].AlignedByteOffset=offsets[i];
    }
    printf("Input offset mode: %s\n",explicit_offsets?"explicit":"APPEND"); fflush(stdout);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd={0}; pd.pRootSignature=root;
    pd.VS.pShaderBytecode=vs->lpVtbl->GetBufferPointer(vs); pd.VS.BytecodeLength=vs->lpVtbl->GetBufferSize(vs);
    pd.PS.pShaderBytecode=ps->lpVtbl->GetBufferPointer(ps); pd.PS.BytecodeLength=ps->lpVtbl->GetBufferSize(ps);
    pd.InputLayout.pInputElementDescs=input; pd.InputLayout.NumElements=6;
    pd.SampleMask=UINT_MAX; pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; pd.RasterizerState.DepthClipEnable=TRUE;
    pd.BlendState.RenderTarget[0].SrcBlend=D3D12_BLEND_ONE; pd.BlendState.RenderTarget[0].DestBlend=D3D12_BLEND_ZERO;
    pd.BlendState.RenderTarget[0].BlendOp=D3D12_BLEND_OP_ADD; pd.BlendState.RenderTarget[0].SrcBlendAlpha=D3D12_BLEND_ONE;
    pd.BlendState.RenderTarget[0].DestBlendAlpha=D3D12_BLEND_ZERO; pd.BlendState.RenderTarget[0].BlendOpAlpha=D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].LogicOp=D3D12_LOGIC_OP_NOOP; pd.BlendState.RenderTarget[0].RenderTargetWriteMask=15;
    pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    pd.NumRenderTargets=1; pd.RTVFormats[0]=rt.Format; pd.SampleDesc.Count=1;
    ID3D12PipelineState *pso=NULL; CHECK(dev->lpVtbl->CreateGraphicsPipelineState(dev,&pd,&IID_ID3D12PipelineState,(void **)&pso));
    D3D12_COMMAND_QUEUE_DESC qd={0}; qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *queue=NULL; CHECK(dev->lpVtbl->CreateCommandQueue(dev,&qd,&IID_ID3D12CommandQueue,(void **)&queue));
    ID3D12CommandAllocator *allocator=NULL; CHECK(dev->lpVtbl->CreateCommandAllocator(dev,qd.Type,&IID_ID3D12CommandAllocator,(void **)&allocator));
    ID3D12GraphicsCommandList *list=NULL; CHECK(dev->lpVtbl->CreateCommandList(dev,0,qd.Type,allocator,pso,&IID_ID3D12GraphicsCommandList,(void **)&list));
    list->lpVtbl->SetGraphicsRootSignature(list,root);
    list->lpVtbl->SetGraphicsRootUnorderedAccessView(list,0,output->lpVtbl->GetGPUVirtualAddress(output));
    D3D12_VIEWPORT viewport={0,0,64,1,0,1}; D3D12_RECT scissor={0,0,64,1};
    list->lpVtbl->RSSetViewports(list,1,&viewport); list->lpVtbl->RSSetScissorRects(list,1,&scissor);
    list->lpVtbl->OMSetRenderTargets(list,1,&rtv,FALSE,NULL);
    list->lpVtbl->IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    D3D12_VERTEX_BUFFER_VIEW vb={vertices->lpVtbl->GetGPUVirtualAddress(vertices),32,8};
    D3D12_VERTEX_BUFFER_VIEW ib={instances->lpVtbl->GetGPUVirtualAddress(instances),8*24,24};
    list->lpVtbl->IASetVertexBuffers(list,0,1,&vb); list->lpVtbl->IASetVertexBuffers(list,2,1,&ib);
    list->lpVtbl->DrawInstanced(list,4,4,0,3);
    D3D12_RESOURCE_BARRIER barrier={0}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=output; barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->lpVtbl->ResourceBarrier(list,1,&barrier); list->lpVtbl->CopyBufferRegion(list,readback,0,output,0,64*8*16);
    CHECK(list->lpVtbl->Close(list)); ID3D12CommandList *submitted=(ID3D12CommandList *)list;
    queue->lpVtbl->ExecuteCommandLists(queue,1,&submitted);
    ID3D12Fence *fence=NULL; CHECK(dev->lpVtbl->CreateFence(dev,0,0,&IID_ID3D12Fence,(void **)&fence));
    CHECK(queue->lpVtbl->Signal(queue,fence,1)); HANDLE event=CreateEventA(NULL,FALSE,FALSE,NULL);
    CHECK(fence->lpVtbl->SetEventOnCompletion(fence,1,event));
    if (WaitForSingleObject(event,15000)!=WAIT_OBJECT_0) { puts("FAIL GPU fence timeout"); return 1; }
    CloseHandle(event);
    D3D12_RANGE read={0,64*8*16}; CHECK(readback->lpVtbl->Map(readback,0,&read,&mapped));
    float *values=mapped; int failed=0; UINT checked=0;
    for (UINT i=3;i<7;i++) for (UINT v=0;v<4;v++) {
        float *actual=values+(i*4+v)*8*4;
        for (UINT c=0;c<4;c++) {failed+=compare(actual[c],snorm(packed[v][c]),"position_snorm",v,i,c);checked++;}
        uint16_t ip[4]; memcpy(ip,instance_data+i*24,8);
        for (UINT c=0;c<4;c++) {failed+=compare(actual[4+c],ip[c]/65535.f,"instance_unorm",v,i,c);checked++;}
        for (UINT c=0;c<4;c++) {failed+=compare(actual[8+c],instance_data[i*24+8+c]/255.f,"uv_unorm",v,i,c);checked++;}
        for (UINT a=0;a<2;a++) {
            uint32_t axis; memcpy(&axis,instance_data+i*24+12+a*4,4);
            for (UINT c=0;c<4;c++) {UINT bits=c==3?(axis>>30):((axis>>(c*10))&1023);
                failed+=compare(actual[12+a*4+c],bits/(c==3?3.f:1023.f),"packed_axis_unorm",v,i,c);checked++;}
        }
        for (UINT c=0;c<4;c++) {failed+=compare(actual[20+c],instance_data[i*24+20+c]/255.f,"colour_unorm",v,i,c);checked++;}
        printf("IDs physical_instance=%u vertex=%u GPU_VertexID=%.0f GPU_InstanceID=%.0f\n",i,v,actual[24],actual[25]);
    }
    readback->lpVtbl->Unmap(readback,0,&empty);
    printf("%s: %u packed input component checks; mismatches=%d; %s offsets, 24-byte instance stride, StartInstanceLocation=3. Fenced GPU readback.\n",failed?"FAIL":"PASS",checked,failed,explicit_offsets?"explicit":"APPEND");
    fflush(stdout);
    RELEASE(fence); RELEASE(list); RELEASE(allocator); RELEASE(queue); RELEASE(pso);
    RELEASE(root); RELEASE(heap); RELEASE(target); RELEASE(vertices); RELEASE(instances); RELEASE(output); RELEASE(readback);
    RELEASE(vs); RELEASE(ps); RELEASE(dev);
    return failed?1:0;
}
