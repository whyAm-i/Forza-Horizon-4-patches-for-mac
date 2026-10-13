#include "../../highball_repo/spike/tsshim/d3d12.c"
#include <assert.h>

static UINT expected[32], expected_count;
static const D3D12_INPUT_ELEMENT_DESC *original;
static void check_layout(const D3D12_INPUT_LAYOUT_DESC *layout)
{
    assert(layout->NumElements == expected_count);
    assert(layout->pInputElementDescs != original);
    for (UINT i=0; i<expected_count; i++) assert(layout->pInputElementDescs[i].AlignedByteOffset == expected[i]);
}
static HRESULT STDMETHODCALLTYPE graphics(ID3D12Device *dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *d, REFIID id, void **out)
{ (void)dev; (void)id; (void)out; check_layout(&d->InputLayout); return E_FAIL; }
static HRESULT STDMETHODCALLTYPE library(ID3D12PipelineLibrary *lib, const WCHAR *name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *d, REFIID id, void **out)
{ (void)lib; (void)name; return graphics(NULL,d,id,out); }
static void check_stream(const D3D12_PIPELINE_STATE_STREAM_DESC *d)
{
    /* First subobject is a 40-byte RT_FORMAT_ARRAY wrapper, then input layout. */
    D3D12_INPUT_LAYOUT_DESC layout;
    memcpy(&layout, (BYTE *)d->pPipelineStateSubobjectStream+48, sizeof layout);
    check_layout(&layout);
    struct D3D12_RT_FORMAT_ARRAY rt;
    memcpy(&rt,(BYTE *)d->pPipelineStateSubobjectStream+4,sizeof rt);
    assert(rt.NumRenderTargets==1 && rt.RTFormats[0]==DXGI_FORMAT_R8G8B8A8_UNORM);
}
static HRESULT STDMETHODCALLTYPE stream(ID3D12Device2 *dev, const D3D12_PIPELINE_STATE_STREAM_DESC *d, REFIID id, void **out)
{ (void)dev; (void)id; (void)out; check_stream(d); return E_FAIL; }
static HRESULT STDMETHODCALLTYPE libstream(ID3D12PipelineLibrary1 *lib,const WCHAR *name,const D3D12_PIPELINE_STATE_STREAM_DESC *d,REFIID id,void **out)
{ (void)lib; (void)name; return stream(NULL,d,id,out); }
int main(void)
{
    g_explicit_input_offsets=1;
    D3D12_INPUT_ELEMENT_DESC e[7]={0}, resolved[32];
    DXGI_FORMAT formats[]={DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R16G16B16A16_SNORM,DXGI_FORMAT_R16_UNORM,DXGI_FORMAT_R32G32B32_FLOAT,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8_UNORM};
    UINT slots[]={0,2,0,2,2,2,0};
    UINT offsets[]={0,0,2,8,20,24,11};
    for(UINT i=0;i<7;i++){e[i].Format=formats[i];e[i].InputSlot=slots[i];e[i].AlignedByteOffset=D3D12_APPEND_ALIGNED_ELEMENT;expected[i]=offsets[i];}
    e[6].AlignedByteOffset=11;
    D3D12_INPUT_LAYOUT_DESC in={e,7}, out;
    original=e; expected_count=7;
    assert(explicit_input_layout(&in,resolved,&out)); check_layout(&out);
    assert(e[2].AlignedByteOffset==D3D12_APPEND_ALIGNED_ELEMENT && e[6].AlignedByteOffset==11);
    g_explicit_input_offsets=0; assert(!explicit_input_layout(&in,resolved,&out)); g_explicit_input_offsets=1;
    e[5].Format=DXGI_FORMAT_BC1_UNORM;assert(!explicit_input_layout(&in,resolved,&out));e[5].Format=formats[5];
    e[5].InputSlot=32;assert(!explicit_input_layout(&in,resolved,&out));e[5].InputSlot=2;
    e[5].AlignedByteOffset=UINT_MAX-2;assert(!explicit_input_layout(&in,resolved,&out));e[5].AlignedByteOffset=D3D12_APPEND_ALIGNED_ELEMENT;
    in.NumElements=33;assert(!explicit_input_layout(&in,resolved,&out));in.NumElements=7;
    e[0].AlignedByteOffset=UINT_MAX-1;assert(!explicit_input_layout(&in,resolved,&out));e[0].AlignedByteOffset=D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd={0};gd.InputLayout=in;
    real_CreateGraphicsPipelineState=graphics; real_LoadGraphicsPipeline=library;
    assert(hook_CreateGraphicsPipelineState(NULL,&gd,NULL,NULL)==E_FAIL);
    assert(hook_LoadGraphicsPipeline(NULL,NULL,&gd,NULL,NULL)==E_FAIL);
    BYTE bytes[64]={0};
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type=D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
    struct D3D12_RT_FORMAT_ARRAY rt={0};rt.NumRenderTargets=1;rt.RTFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
    memcpy(bytes,&type,4);memcpy(bytes+4,&rt,sizeof rt);
    type=D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT;
    memcpy(bytes+40,&type,4);memcpy(bytes+48,&in,sizeof in);
    D3D12_PIPELINE_STATE_STREAM_DESC sd={sizeof bytes,bytes}, sr;void *owned=NULL;
    assert(explicit_input_stream(&sd,resolved,&sr,&owned));check_stream(&sr);free(owned);
    real_CreatePipelineStateStream=stream;real_LoadPipeline=libstream;
    assert(hook_CreatePipelineStateStream(NULL,&sd,NULL,NULL)==E_FAIL);
    assert(hook_LoadPipeline(NULL,NULL,&sd,NULL,NULL)==E_FAIL);
    assert(!memcmp(bytes+48,&in,sizeof in));
    sd.SizeInBytes=63;assert(!explicit_input_stream(&sd,resolved,&sr,&owned) && !owned);sd.SizeInBytes=64;
    type=(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE)999;memcpy(bytes,&type,4);
    assert(!explicit_input_stream(&sd,resolved,&sr,&owned) && !owned);
    puts("PASS: per-slot alignment, interleaved slots, packed and mixed widths, explicit offsets, source immutability, disabled flag, invalid format/slot/count/overflow, all four pipeline hooks, RT format stream ABI, truncated/unknown streams.");
    return 0;
}
