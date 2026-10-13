/* Opt-in snapshots. All application arguments are forwarded unchanged.
 * Census may sample tracked CPU UPLOAD CBVs; no GPU copies, barriers or submissions. */
#include "descriptor_capture.h"

static void invalidate_binding_snapshot(ListTrace *lt)
{
    memset(lt->vb_addresses, 0, sizeof lt->vb_addresses);
    memset(lt->vb_sizes, 0, sizeof lt->vb_sizes);
    memset(lt->vb_strides, 0, sizeof lt->vb_strides);
    memset(&lt->index_view, 0, sizeof lt->index_view);
    memset(lt->root_kinds, 0, sizeof lt->root_kinds);
    memset(lt->root_constant_masks, 0, sizeof lt->root_constant_masks);
}

static int is_binding_target(UINT hash)
{
    static const UINT hashes[] = {
        0xa99ec1d9, 0xc8ca6104, 0xc58458c3, 0xdaebf740, 0xdd05ba81,
        0x2b9646ba, 0x3d2781f0, 0x246b7a8c, 0xa4f7bac1, 0xe55eed61,
        0x05ab27f4, 0xea91ffda, 0x7e688961, 0xca8bb759, 0xfd8c05c4,
        0x46c59935
    };
    for (UINT i = 0; i < sizeof hashes / sizeof *hashes; i++)
        if (hash == hashes[i]) return 1;
    return 0;
}

static void write_binding_snapshot(FILE *f, const ListTrace *lt, UINT ord)
{
    fprintf(f, "# BIND ord %u | list %p | generation %u | thread %lu | root %p | bundle %d\n",
        ord, lt->list, lt->recording_generation, (unsigned long)GetCurrentThreadId(),
        lt->graphics_root, lt->is_bundle);
    for (UINT s = 0; s < 16; s++) {
        if (lt->vb_addresses[s]) fprintf(f,
            "# VB ord %u | slot %u | address %016llx | stride %u | size %u\n",
            ord, s, (unsigned long long)lt->vb_addresses[s], lt->vb_strides[s], lt->vb_sizes[s]);
    }
    fprintf(f, "# IB ord %u | address %016llx | size %u | format %u\n",
        ord, (unsigned long long)lt->index_view.BufferLocation,
        lt->index_view.SizeInBytes, (UINT)lt->index_view.Format);
    for (UINT p = 0; p < 64; p++) {
        if (!lt->root_kinds[p]) continue;
        if (lt->root_kinds[p] == 5) {
            fprintf(f, "# ROOT ord %u | param %u | constants", ord, p);
            for (UINT k = 0; k < 64; k++) {
                if (lt->root_constant_masks[p] & ((UINT64)1 << k))
                    fprintf(f, " %u:%08x", k, lt->root_constants[p][k]);
            }
            fputc('\n', f);
        } else {
            static const char *names[] = { "unset", "table", "cbv", "srv", "uav" };
            fprintf(f, "# ROOT ord %u | param %u | %s %016llx\n",
                ord, p, names[lt->root_kinds[p]], (unsigned long long)lt->root_values[p]);
            if (lt->root_kinds[p] == 1) write_table_snapshot(f, ord, p, lt->root_values[p]);
        }
    }
}

static HRESULT (STDMETHODCALLTYPE *real_capture_CreateRootSignature)(ID3D12Device *, UINT, const void *, SIZE_T, REFIID, void **);
static HRESULT STDMETHODCALLTYPE hook_capture_CreateRootSignature(ID3D12Device *dev, UINT node, const void *blob, SIZE_T size, REFIID iid, void **out)
{
    HRESULT hr = real_capture_CreateRootSignature(dev, node, blob, size, iid, out);
    if (SUCCEEDED(hr) && out && *out && blob && size && size < 1024 * 1024) {
        char path[MAX_PATH];
        CreateDirectoryA("C:\\highball\\roots", NULL);
        snprintf(path, sizeof path, "C:\\highball\\roots\\%p.dxbc", *out);
        EnterCriticalSection(&trace_lock);
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(blob, 1, size, f); fclose(f); }
        LeaveCriticalSection(&trace_lock);
        LOG("ROOT_SIGNATURE root=%p bytes=%llu file=%s", *out, (unsigned long long)size, path);
    }
    return hr;
}

static void (STDMETHODCALLTYPE *real_capture_SetGraphicsRootSignature)(ID3D12GraphicsCommandList *, ID3D12RootSignature *);
static void STDMETHODCALLTYPE hook_capture_SetGraphicsRootSignature(ID3D12GraphicsCommandList *list, ID3D12RootSignature *root)
{
    EnterCriticalSection(&trace_lock);
    ListTrace *lt = lookup_list_locked(list);
    if (lt && lt->graphics_root != root) {
        lt->graphics_root = root;
        memset(lt->root_kinds, 0, sizeof lt->root_kinds);
        memset(lt->root_constant_masks, 0, sizeof lt->root_constant_masks);
    }
    LeaveCriticalSection(&trace_lock);
    real_capture_SetGraphicsRootSignature(list, root);
}

static void capture_root_value(ID3D12GraphicsCommandList *list, UINT index, UINT kind, UINT64 value)
{
    if (index >= 64) return;
    EnterCriticalSection(&trace_lock);
    ListTrace *lt = lookup_list_locked(list);
    if (lt) { lt->root_kinds[index] = kind; lt->root_values[index] = value; }
    LeaveCriticalSection(&trace_lock);
}

#define CAPTURE_ROOT_DESCRIPTOR(Name, Kind) \
    static void (STDMETHODCALLTYPE *real_capture_##Name)(ID3D12GraphicsCommandList *, UINT, D3D12_GPU_VIRTUAL_ADDRESS); \
    static void STDMETHODCALLTYPE hook_capture_##Name(ID3D12GraphicsCommandList *list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS value) \
    { capture_root_value(list, index, Kind, value); real_capture_##Name(list, index, value); }
CAPTURE_ROOT_DESCRIPTOR(SetGraphicsRootConstantBufferView, 2)
CAPTURE_ROOT_DESCRIPTOR(SetGraphicsRootShaderResourceView, 3)
CAPTURE_ROOT_DESCRIPTOR(SetGraphicsRootUnorderedAccessView, 4)
#undef CAPTURE_ROOT_DESCRIPTOR

static void (STDMETHODCALLTYPE *real_capture_SetGraphicsRootDescriptorTable)(ID3D12GraphicsCommandList *, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
static void STDMETHODCALLTYPE hook_capture_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList *list, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE value)
{
    capture_root_value(list, index, 1, value.ptr);
    real_capture_SetGraphicsRootDescriptorTable(list, index, value);
}

static void capture_constants(ID3D12GraphicsCommandList *list, UINT index, UINT count, const void *values, UINT offset)
{
    if (index >= 64 || offset >= 64 || !values) return;
    if (count > 64 - offset) count = 64 - offset;
    EnterCriticalSection(&trace_lock);
    ListTrace *lt = lookup_list_locked(list);
    if (lt) {
        lt->root_kinds[index] = 5;
        memcpy(&lt->root_constants[index][offset], values, count * sizeof(UINT));
        for (UINT k = 0; k < count; k++) lt->root_constant_masks[index] |= (UINT64)1 << (offset + k);
    }
    LeaveCriticalSection(&trace_lock);
}

static void (STDMETHODCALLTYPE *real_capture_SetGraphicsRoot32BitConstants)(ID3D12GraphicsCommandList *, UINT, UINT, const void *, UINT);
static void STDMETHODCALLTYPE hook_capture_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList *list, UINT index, UINT count, const void *values, UINT offset)
{
    capture_constants(list, index, count, values, offset);
    real_capture_SetGraphicsRoot32BitConstants(list, index, count, values, offset);
}

static void (STDMETHODCALLTYPE *real_capture_SetGraphicsRoot32BitConstant)(ID3D12GraphicsCommandList *, UINT, UINT, UINT);
static void STDMETHODCALLTYPE hook_capture_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList *list, UINT index, UINT value, UINT offset)
{
    capture_constants(list, index, 1, &value, offset);
    real_capture_SetGraphicsRoot32BitConstant(list, index, value, offset);
}

static void (STDMETHODCALLTYPE *real_capture_IASetIndexBuffer)(ID3D12GraphicsCommandList *, const D3D12_INDEX_BUFFER_VIEW *);
static void STDMETHODCALLTYPE hook_capture_IASetIndexBuffer(ID3D12GraphicsCommandList *list, const D3D12_INDEX_BUFFER_VIEW *view)
{
    EnterCriticalSection(&trace_lock);
    ListTrace *lt = lookup_list_locked(list);
    if (lt) {
        if (view) lt->index_view = *view;
        else memset(&lt->index_view, 0, sizeof lt->index_view);
    }
    LeaveCriticalSection(&trace_lock);
    real_capture_IASetIndexBuffer(list, view);
}

static void (STDMETHODCALLTYPE *real_capture_SetDescriptorHeaps)(ID3D12GraphicsCommandList *, UINT, ID3D12DescriptorHeap *const *);
static void STDMETHODCALLTYPE hook_capture_SetDescriptorHeaps(ID3D12GraphicsCommandList *list, UINT count, ID3D12DescriptorHeap *const *heaps)
{
    EnterCriticalSection(&trace_lock);
    ListTrace *lt = lookup_list_locked(list);
    if (lt) {
        int changed = count != lt->bound_heap_count;
        for (UINT i = 0; i < count && i < 2; i++)
            if (lt->bound_heaps[i] != (heaps ? heaps[i] : NULL)) changed = 1;
        if (changed) {
            for (UINT i = 0; i < 64; i++)
                if (lt->root_kinds[i] == 1) lt->root_kinds[i] = 0;
        }
        lt->bound_heap_count = count;
        for (UINT i = 0; i < 2; i++) lt->bound_heaps[i] = heaps && i < count ? heaps[i] : NULL;
    }
    LeaveCriticalSection(&trace_lock);
    real_capture_SetDescriptorHeaps(list, count, heaps);
}

static void patch_binding_slots(ID3D12GraphicsCommandListVtbl *vt)
{
#define BINDING_SLOT(Name) patch_slot((void **)&vt->Name, (void *)hook_capture_##Name, (void **)&real_capture_##Name)
    BINDING_SLOT(SetGraphicsRootSignature);
    BINDING_SLOT(SetGraphicsRootDescriptorTable);
    BINDING_SLOT(SetGraphicsRootConstantBufferView);
    BINDING_SLOT(SetGraphicsRootShaderResourceView);
    BINDING_SLOT(SetGraphicsRootUnorderedAccessView);
    BINDING_SLOT(SetGraphicsRoot32BitConstants);
    BINDING_SLOT(SetGraphicsRoot32BitConstant);
    BINDING_SLOT(IASetIndexBuffer);
    BINDING_SLOT(SetDescriptorHeaps);
#undef BINDING_SLOT
}
