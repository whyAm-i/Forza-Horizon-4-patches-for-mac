// tsshim: a d3d12.dll that sits in front of D3DMetal's and serves DirectX 12 timestamp queries.
//
// D3DMetal 3.0 has no timestamp queries: CreateQueryHeap hands back a heap, then EndQuery and
// ResolveQueryData for query type 2 log a refusal and do nothing, so a game that times its
// frames reads garbage and crashes (Guardians of the Galaxy, highball #63). This proxy loads
// the real D3DMetal d3d12.dll (HB_D3D12_REAL, a Windows path), forwards everything, and after
// D3D12CreateDevice patches four vtable slots: the device's CreateQueryHeap answers timestamp
// heaps itself, the command list's EndQuery stamps the CPU clock into that heap, its
// ResolveQueryData copies the stamps into the destination buffer through an upload ring, and
// the queue reports the CPU clock's frequency. Everything else is D3DMetal untouched.
//
// Two more gaps are closed the same way (both found on D3DMetal 4.0b2 with Forza Horizon 6,
// 2026-10-05, see the hooks for the measurements):
// - CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL) with a shader model D3DMetal does not know
//   returns E_INVALIDARG where a current Windows runtime answers with the highest model it
//   supports. The hook asks again with lower values and returns that answer.
// - ResolveSubresource from a multisampled depth texture into a colour texture ends in a Metal
//   assertion that aborts the thread. The hook drops the call (HB_TSSHIM_DEPTHRESOLVE=1 lets it
//   through, for a D3DMetal that has learned to do it).
//
// Build: x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc -o d3d12.dll d3d12.c d3d12.def
// then stamp the "Wine builtin DLL" marker so WINEDLLPATH_PREPEND picks it up.
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HMODULE real_dll;
static HINSTANCE self;
static int dbg, idle;
static UINT64 qpc_freq;
static int trace_indirect_pso;
static CRITICAL_SECTION trace_lock;
static int trace_lock_ready;
static int g_capture_bindings;
static int g_explicit_input_offsets;
static void capture_view(UINT kind, SIZE_T handle, UINT64 address, UINT size, ID3D12Resource *resource, UINT format, UINT dimension);
// Debug lines go to a file: a GUI-subsystem game started by Steam has no stderr handle on the
// Windows side, so fprintf(stderr) vanished (The Last Caretaker, 2026-09-19). C:\highball\logs is
// where Highball keeps the renderers' own logs inside the bottle.
static FILE *dbg_file;
static FILE *dbg_out(void)
{
    if (!dbg_file) {
        char path[MAX_PATH]; snprintf(path, sizeof path, "C:\\highball\\logs\\tsshim-%lu.log", (unsigned long)GetCurrentProcessId());
        dbg_file = fopen(path, "a");
        if (!dbg_file) dbg_file = stderr;
    }
    return dbg_file;
}
static CRITICAL_SECTION log_lock; static int log_lock_ready;
#define LOG(...) do { if (dbg) { if (!log_lock_ready) { InitializeCriticalSection(&log_lock); log_lock_ready = 1; } \
    EnterCriticalSection(&log_lock); FILE *f_ = dbg_out(); SYSTEMTIME st_; GetLocalTime(&st_); \
    fprintf(f_, "%02u:%02u:%02u.%03u tsshim[%lu]: ", st_.wHour, st_.wMinute, st_.wSecond, st_.wMilliseconds, (unsigned long)GetCurrentThreadId()); fprintf(f_, __VA_ARGS__); fputc('\n', f_); fflush(f_); LeaveCriticalSection(&log_lock); } } while (0)

// Wine tells builtin modules apart by name (the file name on Wine 10, the PE's internal export name
// on CrossOver's Wine 11), so a second "d3d12.dll" at another path comes back as this shim. Highball
// lays the real one out beside the shim as apd12.dll, export name patched to match, with its Mach-O
// half as x86_64-unix/apd12.so (InstalledEngine.timestampShimDir), and passes the path in
// HB_D3D12_REAL. The bare-name fallback below is for experiments only.
static HMODULE load_real(void)
{
    WCHAR path[1024];
    DWORD n;
    if (real_dll) return real_dll;
    n = GetEnvironmentVariableW(L"HB_D3D12_REAL", path, 1024);
    if (n && n < 1024) real_dll = LoadLibraryW(path);
    else real_dll = LoadLibraryW(L"d3d12_d3dmetal.dll");
    if (real_dll == self) { fprintf(stderr, "tsshim: the real d3d12.dll resolved to the shim itself\n"); real_dll = NULL; }
    if (!real_dll) fprintf(stderr, "tsshim: cannot load the real D3DMetal d3d12.dll (%ls, error %lu)\n", n ? path : L"d3d12_d3dmetal.dll", GetLastError());
    return real_dll;
}

// Forwarded exports: a jump through a pointer resolved at load, so no signature is guessed.
#define FORWARD(name) \
    static void *p_##name __attribute__((used)); \
    __asm__(".text\n.globl " #name "\n" #name ":\n\tjmp *p_" #name "(%rip)\n");
FORWARD(D3D12CoreCreateLayeredDevice)
FORWARD(D3D12CoreGetLayeredDeviceSize)
FORWARD(D3D12CoreRegisterLayers)
FORWARD(D3D12CreateRootSignatureDeserializer)
FORWARD(D3D12CreateVersionedRootSignatureDeserializer)
FORWARD(D3D12EnableExperimentalFeatures)
FORWARD(D3D12GetDebugInterface)
FORWARD(D3D12SerializeRootSignature)
FORWARD(D3D12SerializeVersionedRootSignature)
FORWARD(GetBehaviorValue)
#define RESOLVE(name) do { p_##name = (void *)GetProcAddress(real_dll, #name); if (!p_##name) LOG("real d3d12.dll lacks %s", #name); } while (0)

// ---- the fake timestamp query heap -------------------------------------------------------

typedef struct FakeHeap {
    const ID3D12QueryHeapVtbl *lpVtbl;
    LONG ref;
    UINT count;
    UINT64 *values;
    ID3D12Device *device;
} FakeHeap;

static const ID3D12QueryHeapVtbl fake_heap_vtbl;
static int is_fake(ID3D12QueryHeap *h) { return h && h->lpVtbl == &fake_heap_vtbl; }

static HRESULT STDMETHODCALLTYPE fh_QueryInterface(ID3D12QueryHeap *iface, REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
        || IsEqualGUID(riid, &IID_ID3D12Pageable) || IsEqualGUID(riid, &IID_ID3D12QueryHeap)) {
        InterlockedIncrement(&((FakeHeap *)iface)->ref); *out = iface; return S_OK;
    }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE fh_AddRef(ID3D12QueryHeap *iface) { return InterlockedIncrement(&((FakeHeap *)iface)->ref); }
static ULONG STDMETHODCALLTYPE fh_Release(ID3D12QueryHeap *iface)
{
    FakeHeap *h = (FakeHeap *)iface; ULONG r = InterlockedDecrement(&h->ref);
    if (!r) { h->device->lpVtbl->Release(h->device); free(h->values); free(h); }
    return r;
}
static HRESULT STDMETHODCALLTYPE fh_GetPrivateData(ID3D12QueryHeap *iface, REFGUID guid, UINT *size, void *data) { if (size) *size = 0; return (HRESULT)0x887A0002L; /* DXGI_ERROR_NOT_FOUND */ }
static HRESULT STDMETHODCALLTYPE fh_SetPrivateData(ID3D12QueryHeap *iface, REFGUID guid, UINT size, const void *data) { return S_OK; }
static HRESULT STDMETHODCALLTYPE fh_SetPrivateDataInterface(ID3D12QueryHeap *iface, REFGUID guid, const IUnknown *data) { return S_OK; }
static HRESULT STDMETHODCALLTYPE fh_SetName(ID3D12QueryHeap *iface, LPCWSTR name) { return S_OK; }
static HRESULT STDMETHODCALLTYPE fh_GetDevice(ID3D12QueryHeap *iface, REFIID riid, void **out)
{
    FakeHeap *h = (FakeHeap *)iface; return h->device->lpVtbl->QueryInterface(h->device, riid, out);
}
static const ID3D12QueryHeapVtbl fake_heap_vtbl = {
    fh_QueryInterface, fh_AddRef, fh_Release, fh_GetPrivateData, fh_SetPrivateData, fh_SetPrivateDataInterface, fh_SetName, fh_GetDevice
};

// ---- the upload ring the stamps travel through ---------------------------------------------

#define RING_BYTES (8u << 20)
typedef struct Ring { ID3D12Device *device; ID3D12Resource *buffer; UINT8 *cpu; volatile LONG64 cursor; } Ring;
static Ring rings[4];
static CRITICAL_SECTION ring_lock;

static Ring *ring_for(ID3D12Device *dev)
{
    Ring *r = NULL; int i;
    EnterCriticalSection(&ring_lock);
    for (i = 0; i < 4 && !r; i++) if (rings[i].device == dev) r = &rings[i];
    for (i = 0; i < 4 && !r; i++) if (!rings[i].device) {
        D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };
        D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, RING_BYTES, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
        ID3D12Resource *buf = NULL; void *cpu = NULL;
        if (SUCCEEDED(dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&buf))
            && SUCCEEDED(buf->lpVtbl->Map(buf, 0, NULL, &cpu))) {
            rings[i].device = dev; dev->lpVtbl->AddRef(dev);
            rings[i].buffer = buf; rings[i].cpu = cpu; rings[i].cursor = 0; r = &rings[i];
            LOG("upload ring of %u bytes created", RING_BYTES);
        } else { if (buf) buf->lpVtbl->Release(buf); LOG("upload ring creation failed"); }
        break;
    }
    LeaveCriticalSection(&ring_lock);
    return r;
}

static UINT64 now_ticks(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (UINT64)c.QuadPart; }

// ---- the hooks -----------------------------------------------------------------------------

typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateQueryHeap)(ID3D12Device *, const D3D12_QUERY_HEAP_DESC *, REFIID, void **);
typedef void (STDMETHODCALLTYPE *PFN_Query)(ID3D12GraphicsCommandList *, ID3D12QueryHeap *, D3D12_QUERY_TYPE, UINT);
typedef void (STDMETHODCALLTYPE *PFN_ResolveQueryData)(ID3D12GraphicsCommandList *, ID3D12QueryHeap *, D3D12_QUERY_TYPE, UINT, UINT, ID3D12Resource *, UINT64);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetTimestampFrequency)(ID3D12CommandQueue *, UINT64 *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetClockCalibration)(ID3D12CommandQueue *, UINT64 *, UINT64 *);
static PFN_CreateQueryHeap real_CreateQueryHeap;
static PFN_Query real_EndQuery, real_BeginQuery;
static PFN_ResolveQueryData real_ResolveQueryData;
static PFN_GetTimestampFrequency real_GetTimestampFrequency;
static PFN_GetClockCalibration real_GetClockCalibration;

static HRESULT STDMETHODCALLTYPE hook_CreateQueryHeap(ID3D12Device *dev, const D3D12_QUERY_HEAP_DESC *desc, REFIID riid, void **out)
{
    FakeHeap *h; HRESULT hr;
    if (!desc || (desc->Type != D3D12_QUERY_HEAP_TYPE_TIMESTAMP && desc->Type != D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP))
        return real_CreateQueryHeap(dev, desc, riid, out);
    if (!out) return S_FALSE;
    h = calloc(1, sizeof *h); if (!h) return E_OUTOFMEMORY;
    h->lpVtbl = &fake_heap_vtbl; h->ref = 1; h->count = desc->Count;
    h->values = calloc(desc->Count ? desc->Count : 1, sizeof(UINT64));
    h->device = dev; dev->lpVtbl->AddRef(dev);
    hr = fh_QueryInterface((ID3D12QueryHeap *)h, riid, out);
    fh_Release((ID3D12QueryHeap *)h);
    LOG("timestamp query heap of %u entries served by the shim (hr 0x%08lx)", desc->Count, (unsigned long)hr);
    return hr;
}

static void STDMETHODCALLTYPE hook_EndQuery(ID3D12GraphicsCommandList *list, ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    FakeHeap *h;
    if (!is_fake(heap)) { real_EndQuery(list, heap, type, index); return; }
    h = (FakeHeap *)heap; if (index < h->count) h->values[index] = now_ticks();
}

static void STDMETHODCALLTYPE hook_BeginQuery(ID3D12GraphicsCommandList *list, ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    if (!is_fake(heap)) real_BeginQuery(list, heap, type, index);
}

static void STDMETHODCALLTYPE hook_ResolveQueryData(ID3D12GraphicsCommandList *list, ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
                                                    UINT start, UINT count, ID3D12Resource *dst, UINT64 offset)
{
    FakeHeap *h; Ring *r; UINT64 bytes, off; LONG64 pos; int tries;
    if (!is_fake(heap)) { real_ResolveQueryData(list, heap, type, start, count, dst, offset); return; }
    h = (FakeHeap *)heap; r = ring_for(h->device);
    if (!r || !dst || !count || start >= h->count) return;
    if (start + count > h->count) count = h->count - start;
    bytes = (UINT64)count * 8; if (bytes > RING_BYTES / 2) return;
    for (tries = 0; tries < 2; tries++) {
        pos = InterlockedExchangeAdd64(&r->cursor, (LONG64)bytes); off = (UINT64)pos % RING_BYTES;
        if (off + bytes <= RING_BYTES) break;
        InterlockedExchangeAdd64(&r->cursor, (LONG64)(RING_BYTES - off));   // skip the tail, start over
    }
    if (tries == 2) return;
    memcpy(r->cpu + off, h->values + start, (size_t)bytes);
    list->lpVtbl->CopyBufferRegion(list, dst, offset, r->buffer, off, bytes);
}

static HRESULT STDMETHODCALLTYPE hook_GetTimestampFrequency(ID3D12CommandQueue *q, UINT64 *f)
{
    if (!f) return E_INVALIDARG;
    *f = qpc_freq; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hook_GetClockCalibration(ID3D12CommandQueue *q, UINT64 *gpu, UINT64 *cpu)
{
    UINT64 t = now_ticks(); if (gpu) *gpu = t; if (cpu) *cpu = t; return S_OK;
}

// ---- patching ------------------------------------------------------------------------------

static void patch_slot(void **slot, void *hook, void **saved)
{
    DWORD old;
    if (*slot == hook) return;
    // D3DMetal's vtables live in its framework's __DATA segment on the Mach-O side: writable, but
    // outside Wine's view of memory, so VirtualProtect refuses the address. Try it for the PE case,
    // then write regardless (verified on D3DMetal 3.0 with Wine 10 and 11, 2026-09-07).
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) old = 0;
    if (!*saved) *saved = *slot;
    *slot = hook;
    if (old) VirtualProtect(slot, sizeof(void *), old, &old);
}

// Diagnostic only (HB_TSSHIM_DEBUG): log each resource the game creates, so a Metal assertion
// such as "MTLTextureDescriptor has invalid pixelFormat (0)" (The Last Caretaker, 2026-09-19)
// can be tied to the DXGI format D3DMetal failed to map. Forwarded untouched otherwise.
static HRESULT (STDMETHODCALLTYPE *real_CreateCommittedResource)(ID3D12Device *, const D3D12_HEAP_PROPERTIES *, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_CreatePlacedResource)(ID3D12Device *, ID3D12Heap *, UINT64, const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE *, REFIID, void **);
static void log_resource(const char *what, const D3D12_RESOURCE_DESC *d, HRESULT hr)
{
    if (!d || d->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return;
    LOG("%s dim=%d format=%u %llux%u depth/array=%u mips=%u samples=%u flags=0x%x hr=0x%lx", what, (int)d->Dimension, (unsigned)d->Format,
        (unsigned long long)d->Width, (unsigned)d->Height, (unsigned)d->DepthOrArraySize, (unsigned)d->MipLevels, (unsigned)d->SampleDesc.Count, (unsigned)d->Flags, (unsigned long)hr);
}
/* Creation-time metadata only. Never retain or dereference these objects later. */
#include "upload_capture.h"
static FILE *g_buffer_metadata_file;
static void capture_created_buffer(const D3D12_RESOURCE_DESC *d, HRESULT hr, void **out, int placed, UINT64 offset, D3D12_RESOURCE_STATES state)
{
    if (!g_capture_bindings || FAILED(hr) || !d || d->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || !out || !*out) return;
    ID3D12Resource *resource = NULL;
    IUnknown *object = *out;
    if (FAILED(object->lpVtbl->QueryInterface(object, &IID_ID3D12Resource, (void **)&resource)) || !resource) return;
    UINT64 address = resource->lpVtbl->GetGPUVirtualAddress(resource);
    D3D12_HEAP_PROPERTIES properties = { 0 };
    D3D12_HEAP_FLAGS flags = 0;
    HRESULT heap_hr = resource->lpVtbl->GetHeapProperties(resource, &properties, &flags);
    if (SUCCEEDED(heap_hr) && properties.Type == D3D12_HEAP_TYPE_UPLOAD)
        register_upload_resource(resource, object, address, d->Width);
    EnterCriticalSection(&trace_lock);
    if (!g_buffer_metadata_file) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "C:\\highball\\buffers-%lu.txt", (unsigned long)GetCurrentProcessId());
        g_buffer_metadata_file = fopen(path, "w");
        if (g_buffer_metadata_file) fputs("# binding-capture-v5: creation metadata; census CPU upload samples are stored separately\n", g_buffer_metadata_file);
    }
    if (g_buffer_metadata_file) {
        fprintf(g_buffer_metadata_file, "resource %p | address %016llx | size %llu | heap_type %u | heap_hr %08lx | heap_flags %u | placed %d | heap_offset %llu | initial_state %u\n",
            resource, (unsigned long long)address, (unsigned long long)d->Width,
            SUCCEEDED(heap_hr) ? (UINT)properties.Type : 0, (unsigned long)heap_hr, (UINT)flags,
            placed, (unsigned long long)offset, (UINT)state);
        fflush(g_buffer_metadata_file);
    }
    LeaveCriticalSection(&trace_lock);
    resource->lpVtbl->Release(resource);
}
static HRESULT STDMETHODCALLTYPE hook_CreateCommittedResource(ID3D12Device *dev, const D3D12_HEAP_PROPERTIES *hp, D3D12_HEAP_FLAGS hf, const D3D12_RESOURCE_DESC *d, D3D12_RESOURCE_STATES st, const D3D12_CLEAR_VALUE *cv, REFIID riid, void **out)
{
    if (dbg) { log_resource("CreateCommittedResource begin", d, 0); }
    HRESULT hr = real_CreateCommittedResource(dev, hp, hf, d, st, cv, riid, out);
    capture_created_buffer(d, hr, out, 0, 0, st);
    if (dbg) { log_resource("CreateCommittedResource done", d, hr); if (d && d->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) LOG("  -> res=%p", out ? *out : NULL); }
    return hr;
}
static HRESULT STDMETHODCALLTYPE hook_CreatePlacedResource(ID3D12Device *dev, ID3D12Heap *heap, UINT64 off, const D3D12_RESOURCE_DESC *d, D3D12_RESOURCE_STATES st, const D3D12_CLEAR_VALUE *cv, REFIID riid, void **out)
{
    if (dbg) { log_resource("CreatePlacedResource begin", d, 0); }
    HRESULT hr = real_CreatePlacedResource(dev, heap, off, d, st, cv, riid, out);
    capture_created_buffer(d, hr, out, 1, off, st);
    if (dbg) { log_resource("CreatePlacedResource done", d, hr); if (d && d->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) LOG("  -> res=%p", out ? *out : NULL); }
    return hr;
}
static HRESULT (STDMETHODCALLTYPE *real_CreateReservedResource)(ID3D12Device *, const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE hook_CreateReservedResource(ID3D12Device *dev, const D3D12_RESOURCE_DESC *d, D3D12_RESOURCE_STATES st, const D3D12_CLEAR_VALUE *cv, REFIID riid, void **out)
{
    if (dbg) { log_resource("CreateReservedResource begin", d, 0); }
    HRESULT hr = real_CreateReservedResource(dev, d, st, cv, riid, out);
    if (dbg) { log_resource("CreateReservedResource done", d, hr); if (d && d->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) LOG("  -> res=%p", out ? *out : NULL); }
    return hr;
}
// Views: a cast over a typeless texture is where a format D3DMetal cannot map would surface.
static void (STDMETHODCALLTYPE *real_CreateShaderResourceView)(ID3D12Device *, ID3D12Resource *, const D3D12_SHADER_RESOURCE_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void (STDMETHODCALLTYPE *real_CreateUnorderedAccessView)(ID3D12Device *, ID3D12Resource *, ID3D12Resource *, const D3D12_UNORDERED_ACCESS_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void (STDMETHODCALLTYPE *real_CreateRenderTargetView)(ID3D12Device *, ID3D12Resource *, const D3D12_RENDER_TARGET_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void (STDMETHODCALLTYPE *real_CreateDepthStencilView)(ID3D12Device *, ID3D12Resource *, const D3D12_DEPTH_STENCIL_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void STDMETHODCALLTYPE hook_CreateShaderResourceView(ID3D12Device *dev, ID3D12Resource *res, const D3D12_SHADER_RESOURCE_VIEW_DESC *d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    if (dbg && d) LOG("SRV begin res=%p format=%u viewdim=%d", res, (unsigned)d->Format, (int)d->ViewDimension);
    real_CreateShaderResourceView(dev, res, d, h);
    if (g_capture_bindings) capture_view(3, h.ptr, d && d->ViewDimension == D3D12_SRV_DIMENSION_BUFFER ? d->Buffer.FirstElement : 0,
        d && d->ViewDimension == D3D12_SRV_DIMENSION_BUFFER ? d->Buffer.StructureByteStride : 0,
        res, d ? d->Format : 0, d ? d->ViewDimension : 0);
    if (dbg && d) LOG("SRV done res=%p", res);
}
static void STDMETHODCALLTYPE hook_CreateUnorderedAccessView(ID3D12Device *dev, ID3D12Resource *res, ID3D12Resource *counter, const D3D12_UNORDERED_ACCESS_VIEW_DESC *d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    if (dbg && d) LOG("UAV begin res=%p format=%u viewdim=%d", res, (unsigned)d->Format, (int)d->ViewDimension);
    real_CreateUnorderedAccessView(dev, res, counter, d, h);
    if (g_capture_bindings) capture_view(4, h.ptr, d && d->ViewDimension == D3D12_UAV_DIMENSION_BUFFER ? d->Buffer.FirstElement : 0,
        d && d->ViewDimension == D3D12_UAV_DIMENSION_BUFFER ? d->Buffer.StructureByteStride : 0,
        res, d ? d->Format : 0, d ? d->ViewDimension : 0);
    if (dbg && d) LOG("UAV done res=%p", res);
}
static void STDMETHODCALLTYPE hook_CreateRenderTargetView(ID3D12Device *dev, ID3D12Resource *res, const D3D12_RENDER_TARGET_VIEW_DESC *d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    if (dbg && d) LOG("RTV begin res=%p format=%u viewdim=%d", res, (unsigned)d->Format, (int)d->ViewDimension);
    real_CreateRenderTargetView(dev, res, d, h);
    if (dbg && d) LOG("RTV done res=%p", res);
}
static void STDMETHODCALLTYPE hook_CreateDepthStencilView(ID3D12Device *dev, ID3D12Resource *res, const D3D12_DEPTH_STENCIL_VIEW_DESC *d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    if (dbg && d) LOG("DSV begin res=%p format=%u viewdim=%d", res, (unsigned)d->Format, (int)d->ViewDimension);
    real_CreateDepthStencilView(dev, res, d, h);
    if (dbg && d) LOG("DSV done res=%p", res);
}

// ---- feature queries -------------------------------------------------------------------------

// A program asks for the highest shader model it understands and the runtime answers with the
// highest one it supports at or below that. D3DMetal only accepts the values it knows (up to 0x68,
// SM 6.8, on 4.0b2, answering 0x66) and returns E_INVALIDARG for a newer one, which a program built
// against a newer SDK takes as "no usable shader model": Forza Horizon 6's 461.691 build asks with
// 0x6a and stopped at "Your graphics card is not supported (FH206)" (M4, macOS 27.0, D3DMetal 4.0b2).
// The Windows runtime the game ships knows its own enum, so on Windows the question never fails.
static int force_no_fp16;
static int max_sm;
static int override_tiled_tier = -1;

static HRESULT (STDMETHODCALLTYPE *real_CheckFeatureSupport)(ID3D12Device *, D3D12_FEATURE, void *, UINT);
static HRESULT STDMETHODCALLTYPE hook_CheckFeatureSupport(ID3D12Device *dev, D3D12_FEATURE feature, void *data, UINT size)
{
    HRESULT hr = real_CheckFeatureSupport(dev, feature, data, size);
    if (feature == D3D12_FEATURE_SHADER_MODEL && SUCCEEDED(hr) && data && size >= sizeof(D3D12_FEATURE_DATA_SHADER_MODEL)) {
        D3D12_FEATURE_DATA_SHADER_MODEL *sm = data;
        if (max_sm && sm->HighestShaderModel > max_sm) {
            LOG("shader model clamped from 0x%x to 0x%x", (unsigned)sm->HighestShaderModel, (unsigned)max_sm);
            sm->HighestShaderModel = max_sm;
        }
    } else if (feature == D3D12_FEATURE_SHADER_MODEL && hr == E_INVALIDARG && data && size >= sizeof(D3D12_FEATURE_DATA_SHADER_MODEL)) {
        D3D12_FEATURE_DATA_SHADER_MODEL *sm = data;
        UINT asked = sm->HighestShaderModel, v;
        for (v = asked - 1; v >= D3D_SHADER_MODEL_6_0 && v < asked; v--) {
            sm->HighestShaderModel = v;
            if (SUCCEEDED(hr = real_CheckFeatureSupport(dev, feature, data, size))) break;
        }
        if (FAILED(hr)) sm->HighestShaderModel = asked;
        if (max_sm && sm->HighestShaderModel > max_sm) sm->HighestShaderModel = max_sm;
        LOG("shader model query for 0x%x answered as 0x%x (hr 0x%08lx)", asked, (unsigned)sm->HighestShaderModel, (unsigned long)hr);
    } else if (feature == D3D12_FEATURE_D3D12_OPTIONS && SUCCEEDED(hr) && data && size >= sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS)) {
        D3D12_FEATURE_DATA_D3D12_OPTIONS *opt = data;
        LOG("D3D12_OPTIONS: MinPrecision=0x%x TiledResources=%d", opt->MinPrecisionSupport, opt->TiledResourcesTier);
        if (force_no_fp16) {
            opt->MinPrecisionSupport = D3D12_SHADER_MIN_PRECISION_SUPPORT_NONE;
            LOG("  -> forced MinPrecisionSupport = 0");
        }
        if (override_tiled_tier >= 0) {
            LOG("  -> overriding TiledResourcesTier from %d to %d", opt->TiledResourcesTier, override_tiled_tier);
            opt->TiledResourcesTier = (D3D12_TILED_RESOURCES_TIER)override_tiled_tier;
        }
    } else if ((int)feature == 23 /* D3D12_FEATURE_D3D12_OPTIONS4 */ && SUCCEEDED(hr) && data) {
        D3D12_FEATURE_DATA_D3D12_OPTIONS4 *opt4 = data;
        LOG("D3D12_OPTIONS4: Native16Bit=%d", opt4->Native16BitShaderOpsSupported);
        if (force_no_fp16) {
            opt4->Native16BitShaderOpsSupported = FALSE;
            LOG("  -> forced Native16BitShaderOpsSupported = 0");
        }
    } else if (dbg) LOG("CheckFeatureSupport feature=%d size=%u hr=0x%08lx", (int)feature, size, (unsigned long)hr);
    return hr;
}

// ---- depth resolve ---------------------------------------------------------------------------

// ResolveSubresource(colour texture, multisampled depth texture, R32_FLOAT): Windows drivers take
// it. D3DMetal 4.0b2 builds a Metal texture view of the source in the destination's format, and
// Metal has no cast between depth and colour formats:
//   _mtlValidateArgumentsForTextureViewOnDevice: failed assertion `Texture Creation
//   source texture pixelFormat (MTLPixelFormatDepth32Float) not compatible with texture view
//   pixelFormat (MTLPixelFormatR32Float)'
// The assertion aborts D3DMetal's thread with the game's locks held, so the game stands still for
// good. Forza Horizon 6 issues the call every frame in its Character Select scene (960x544, 4
// samples) and froze there on every load; with the call dropped the scene draws and the game goes
// on (820 calls dropped in 36 s, M4, macOS 27.0). The destination keeps whatever it held, so an
// effect that reads it may be off; nothing visible in that scene.
static int pass_depth_resolve;
static void (STDMETHODCALLTYPE *real_ResolveSubresource)(ID3D12GraphicsCommandList *, ID3D12Resource *, UINT, ID3D12Resource *, UINT, DXGI_FORMAT);
static int is_depth_texture(ID3D12Resource *res, D3D12_RESOURCE_DESC *d)
{
    memset(d, 0, sizeof *d);
    if (!res) return 0;
    res->lpVtbl->GetDesc(res, d);
    return d->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER && (d->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0;
}
static void STDMETHODCALLTYPE hook_ResolveSubresource(ID3D12GraphicsCommandList *list, ID3D12Resource *dst, UINT dst_sub, ID3D12Resource *src, UINT src_sub, DXGI_FORMAT format)
{
    D3D12_RESOURCE_DESC dd, sd;
    if (!pass_depth_resolve && is_depth_texture(src, &sd) && sd.SampleDesc.Count > 1 && !is_depth_texture(dst, &dd)) {
        static LONG told;
        if (!InterlockedExchange(&told, 1))
            LOG("multisampled depth resolve into a colour texture dropped (source format %u, %u samples, destination format %u %llux%u, format %u)",
                (unsigned)sd.Format, (unsigned)sd.SampleDesc.Count, (unsigned)dd.Format, (unsigned long long)dd.Width, (unsigned)dd.Height, (unsigned)format);
        return;
    }
    real_ResolveSubresource(list, dst, dst_sub, src, src_sub, format);
}

// ---- pipeline state creation: GS, Hull/Domain Shader diagnostic & suppression -----------
static int no_gs;
static int no_tess;

#define TRACE_HASH_CAPACITY 65536
#define TRACE_HASH_MASK (TRACE_HASH_CAPACITY - 1)
#define TRACE_MAX_SIGNATURES 128
#define TRACE_MAX_LISTS 256
#define MAX_ARG_SAMPLES 64
#define MAX_CENSUS_PSOS 2048

#define MAX_INPUT_ELEMENTS 16

typedef struct InputElementTrace {
    char semantic_name[32];
    UINT semantic_index;
    UINT format;
    UINT input_slot;
    UINT aligned_byte_offset;
    UINT input_slot_class;
    UINT instance_data_step_rate;
} InputElementTrace;

typedef struct PipelineTrace {
    ID3D12PipelineState *pso;
    IUnknown *canonical_unk;
    UINT creation_index;
    const char *creation_type;
    int is_compute;
    UINT vs_len, ps_len, gs_len, hs_len, ds_len, cs_len;
    UINT vs_hash, ps_hash, gs_hash, hs_hash, ds_hash, cs_hash;
    UINT topology, num_rtvs, sample_count;
    DXGI_FORMAT rtv_formats[8];
    int reported;
    UINT num_input_elements;
    InputElementTrace input_elements[MAX_INPUT_ELEMENTS];
} PipelineTrace;

typedef struct SignatureTrace {
    ID3D12CommandSignature *signature;
    IUnknown *canonical_unk;
    UINT byte_stride, num_args;
    UINT arg_types[16];
} SignatureTrace;

typedef struct ListTrace {
    ID3D12GraphicsCommandList *list;
    IUnknown *canonical_unk;
    ID3D12PipelineState *pso;
    ID3D12CommandSignature *last_signature;
    int unknown_reported;
    int is_bundle;
    UINT vb_strides[16];
    UINT vb_sizes[16];
    UINT num_rtvs;
    int has_dsv;
    UINT64 vb_addresses[16];
    D3D12_INDEX_BUFFER_VIEW index_view;
    ID3D12RootSignature *graphics_root;
    UINT root_kinds[64];
    UINT64 root_values[64];
    UINT root_constants[64][64];
    UINT64 root_constant_masks[64];
    UINT recording_generation;
    ID3D12DescriptorHeap *bound_heaps[2];
    UINT bound_heap_count;
} ListTrace;

#define MAX_BUNDLE_TRACK 64
#define MAX_BUNDLE_DRAWS 256

typedef struct BundleDrawInfo {
    ID3D12PipelineState *pso;
    int is_indexed;
    UINT count;
    UINT icount;
    UINT vb_strides[16];
} BundleDrawInfo;

typedef struct BundleTrace {
    ID3D12GraphicsCommandList *bundle;
    IUnknown *canonical_unk;
    UINT draw_count;
    BundleDrawInfo draws[MAX_BUNDLE_DRAWS];
} BundleTrace;

static BundleTrace g_bundles[MAX_BUNDLE_TRACK];
static UINT g_bundle_count = 0;
static UINT g_bundles_executed_in_frame = 0;

typedef struct IndirectArgSample {
    UINT sample_id;
    ID3D12PipelineState *pso;
    UINT creation_index;
    UINT64 arg_offset;
    UINT vertex_count;
    UINT instance_count;
    UINT start_vertex;
    UINT start_instance;
    int readback_pending;
} IndirectArgSample;

typedef struct DrawCensusEntry {
    ID3D12PipelineState *pso;
    UINT creation_index;
    UINT vs_hash;
    UINT ps_hash;
    UINT topology;
    const char *type_name;
    UINT draw_count;
    UINT draw_indexed_count;
    UINT indirect_count;
    UINT dispatch_count;
    UINT64 instance_count_sum;
    UINT instance_count_max;
    UINT64 vertex_index_count_sum;
    UINT64 thread_groups_sum;
    UINT vb_strides[16];
    int vb_strides_captured;
} DrawCensusEntry;

static PipelineTrace pipeline_hash_map[TRACE_HASH_CAPACITY];
static UINT pipeline_hash_count = 0;
static int pipeline_hash_full_warned = 0;
static volatile LONG g_pso_creation_counter = 0;

static SignatureTrace signature_traces[TRACE_MAX_SIGNATURES];
static ListTrace list_traces[TRACE_MAX_LISTS];
static UINT signature_trace_count;
static UINT trace_indirect_context_count;

// Step A & Step 2: Draw Census (Trigger-file + optional frame counter)
static int g_census_enabled = 0;
static int g_census_target_frame = -1; // -1 = disabled by default, set via HB_TSSHIM_CENSUS_FRAME
static int g_census_target_frame_done = 0;
static int g_census_active = 0;
static int g_trigger_census_pending = 0;
static int g_census_trigger_count = 0;
static UINT g_world_frame_index = 0;
static int g_world_loaded = 0;
static int g_frame_has_present = 0;
static DrawCensusEntry g_census_entries[MAX_CENSUS_PSOS];
static UINT g_census_entry_count = 0;

// Step B: Arg Values
static int capture_indirect_args = 0;
static int arg_probe_done = 0;
static int args_captured_count = 0;
static IndirectArgSample arg_samples[MAX_ARG_SAMPLES];
static ID3D12Resource *g_readback_buf = NULL;
static int g_readback_recorded = 0;
static int g_readback_completed = 0;

// Step C: Diagnostic Bisection
static UINT g_skip_vs_hashes[32];
static UINT g_num_skip_vs = 0;

static IUnknown *get_canonical_unk(IUnknown *obj)
{
    IUnknown *unk = NULL;
    if (!obj || !obj->lpVtbl || !obj->lpVtbl->QueryInterface) return NULL;
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_IUnknown, (void **)&unk)) && unk) {
        unk->lpVtbl->Release(unk);
        return unk;
    }
    return obj;
}

static inline UINT hash_pso_ptr(void *ptr)
{
    uintptr_t v = (uintptr_t)ptr;
    v ^= v >> 16;
    v *= 0x45d9f3b;
    v ^= v >> 16;
    return (UINT)(v & TRACE_HASH_MASK);
}

static const char *arg_type_name(UINT type)
{
    switch (type) {
    case 0: return "draw";
    case 1: return "draw-indexed";
    case 2: return "dispatch";
    case 3: return "VBV";
    case 4: return "IBV";
    case 5: return "constant";
    case 6: return "root-CBV";
    case 7: return "root-SRV";
    case 8: return "root-UAV";
    case 9: return "dispatch-rays";
    case 10: return "dispatch-mesh";
    default: return "unknown";
    }
}

static UINT hash_shader(const D3D12_SHADER_BYTECODE *shader)
{
    const UINT8 *p = (const UINT8 *)shader->pShaderBytecode;
    UINT h = 2166136261u;
    SIZE_T i;
    if (!p || !shader->BytecodeLength) return 0;
    for (i = 0; i < shader->BytecodeLength; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static UINT pso_hash(const PipelineTrace *p)
{
    UINT h = 2166136261u;
    h = (h ^ p->vs_hash) * 16777619u;
    h = (h ^ p->ps_hash) * 16777619u;
    h = (h ^ p->gs_hash) * 16777619u;
    h = (h ^ p->hs_hash) * 16777619u;
    h = (h ^ p->ds_hash) * 16777619u;
    h = (h ^ p->cs_hash) * 16777619u;
    h = (h ^ p->topology) * 16777619u;
    h = (h ^ p->sample_count) * 16777619u;
    return h;
}

static PipelineTrace *lookup_pipeline_locked(ID3D12PipelineState *bound, IUnknown *canonical_unk)
{
    if (!bound) return NULL;
    UINT h = hash_pso_ptr(bound);
    for (UINT step = 0; step < TRACE_HASH_CAPACITY; step++) {
        UINT idx = (h + step) & TRACE_HASH_MASK;
        if (!pipeline_hash_map[idx].pso) break;
        if (pipeline_hash_map[idx].pso == bound) return &pipeline_hash_map[idx];
    }
    if (canonical_unk && canonical_unk != (IUnknown *)bound) {
        UINT h_unk = hash_pso_ptr(canonical_unk);
        for (UINT step = 0; step < TRACE_HASH_CAPACITY; step++) {
            UINT idx = (h_unk + step) & TRACE_HASH_MASK;
            if (!pipeline_hash_map[idx].pso) break;
            if (pipeline_hash_map[idx].canonical_unk == canonical_unk) {
                return &pipeline_hash_map[idx];
            }
        }
    }
    return NULL;
}

static PipelineTrace *lookup_pipeline(ID3D12PipelineState *bound)
{
    PipelineTrace *res = NULL;
    if (!bound) return NULL;
    IUnknown *unk = get_canonical_unk((IUnknown *)bound);
    EnterCriticalSection(&trace_lock);
    res = lookup_pipeline_locked(bound, unk);
    LeaveCriticalSection(&trace_lock);
    return res;
}

static PipelineTrace *insert_pipeline_locked(ID3D12PipelineState *pso, IUnknown *canonical_unk)
{
    if (!pso) return NULL;
    UINT h = hash_pso_ptr(pso);
    for (UINT step = 0; step < TRACE_HASH_CAPACITY; step++) {
        UINT idx = (h + step) & TRACE_HASH_MASK;
        if (pipeline_hash_map[idx].pso == pso) return &pipeline_hash_map[idx];
        if (!pipeline_hash_map[idx].pso) {
            if (pipeline_hash_count < TRACE_HASH_CAPACITY - 1) {
                pipeline_hash_count++;
                pipeline_hash_map[idx].pso = pso;
                pipeline_hash_map[idx].canonical_unk = canonical_unk;
                return &pipeline_hash_map[idx];
            }
            break;
        }
    }
    if (!pipeline_hash_full_warned) {
        pipeline_hash_full_warned = 1;
        LOG("WARNING: pipeline hash map FULL (%u entries), cannot register PSO %p", pipeline_hash_count, pso);
    }
    return NULL;
}


static const char *dxgi_format_name(UINT format)
{
    switch (format) {
    case 0: return "DXGI_FORMAT_UNKNOWN";
    case 1: return "DXGI_FORMAT_R32G32B32A32_TYPELESS";
    case 2: return "DXGI_FORMAT_R32G32B32A32_FLOAT";
    case 3: return "DXGI_FORMAT_R32G32B32A32_UINT";
    case 4: return "DXGI_FORMAT_R32G32B32A32_SINT";
    case 5: return "DXGI_FORMAT_R32G32B32_TYPELESS";
    case 6: return "DXGI_FORMAT_R32G32B32_FLOAT";
    case 7: return "DXGI_FORMAT_R32G32B32_UINT";
    case 8: return "DXGI_FORMAT_R32G32B32_SINT";
    case 9: return "DXGI_FORMAT_R16G16B16A16_TYPELESS";
    case 10: return "DXGI_FORMAT_R16G16B16A16_FLOAT";
    case 11: return "DXGI_FORMAT_R16G16B16A16_UNORM";
    case 12: return "DXGI_FORMAT_R16G16B16A16_UINT";
    case 13: return "DXGI_FORMAT_R16G16B16A16_SNORM";
    case 14: return "DXGI_FORMAT_R16G16B16A16_SINT";
    case 15: return "DXGI_FORMAT_R32G32_TYPELESS";
    case 16: return "DXGI_FORMAT_R32G32_FLOAT";
    case 17: return "DXGI_FORMAT_R32G32_UINT";
    case 18: return "DXGI_FORMAT_R32G32_SINT";
    case 23: return "DXGI_FORMAT_R10G10B10A2_TYPELESS";
    case 24: return "DXGI_FORMAT_R10G10B10A2_UNORM";
    case 25: return "DXGI_FORMAT_R10G10B10A2_UINT";
    case 26: return "DXGI_FORMAT_R11G11B10_FLOAT";
    case 27: return "DXGI_FORMAT_R8G8B8A8_TYPELESS";
    case 28: return "DXGI_FORMAT_R8G8B8A8_UNORM";
    case 29: return "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
    case 30: return "DXGI_FORMAT_R8G8B8A8_UINT";
    case 31: return "DXGI_FORMAT_R8G8B8A8_SNORM";
    case 32: return "DXGI_FORMAT_R8G8B8A8_SINT";
    case 33: return "DXGI_FORMAT_R16G16_TYPELESS";
    case 34: return "DXGI_FORMAT_R16G16_FLOAT";
    case 35: return "DXGI_FORMAT_R16G16_UNORM";
    case 36: return "DXGI_FORMAT_R16G16_UINT";
    case 37: return "DXGI_FORMAT_R16G16_SNORM";
    case 38: return "DXGI_FORMAT_R16G16_SINT";
    case 39: return "DXGI_FORMAT_R32_TYPELESS";
    case 40: return "DXGI_FORMAT_D32_FLOAT";
    case 41: return "DXGI_FORMAT_R32_FLOAT";
    case 42: return "DXGI_FORMAT_R32_UINT";
    case 43: return "DXGI_FORMAT_R32_SINT";
    case 49: return "DXGI_FORMAT_R8G8_UNORM";
    case 50: return "DXGI_FORMAT_R8G8_UINT";
    case 51: return "DXGI_FORMAT_R8G8_SNORM";
    case 52: return "DXGI_FORMAT_R8G8_SINT";
    case 53: return "DXGI_FORMAT_R16_TYPELESS";
    case 54: return "DXGI_FORMAT_R16_FLOAT";
    case 55: return "DXGI_FORMAT_D16_UNORM";
    case 56: return "DXGI_FORMAT_R16_UNORM";
    case 57: return "DXGI_FORMAT_R16_UINT";
    case 58: return "DXGI_FORMAT_R16_SNORM";
    case 59: return "DXGI_FORMAT_R16_SINT";
    case 60: return "DXGI_FORMAT_R8_TYPELESS";
    case 61: return "DXGI_FORMAT_R8_UNORM";
    case 62: return "DXGI_FORMAT_R8_UINT";
    case 63: return "DXGI_FORMAT_R8_SNORM";
    case 64: return "DXGI_FORMAT_R8_SINT";
    case 65: return "DXGI_FORMAT_A8_UNORM";
    case 87: return "DXGI_FORMAT_B8G8R8A8_UNORM";
    default: return "DXGI_FORMAT_UNKNOWN";
    }
}

static int is_step1_target_pso(UINT creation_index, UINT vs_hash)
{
    static const UINT target_psos[] = { 9703, 8653, 8656, 9058, 9196, 8541, 8616, 8761, 8880, 9077 };
    static const UINT target_vs[] = { 0xa99ec1d9, 0xc8ca6104, 0xc58458c3, 0xdaebf740, 0xdd05ba81, 0x46c59935 };
    for (size_t i = 0; i < sizeof(target_psos) / sizeof(target_psos[0]); i++) {
        if (target_psos[i] == creation_index) return 1;
    }
    for (size_t i = 0; i < sizeof(target_vs) / sizeof(target_vs[0]); i++) {
        if (target_vs[i] == vs_hash) return 1;
    }
    return 0;
}

static void dump_target_pso_input_layout(const PipelineTrace *p, const char *phase)
{
    if (!p) return;
    LOG("================ INPUT LAYOUT DUMP [%s] PSO(%u) [VS=%08x, Type=%s, Elements=%u] ================",
        phase, p->creation_index, p->vs_hash, p->creation_type ? p->creation_type : "unknown", p->num_input_elements);
    for (UINT k = 0; k < p->num_input_elements; k++) {
        const InputElementTrace *el = &p->input_elements[k];
        LOG("  [Elem %2u] SemanticName=%s SemanticIndex=%u Format=%s (%u) Slot=%u AlignedByteOffset=%u SlotClass=%s (%u) InstanceDataStepRate=%u",
            k, el->semantic_name, el->semantic_index, dxgi_format_name(el->format), el->format,
            el->input_slot, el->aligned_byte_offset,
            el->input_slot_class == 1 ? "D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA" : "D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA",
            el->input_slot_class, el->instance_data_step_rate);
    }
    if (p->num_input_elements == 0) {
        LOG("  (No input elements declared / InputLayout empty)");
    }
    LOG("=================================================================================================");
}

static void maybe_dump_vs_bytecode(UINT hash, const void *data, SIZE_T len)
{
    if (!data || !len || len > 8 * 1024 * 1024) return;
    static const UINT target_hashes[] = {
        0xa99ec1d9, 0xc8ca6104, 0xc58458c3, 0xdaebf740, 0xdd05ba81,
        0x2b9646ba, 0x3d2781f0, 0x246b7a8c, 0xa4f7bac1, 0xe55eed61,
        0x05ab27f4, 0xea91ffda, 0x7e688961, 0xca8bb759, 0xfd8c05c4,
        0x46c59935
    };
    int match = 0;
    for (size_t i = 0; i < sizeof(target_hashes) / sizeof(target_hashes[0]); i++) {
        if (target_hashes[i] == hash) { match = 1; break; }
    }
    if (!match) return;

    CreateDirectoryA("C:\\highball", NULL);
    CreateDirectoryA("C:\\highball\\shaders", NULL);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "C:\\highball\\shaders\\%08x.dxbc", hash);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, data, (DWORD)len, &written, NULL);
        CloseHandle(h);
        LOG("DUMPED VS BYTECODE: %s (%u bytes)", path, (UINT)len);
    }
}

/* Capture material shading for the two user-identified families. Selection uses
 * the VS family so quality-dependent PS variants are included. No bytecode is
 * changed. Deduplication and byte/file caps bound disk work per process. */
static void maybe_dump_outlier_ps(UINT vs_hash, const D3D12_SHADER_BYTECODE *ps)
{
    static UINT hashes[128], count;
    static SIZE_T total;
    if (!g_capture_bindings || (vs_hash != 0xdaebf740 && vs_hash != 0x46c59935)
        || !ps || !ps->pShaderBytecode || !ps->BytecodeLength
        || ps->BytecodeLength > 8 * 1024 * 1024) return;
    UINT hash = hash_shader(ps);
    EnterCriticalSection(&trace_lock);
    for (UINT i = 0; i < count; i++) {
        if (hashes[i] == hash) { LeaveCriticalSection(&trace_lock); return; }
    }
    if (count == sizeof hashes / sizeof *hashes || ps->BytecodeLength > 8 * 1024 * 1024 - total) {
        LeaveCriticalSection(&trace_lock);
        return;
    }
    CreateDirectoryA("C:\\highball\\shaders", NULL);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "C:\\highball\\shaders\\ps-%08x.dxbc", hash);
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        BOOL ok = WriteFile(file, ps->pShaderBytecode, (DWORD)ps->BytecodeLength, &written, NULL);
        CloseHandle(file);
        if (ok && written == ps->BytecodeLength) {
            hashes[count++] = hash;
            total += ps->BytecodeLength;
            LOG("DUMPED OUTLIER PS: VS=%08x PS=%08x bytes=%u file=%s", vs_hash, hash, (UINT)written, path);
        }
    }
    LeaveCriticalSection(&trace_lock);
}

static PipelineTrace *register_pso(ID3D12PipelineState *pso, const char *type_name, const PipelineTrace *init_data)
{
    if (!pso) return NULL;
    IUnknown *unk = get_canonical_unk((IUnknown *)pso);
    EnterCriticalSection(&trace_lock);
    PipelineTrace *entry = lookup_pipeline_locked(pso, unk);
    if (!entry) {
        entry = insert_pipeline_locked(pso, unk);
        if (entry) {
            if (init_data) *entry = *init_data;
            entry->pso = pso;
            entry->canonical_unk = unk;
            entry->creation_type = type_name;
            entry->creation_index = (UINT)InterlockedIncrement(&g_pso_creation_counter);
            if (is_step1_target_pso(entry->creation_index, entry->vs_hash)) {
                dump_target_pso_input_layout(entry, "Creation");
            }
            if (dbg) {
                if (entry->is_compute) {
                    LOG("REGISTER_PSO[%u] type=%s pso=%p CS=%u/%08x",
                        entry->creation_index, type_name, pso, entry->cs_len, entry->cs_hash);
                } else {
                    LOG("REGISTER_PSO[%u] type=%s pso=%p VS=%u/%08x PS=%u/%08x GS=%u/%08x HS=%u/%08x DS=%u/%08x",
                        entry->creation_index, type_name, pso,
                        entry->vs_len, entry->vs_hash, entry->ps_len, entry->ps_hash,
                        entry->gs_len, entry->gs_hash, entry->hs_len, entry->hs_hash,
                        entry->ds_len, entry->ds_hash);
                }
            }
        }
    }
    LeaveCriticalSection(&trace_lock);
    return entry;
}

static void remember_pipeline(ID3D12PipelineState *pso, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *d, const char *type_name)
{
    PipelineTrace t;
    if (!pso || !d) return;
    memset(&t, 0, sizeof t);
    t.vs_len = (UINT)d->VS.BytecodeLength; t.ps_len = (UINT)d->PS.BytecodeLength;
    t.gs_len = (UINT)d->GS.BytecodeLength; t.hs_len = (UINT)d->HS.BytecodeLength; t.ds_len = (UINT)d->DS.BytecodeLength;
    t.vs_hash = hash_shader(&d->VS); t.ps_hash = hash_shader(&d->PS);
    t.gs_hash = hash_shader(&d->GS); t.hs_hash = hash_shader(&d->HS); t.ds_hash = hash_shader(&d->DS);
    t.topology = d->PrimitiveTopologyType;
    t.num_rtvs = d->NumRenderTargets;
    t.sample_count = d->SampleDesc.Count;
    for (UINT j = 0; j < d->NumRenderTargets && j < 8; j++) t.rtv_formats[j] = d->RTVFormats[j];

    if (d->InputLayout.pInputElementDescs && d->InputLayout.NumElements > 0) {
        UINT n = d->InputLayout.NumElements;
        if (n > MAX_INPUT_ELEMENTS) n = MAX_INPUT_ELEMENTS;
        t.num_input_elements = n;
        for (UINT k = 0; k < n; k++) {
            const D3D12_INPUT_ELEMENT_DESC *el = &d->InputLayout.pInputElementDescs[k];
            if (el->SemanticName) {
                strncpy(t.input_elements[k].semantic_name, el->SemanticName, sizeof(t.input_elements[k].semantic_name) - 1);
                t.input_elements[k].semantic_name[sizeof(t.input_elements[k].semantic_name) - 1] = '\0';
            } else {
                t.input_elements[k].semantic_name[0] = '\0';
            }
            t.input_elements[k].semantic_index = el->SemanticIndex;
            t.input_elements[k].format = (UINT)el->Format;
            t.input_elements[k].input_slot = el->InputSlot;
            t.input_elements[k].aligned_byte_offset = el->AlignedByteOffset;
            t.input_elements[k].input_slot_class = (UINT)el->InputSlotClass;
            t.input_elements[k].instance_data_step_rate = el->InstanceDataStepRate;
        }
    }

    if (d->VS.pShaderBytecode && d->VS.BytecodeLength > 0) {
        maybe_dump_vs_bytecode(t.vs_hash, d->VS.pShaderBytecode, d->VS.BytecodeLength);
    }
    maybe_dump_outlier_ps(t.vs_hash, &d->PS);
    register_pso(pso, type_name, &t);
}

static void remember_compute_pipeline(ID3D12PipelineState *pso, const D3D12_COMPUTE_PIPELINE_STATE_DESC *d, const char *type_name)
{
    PipelineTrace t;
    if (!pso || !d) return;
    memset(&t, 0, sizeof t);
    t.is_compute = 1;
    t.cs_len = (UINT)d->CS.BytecodeLength;
    t.cs_hash = hash_shader(&d->CS);
    register_pso(pso, type_name, &t);
}

static void remember_pipeline_stream(ID3D12PipelineState *pso, const D3D12_PIPELINE_STATE_STREAM_DESC *stream, const char *type_name)
{
    PipelineTrace t;
    D3D12_SHADER_BYTECODE ps_shader = {0};
    const UINT8 *bytes;
    SIZE_T off = 0;

    if (!pso || !stream) return;
    memset(&t, 0, sizeof t);
    t.sample_count = 1;

    if (stream->pPipelineStateSubobjectStream && stream->SizeInBytes >= sizeof(void *)) {
        bytes = (const UINT8 *)stream->pPipelineStateSubobjectStream;
        while (off + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= stream->SizeInBytes) {
            D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
            SIZE_T subobj_size = 0;
            memcpy(&type, bytes + off, sizeof(type));

            SIZE_T data_off = off + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE);
            SIZE_T data_off_aligned = (data_off + 7) & ~7;

            switch (type) {
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
                subobj_size = data_off_aligned - off + sizeof(ID3D12RootSignature *);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
#ifdef D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
#endif
#ifdef D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
#endif
            {
                if (data_off_aligned + sizeof(D3D12_SHADER_BYTECODE) <= stream->SizeInBytes) {
                    D3D12_SHADER_BYTECODE shader;
                    memcpy(&shader, bytes + data_off_aligned, sizeof(shader));
                    if (shader.pShaderBytecode && shader.BytecodeLength > 0 && shader.BytecodeLength < 8 * 1024 * 1024) {
                        UINT hash = hash_shader(&shader);
                        UINT len = (UINT)shader.BytecodeLength;
                        if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS) { t.vs_len = len; t.vs_hash = hash; maybe_dump_vs_bytecode(hash, shader.pShaderBytecode, shader.BytecodeLength); }
                        else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS) { t.ps_len = len; t.ps_hash = hash; ps_shader = shader; }
                        else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS) { t.ds_len = len; t.ds_hash = hash; }
                        else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS) { t.hs_len = len; t.hs_hash = hash; }
                        else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS) { t.gs_len = len; t.gs_hash = hash; }
                        else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS) { t.cs_len = len; t.cs_hash = hash; t.is_compute = 1; }
                    }
                }
                subobj_size = data_off_aligned - off + sizeof(D3D12_SHADER_BYTECODE);
                break;
            }
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
                subobj_size = data_off_aligned - off + sizeof(D3D12_STREAM_OUTPUT_DESC);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
                subobj_size = data_off - off + sizeof(D3D12_BLEND_DESC);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
                subobj_size = data_off - off + sizeof(UINT);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
                subobj_size = data_off - off + sizeof(D3D12_RASTERIZER_DESC);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
                subobj_size = data_off - off + sizeof(D3D12_DEPTH_STENCIL_DESC);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
            {
                if (data_off_aligned + sizeof(D3D12_INPUT_LAYOUT_DESC) <= stream->SizeInBytes) {
                    D3D12_INPUT_LAYOUT_DESC il;
                    memcpy(&il, bytes + data_off_aligned, sizeof(il));
                    if (il.pInputElementDescs && il.NumElements > 0 && il.NumElements < 128) {
                        UINT n = il.NumElements;
                        if (n > MAX_INPUT_ELEMENTS) n = MAX_INPUT_ELEMENTS;
                        t.num_input_elements = n;
                        for (UINT k = 0; k < n; k++) {
                            const D3D12_INPUT_ELEMENT_DESC *el = &il.pInputElementDescs[k];
                            if (el->SemanticName) {
                                strncpy(t.input_elements[k].semantic_name, el->SemanticName, sizeof(t.input_elements[k].semantic_name) - 1);
                                t.input_elements[k].semantic_name[sizeof(t.input_elements[k].semantic_name) - 1] = '\0';
                            } else {
                                t.input_elements[k].semantic_name[0] = '\0';
                            }
                            t.input_elements[k].semantic_index = el->SemanticIndex;
                            t.input_elements[k].format = (UINT)el->Format;
                            t.input_elements[k].input_slot = el->InputSlot;
                            t.input_elements[k].aligned_byte_offset = el->AlignedByteOffset;
                            t.input_elements[k].input_slot_class = (UINT)el->InputSlotClass;
                            t.input_elements[k].instance_data_step_rate = el->InstanceDataStepRate;
                        }
                    }
                }
                subobj_size = data_off_aligned - off + sizeof(D3D12_INPUT_LAYOUT_DESC);
                break;
            }
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
                subobj_size = data_off - off + sizeof(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
                if (data_off + sizeof(D3D12_PRIMITIVE_TOPOLOGY_TYPE) <= stream->SizeInBytes) {
                    memcpy(&t.topology, bytes + data_off, sizeof(D3D12_PRIMITIVE_TOPOLOGY_TYPE));
                }
                subobj_size = data_off - off + sizeof(D3D12_PRIMITIVE_TOPOLOGY_TYPE);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
                if (data_off + sizeof(struct D3D12_RT_FORMAT_ARRAY) <= stream->SizeInBytes) {
                    struct D3D12_RT_FORMAT_ARRAY rts;
                    memcpy(&rts, bytes + data_off, sizeof(rts));
                    t.num_rtvs = rts.NumRenderTargets;
                    for (UINT j = 0; j < 8; j++) t.rtv_formats[j] = rts.RTFormats[j];
                }
                subobj_size = data_off - off + sizeof(struct D3D12_RT_FORMAT_ARRAY);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
                subobj_size = data_off - off + sizeof(DXGI_FORMAT);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
                if (data_off + sizeof(DXGI_SAMPLE_DESC) <= stream->SizeInBytes) {
                    DXGI_SAMPLE_DESC smp;
                    memcpy(&smp, bytes + data_off, sizeof(smp));
                    t.sample_count = smp.Count;
                }
                subobj_size = data_off - off + sizeof(DXGI_SAMPLE_DESC);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
                subobj_size = data_off - off + sizeof(UINT);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
                subobj_size = data_off_aligned - off + sizeof(D3D12_CACHED_PIPELINE_STATE);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
                subobj_size = data_off - off + sizeof(D3D12_PIPELINE_STATE_FLAGS);
                break;
            default:
                subobj_size = 0;
                break;
            }

            if (!subobj_size) break;
            off += (subobj_size + 7) & ~7;
        }

        if (t.vs_len == 0 && t.ps_len == 0 && t.cs_len == 0) {
            for (off = 0; off + sizeof(D3D12_SHADER_BYTECODE) + 8 <= stream->SizeInBytes; off += 8) {
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
                memcpy(&type, bytes + off, sizeof(type));
                if (type >= D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS && type <= D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS) {
                    D3D12_SHADER_BYTECODE shader;
                    memcpy(&shader, bytes + off + 8, sizeof(shader));
                    if (shader.pShaderBytecode && shader.BytecodeLength > 0 && shader.BytecodeLength < 8 * 1024 * 1024) {
                        UINT hash = hash_shader(&shader);
                        UINT len = (UINT)shader.BytecodeLength;
                        switch (type) {
                        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: if (!t.vs_len) { t.vs_len = len; t.vs_hash = hash; maybe_dump_vs_bytecode(hash, shader.pShaderBytecode, shader.BytecodeLength); } break;
                        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: if (!t.ps_len) { t.ps_len = len; t.ps_hash = hash; ps_shader = shader; } break;
                        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: if (!t.gs_len) { t.gs_len = len; t.gs_hash = hash; } break;
                        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: if (!t.hs_len) { t.hs_len = len; t.hs_hash = hash; } break;
                        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: if (!t.ds_len) { t.ds_len = len; t.ds_hash = hash; } break;
                        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: if (!t.cs_len) { t.cs_len = len; t.cs_hash = hash; t.is_compute = 1; } break;
                        default: break;
                        }
                    }
                }
            }
        }
    }

    maybe_dump_outlier_ps(t.vs_hash, &ps_shader);
    register_pso(pso, type_name, &t);
}

static ListTrace *lookup_list_locked(ID3D12GraphicsCommandList *list)
{
    UINT i;
    IUnknown *unk = NULL;
    UINT free_slot = TRACE_MAX_LISTS;
    if (!list) return NULL;
    unk = get_canonical_unk((IUnknown *)list);
    for (i = 0; i < TRACE_MAX_LISTS; i++) {
        if (list_traces[i].list == list || (unk && list_traces[i].canonical_unk == unk)) {
            return &list_traces[i];
        }
        if (!list_traces[i].list && free_slot == TRACE_MAX_LISTS) free_slot = i;
    }
    if (free_slot < TRACE_MAX_LISTS) {
        list_traces[free_slot].list = list;
        list_traces[free_slot].canonical_unk = unk;
        list_traces[free_slot].pso = NULL;
        list_traces[free_slot].last_signature = NULL;
        list_traces[free_slot].unknown_reported = 0;
        list_traces[free_slot].is_bundle = 0;
        memset(list_traces[free_slot].vb_strides, 0, sizeof(list_traces[free_slot].vb_strides));
        memset(list_traces[free_slot].vb_sizes, 0, sizeof(list_traces[free_slot].vb_sizes));
        list_traces[free_slot].num_rtvs = 0;
        list_traces[free_slot].has_dsv = 0;
        return &list_traces[free_slot];
    }
    return NULL;
}

static BundleTrace *lookup_bundle_locked(ID3D12GraphicsCommandList *bundle)
{
    if (!bundle) return NULL;
    IUnknown *unk = get_canonical_unk((IUnknown *)bundle);
    for (UINT i = 0; i < g_bundle_count; i++) {
        if (g_bundles[i].bundle == bundle || (unk && g_bundles[i].canonical_unk == unk)) {
            return &g_bundles[i];
        }
    }
    if (g_bundle_count < MAX_BUNDLE_TRACK) {
        BundleTrace *b = &g_bundles[g_bundle_count++];
        memset(b, 0, sizeof *b);
        b->bundle = bundle;
        b->canonical_unk = unk;
        return b;
    }
    return NULL;
}

static void set_list_pipeline(ID3D12GraphicsCommandList *list, ID3D12PipelineState *pso)
{
    if (!list) return;
    EnterCriticalSection(&trace_lock);
    ListTrace *lt = lookup_list_locked(list);
    if (lt) lt->pso = pso;
    LeaveCriticalSection(&trace_lock);
}

static ID3D12PipelineState *current_pipeline(ID3D12GraphicsCommandList *list)
{
    ID3D12PipelineState *pso = NULL;
    UINT i;
    IUnknown *unk = NULL;
    if (!list) return NULL;
    unk = get_canonical_unk((IUnknown *)list);
    EnterCriticalSection(&trace_lock);
    for (i = 0; i < TRACE_MAX_LISTS; i++) {
        if (list_traces[i].list == list || (unk && list_traces[i].canonical_unk == unk)) {
            pso = list_traces[i].pso;
            break;
        }
    }
    LeaveCriticalSection(&trace_lock);
    return pso;
}

static void remember_signature(ID3D12CommandSignature *signature, const D3D12_COMMAND_SIGNATURE_DESC *d)
{
    UINT i, j;
    IUnknown *unk = NULL;
    if (!signature || !d) return;
    unk = get_canonical_unk((IUnknown *)signature);
    EnterCriticalSection(&trace_lock);
    for (i = 0; i < signature_trace_count; i++) {
        if (signature_traces[i].signature == signature || (unk && signature_traces[i].canonical_unk == unk))
            break;
    }
    if (i == signature_trace_count && signature_trace_count < TRACE_MAX_SIGNATURES) signature_trace_count++;
    if (i < TRACE_MAX_SIGNATURES) {
        SignatureTrace *t = &signature_traces[i];
        memset(t, 0, sizeof *t);
        t->signature = signature;
        t->canonical_unk = unk;
        t->byte_stride = d->ByteStride;
        t->num_args = d->NumArgumentDescs;
        for (j = 0; j < d->NumArgumentDescs && j < 16; j++)
            t->arg_types[j] = (UINT)d->pArgumentDescs[j].Type;
    }
    LeaveCriticalSection(&trace_lock);
    if (dbg) {
        char args[128] = {0}; size_t used = 0;
        for (j = 0; j < d->NumArgumentDescs && j < 16; j++) {
            int n = snprintf(args + used, sizeof(args) - used, "%s%s(%u)",
                             j ? "," : "", arg_type_name(d->pArgumentDescs[j].Type),
                             (unsigned)d->pArgumentDescs[j].Type);
            if (n < 0 || (size_t)n >= sizeof(args) - used) break;
            used += (size_t)n;
        }
        LOG("CreateCommandSignature sig=%p stride=%u args=%u types=[%s]", signature, d->ByteStride, d->NumArgumentDescs, args);
    }
}

static SignatureTrace *lookup_signature(ID3D12CommandSignature *signature)
{
    UINT i;
    IUnknown *unk = NULL;
    if (!signature) return NULL;
    for (i = 0; i < signature_trace_count; i++) {
        if (signature_traces[i].signature == signature) return &signature_traces[i];
    }
    unk = get_canonical_unk((IUnknown *)signature);
    if (unk) {
        for (i = 0; i < signature_trace_count; i++) {
            if (signature_traces[i].canonical_unk == unk) {
                signature_traces[i].signature = signature;
                return &signature_traces[i];
            }
        }
    }
    return NULL;
}

static void report_indirect_context(ID3D12GraphicsCommandList *list, ID3D12CommandSignature *signature)
{
    UINT j;
    PipelineTrace *p = NULL;
    SignatureTrace *s = NULL;
    ListTrace *lt = NULL;
    ID3D12PipelineState *bound = current_pipeline(list);
    if (!trace_indirect_pso) return;

    EnterCriticalSection(&trace_lock);
    p = lookup_pipeline_locked(bound, get_canonical_unk((IUnknown *)bound));
    s = lookup_signature(signature);

    IUnknown *list_unk = get_canonical_unk((IUnknown *)list);
    for (UINT i = 0; i < TRACE_MAX_LISTS; i++) {
        if (list_traces[i].list == list || (list_unk && list_traces[i].canonical_unk == list_unk)) {
            lt = &list_traces[i];
            break;
        }
    }

    if (!bound || trace_indirect_context_count >= 1024) {
        LeaveCriticalSection(&trace_lock);
        return;
    }

    if (p) {
        if (p->reported) { LeaveCriticalSection(&trace_lock); return; }
        p->reported = 1;
    } else {
        if (!lt || (lt->unknown_reported && lt->last_signature == signature)) {
            LeaveCriticalSection(&trace_lock);
            return;
        }
        lt->unknown_reported = 1; lt->last_signature = signature;
    }
    trace_indirect_context_count++;

    if (dbg) {
        char args[128] = {0}; size_t used = 0;
        if (s) {
            for (j = 0; j < s->num_args && j < 16; j++) {
                int n = snprintf(args + used, sizeof(args) - used, "%s%s(%u)",
                                 j ? "," : "", arg_type_name(s->arg_types[j]), s->arg_types[j]);
                if (n < 0 || (size_t)n >= sizeof(args) - used) break;
                used += (size_t)n;
            }
        } else {
            snprintf(args, sizeof(args), "unknown");
        }

        if (p) {
            UINT ph = pso_hash(p);
            if (p->is_compute) {
                LOG("INDIRECT_CONTEXT list=%p sig=%p stride=%u args=[%s] pso=%p(idx=%u, type=%s, pso_hash=0x%08x) CS=%u/%08x",
                    list, signature, s ? s->byte_stride : 0, args, bound,
                    p->creation_index, p->creation_type ? p->creation_type : "unknown", ph,
                    p->cs_len, p->cs_hash);
            } else {
                LOG("INDIRECT_CONTEXT list=%p sig=%p stride=%u args=[%s] pso=%p(idx=%u, type=%s, pso_hash=0x%08x) VS=%u/%08x PS=%u/%08x GS=%u/%08x HS=%u/%08x DS=%u/%08x topology=%u rtvs=%u samples=%u",
                    list, signature, s ? s->byte_stride : 0, args, bound,
                    p->creation_index, p->creation_type ? p->creation_type : "unknown", ph,
                    p->vs_len, p->vs_hash, p->ps_len, p->ps_hash, p->gs_len, p->gs_hash,
                    p->hs_len, p->hs_hash, p->ds_len, p->ds_hash, p->topology, p->num_rtvs, p->sample_count);
            }
        } else {
            LOG("INDIRECT_CONTEXT list=%p sig=%p stride=%u args=[%s] pso=%p (unk=%p, total_registered=%u) metadata=unregistered",
                list, signature, s ? s->byte_stride : 0, args, bound, get_canonical_unk((IUnknown *)bound), pipeline_hash_count);
        }
    }
    LeaveCriticalSection(&trace_lock);
}

// ---- Step A: Draw Census Helpers -----------------------------------------------------------

static DrawCensusEntry *census_get_or_add_entry(ID3D12PipelineState *pso)
{
    if (!pso) return NULL;
    for (UINT i = 0; i < g_census_entry_count; i++) {
        if (g_census_entries[i].pso == pso) return &g_census_entries[i];
    }
    if (g_census_entry_count < MAX_CENSUS_PSOS) {
        DrawCensusEntry *e = &g_census_entries[g_census_entry_count++];
        memset(e, 0, sizeof *e);
        e->pso = pso;
        PipelineTrace *p = lookup_pipeline(pso);
        if (p) {
            e->creation_index = p->creation_index;
            e->vs_hash = p->vs_hash;
            e->ps_hash = p->ps_hash;
            e->topology = p->topology;
            e->type_name = p->creation_type;
        }
        return e;
    }
    return NULL;
}

static void census_record_draw(ID3D12GraphicsCommandList *list, UINT vcount, UINT icount)
{
    if (!g_census_active) return;
    EnterCriticalSection(&trace_lock);
    ListTrace *lt = lookup_list_locked(list);
    ID3D12PipelineState *pso = lt ? lt->pso : NULL;
    DrawCensusEntry *e = census_get_or_add_entry(pso);
    if (e) {
        e->draw_count++;
        e->instance_count_sum += icount;
        if (icount > e->instance_count_max) e->instance_count_max = icount;
        e->vertex_index_count_sum += (UINT64)vcount * (icount ? icount : 1);
        if (icount > 1 && lt && !e->vb_strides_captured) {
            memcpy(e->vb_strides, lt->vb_strides, sizeof(e->vb_strides));
            e->vb_strides_captured = 1;
        }
    }
    LeaveCriticalSection(&trace_lock);
}

static void census_record_draw_indexed(ID3D12GraphicsCommandList *list, UINT idx_count, UINT icount)
{
    if (!g_census_active) return;
    EnterCriticalSection(&trace_lock);
    ListTrace *lt = lookup_list_locked(list);
    ID3D12PipelineState *pso = lt ? lt->pso : NULL;
    DrawCensusEntry *e = census_get_or_add_entry(pso);
    if (e) {
        e->draw_indexed_count++;
        e->instance_count_sum += icount;
        if (icount > e->instance_count_max) e->instance_count_max = icount;
        e->vertex_index_count_sum += (UINT64)idx_count * (icount ? icount : 1);
        if (icount > 1 && lt && !e->vb_strides_captured) {
            memcpy(e->vb_strides, lt->vb_strides, sizeof(e->vb_strides));
            e->vb_strides_captured = 1;
        }
    }
    LeaveCriticalSection(&trace_lock);
}

static void census_record_draw_with_pso(ID3D12PipelineState *pso, UINT vcount, UINT icount, const UINT *vb_strides)
{
    if (!g_census_active) return;
    DrawCensusEntry *e = census_get_or_add_entry(pso);
    if (e) {
        e->draw_count++;
        e->instance_count_sum += icount;
        if (icount > e->instance_count_max) e->instance_count_max = icount;
        e->vertex_index_count_sum += (UINT64)vcount * (icount ? icount : 1);
        if (icount > 1 && vb_strides && !e->vb_strides_captured) {
            memcpy(e->vb_strides, vb_strides, sizeof(e->vb_strides));
            e->vb_strides_captured = 1;
        }
    }
}

static void census_record_draw_indexed_with_pso(ID3D12PipelineState *pso, UINT idx_count, UINT icount, const UINT *vb_strides)
{
    if (!g_census_active) return;
    DrawCensusEntry *e = census_get_or_add_entry(pso);
    if (e) {
        e->draw_indexed_count++;
        e->instance_count_sum += icount;
        if (icount > e->instance_count_max) e->instance_count_max = icount;
        e->vertex_index_count_sum += (UINT64)idx_count * (icount ? icount : 1);
        if (icount > 1 && vb_strides && !e->vb_strides_captured) {
            memcpy(e->vb_strides, vb_strides, sizeof(e->vb_strides));
            e->vb_strides_captured = 1;
        }
    }
}

static void census_record_indirect(ID3D12GraphicsCommandList *list, UINT max_count)
{
    if (!g_census_active) return;
    ID3D12PipelineState *pso = current_pipeline(list);
    EnterCriticalSection(&trace_lock);
    DrawCensusEntry *e = census_get_or_add_entry(pso);
    if (e) {
        e->indirect_count++;
        // Indirect draws carry GPU-driven instances, default 1
        e->instance_count_sum += max_count;
        if (max_count > e->instance_count_max) e->instance_count_max = max_count;
    }
    LeaveCriticalSection(&trace_lock);
}

static void census_record_dispatch(ID3D12GraphicsCommandList *list, UINT x, UINT y, UINT z)
{
    if (!g_census_active) return;
    ID3D12PipelineState *pso = current_pipeline(list);
    EnterCriticalSection(&trace_lock);
    DrawCensusEntry *e = census_get_or_add_entry(pso);
    if (e) {
        e->dispatch_count++;
        e->thread_groups_sum += (UINT64)x * y * z;
    }
    LeaveCriticalSection(&trace_lock);
}

static void dump_census_table(void)
{
    EnterCriticalSection(&trace_lock);
    if (!g_census_entry_count) {
        LOG("================================ DRAW CENSUS (Frame %u) ================================", g_world_frame_index);
        LOG("Total PSOs Active in Frame: 0 (No draw calls recorded in this frame)");
        LOG("Bundles executed in frame: %u %s", g_bundles_executed_in_frame,
            g_bundles_executed_in_frame == 0 ? "(bundles not used)" : "");
        LOG("=========================================================================================");
        LeaveCriticalSection(&trace_lock);
        return;
    }
    // Sort descending by instance_count_max, then instance_count_sum
    for (UINT i = 0; i < g_census_entry_count - 1; i++) {
        for (UINT j = i + 1; j < g_census_entry_count; j++) {
            if (g_census_entries[j].instance_count_max > g_census_entries[i].instance_count_max ||
                (g_census_entries[j].instance_count_max == g_census_entries[i].instance_count_max &&
                 g_census_entries[j].instance_count_sum > g_census_entries[i].instance_count_sum)) {
                DrawCensusEntry tmp = g_census_entries[i];
                g_census_entries[i] = g_census_entries[j];
                g_census_entries[j] = tmp;
            }
        }
    }

    LOG("================================ DRAW CENSUS (Frame %u after world load) ================================", g_world_frame_index);
    LOG("Total PSOs Active in Frame: %u", g_census_entry_count);
    LOG("Rank | PSO Index | Type       | VS Hash  | PS Hash  | Topo | Max Inst | Total Inst | Draws (Non-Idx/Idx/Indir) | Verts/Indices");
    LOG("-------------------------------------------------------------------------------------------------------------------------");
    for (UINT i = 0; i < g_census_entry_count; i++) {
        DrawCensusEntry *e = &g_census_entries[i];
        if (e->dispatch_count > 0 && !e->draw_count && !e->draw_indexed_count && !e->indirect_count) {
            LOG("#%-3u | PSO(%5u) | COMPUTE    | -------- | -------- | ---- | -------- | ---------- | Dispatches: %-5u            | Groups: %llu",
                i + 1, e->creation_index, e->dispatch_count, (unsigned long long)e->thread_groups_sum);
        } else {
            LOG("#%-3u | PSO(%5u) | %-10s | %08x | %08x | %4u | %8u | %10llu | %5u / %5u / %5u       | %llu",
                i + 1, e->creation_index, e->type_name ? e->type_name : "graphics",
                e->vs_hash, e->ps_hash, e->topology, e->instance_count_max,
                (unsigned long long)e->instance_count_sum, e->draw_count,
                e->draw_indexed_count, e->indirect_count,
                (unsigned long long)e->vertex_index_count_sum);
        }
    }
    LOG("=========================================================================================");

    // Step 4: Input layouts and bound VBV strides for all PSOs with Max InstanceCount > 1
    LOG("================ INPUT LAYOUTS & STRIDES FOR INSTANCED PSOS (Max Inst > 1) ================");
    UINT instanced_pso_count = 0;
    for (UINT i = 0; i < g_census_entry_count; i++) {
        DrawCensusEntry *e = &g_census_entries[i];
        if (e->instance_count_max <= 1) continue;
        instanced_pso_count++;
        PipelineTrace *p = lookup_pipeline(e->pso);
        LOG("--- PSO(%5u) [Rank #%u, VS=%08x, PS=%08x, Topo=%u, MaxInst=%u, Draws=%u] ---",
            e->creation_index, i + 1, e->vs_hash, e->ps_hash, e->topology, e->instance_count_max,
            e->draw_count + e->draw_indexed_count + e->indirect_count);
        if (p && p->num_input_elements > 0) {
            LOG("  Input Layout Elements (%u):", p->num_input_elements);
            for (UINT k = 0; k < p->num_input_elements; k++) {
                InputElementTrace *el = &p->input_elements[k];
                LOG("    [Elem %2u] Semantic=%s Index=%u Format=%u Slot=%u Offset=%u Class=%u StepRate=%u",
                    k, el->semantic_name, el->semantic_index, el->format, el->input_slot,
                    el->aligned_byte_offset, el->input_slot_class, el->instance_data_step_rate);
            }
        } else {
            LOG("  Input Layout Elements: (none / not specified)");
        }
        if (e->vb_strides_captured) {
            char vb_buf[256] = {0};
            int vb_pos = 0;
            for (UINT s = 0; s < 8; s++) {
                if (e->vb_strides[s] > 0) {
                    vb_pos += snprintf(vb_buf + vb_pos, sizeof(vb_buf) - vb_pos, "Slot%u: %uB  ", s, e->vb_strides[s]);
                }
            }
            LOG("  Bound VBV Strides at Draw: %s", vb_pos > 0 ? vb_buf : "(none > 0)");
        } else {
            LOG("  Bound VBV Strides at Draw: (not captured)");
        }
    }
    if (!instanced_pso_count) {
        LOG("No PSOs with Max InstanceCount > 1 were active during this census frame.");
    }
    LOG("=========================================================================================");
    LOG("================ INPUT LAYOUTS & STRIDES FOR TOP NON-INSTANCED PSOS ================");
    UINT non_instanced_pso_count = 0;
    for (UINT i = 0; i < g_census_entry_count && non_instanced_pso_count < 15; i++) {
        DrawCensusEntry *e = &g_census_entries[i];
        if (e->instance_count_max <= 1 && (e->draw_count > 0 || e->draw_indexed_count > 0)) {
            non_instanced_pso_count++;
            PipelineTrace *p = lookup_pipeline(e->pso);
            LOG("--- PSO(%5u) [Rank #%u, VS=%08x, PS=%08x, Topo=%u, Draws=%u] ---",
                e->creation_index, i + 1, e->vs_hash, e->ps_hash, e->topology,
                e->draw_count + e->draw_indexed_count + e->indirect_count);
            if (p && p->num_input_elements > 0) {
                LOG("  Input Layout Elements (%u):", p->num_input_elements);
                for (UINT k = 0; k < p->num_input_elements; k++) {
                    InputElementTrace *el = &p->input_elements[k];
                    LOG("    [Elem %2u] Semantic=%s Index=%u Format=%u Slot=%u Offset=%u Class=%u StepRate=%u",
                        k, el->semantic_name, el->semantic_index, el->format, el->input_slot,
                        el->aligned_byte_offset, el->input_slot_class, el->instance_data_step_rate);
                }
            } else {
                LOG("  Input Layout Elements: (none / not specified)");
            }
            if (e->vb_strides_captured) {
                char vb_buf[256] = {0};
                int vb_pos = 0;
                for (UINT s = 0; s < 8; s++) {
                    if (e->vb_strides[s] > 0) {
                        vb_pos += snprintf(vb_buf + vb_pos, sizeof(vb_buf) - vb_pos, "Slot%u: %uB  ", s, e->vb_strides[s]);
                    }
                }
                LOG("  Bound VBV Strides at Draw: %s", vb_pos > 0 ? vb_buf : "(none > 0)");
            } else {
                LOG("  Bound VBV Strides at Draw: (not captured)");
            }
        }
    }
    LOG("================ STEP 1: INPUT LAYOUTS FOR TARGET PSOS ================");
    for (UINT i = 0; i < g_census_entry_count; i++) {
        DrawCensusEntry *e = &g_census_entries[i];
        if (is_step1_target_pso(e->creation_index, e->vs_hash)) {
            PipelineTrace *p = lookup_pipeline(e->pso);
            if (p) dump_target_pso_input_layout(p, "Census");
        }
    }
    LOG("=======================================================================");
    LOG("=========================================================================================");
    LOG("Bundles executed in frame: %u %s", g_bundles_executed_in_frame,
        g_bundles_executed_in_frame == 0 ? "(bundles not used)" : "");
    LOG("=========================================================================================");
    LeaveCriticalSection(&trace_lock);
}

// Step 1 & 3: Hot-Reload Skip Rules & Ordinal Bisection
typedef enum SkipRuleType {
    SKIP_RULE_NONE = 0,
    SKIP_RULE_VS_HASH,
    SKIP_RULE_PSO_INDEX,
    SKIP_RULE_CS_HASH,
    SKIP_RULE_ORDINAL_RANGE,
} SkipRuleType;

typedef struct SkipRule {
    SkipRuleType type;
    UINT vs_hash;
    UINT pso_index;
    UINT cs_hash;
    UINT ord_from;
    UINT ord_to;
    char text[64];
} SkipRule;

#define MAX_HOT_SKIP_RULES 128
static SkipRule g_hot_skip_rules[MAX_HOT_SKIP_RULES];
static UINT g_num_hot_skip_rules = 0;
static int g_hot_skip_enabled = 0;
static int g_hot_skip_initialized = 0;
static UINT g_hot_skip_frame_counter = 0;

// Ordinal counter per frame
static volatile LONG g_frame_draw_ordinal = 0;

// Per-draw census trace file
static FILE *g_census_draws_file = NULL;

typedef struct DispatchRecord {
    UINT pso_index;
    UINT cs_hash;
    UINT x, y, z;
} DispatchRecord;

#define MAX_PENDING_DISPATCHES 64
static DispatchRecord g_pending_dispatches[MAX_PENDING_DISPATCHES];
static UINT g_num_pending_dispatches = 0;

// Legacy / fallback skip counters
#define MAX_SKIP_HASHES 32

typedef struct SkipCounters {
    UINT64 draws_indexed;
    UINT64 draws_instanced;
    UINT64 draws_indirect;
} SkipCounters;

static SkipCounters g_skip_counters[MAX_SKIP_HASHES];
static UINT64 g_total_skips_indexed = 0;
static UINT64 g_total_skips_instanced = 0;
static UINT64 g_total_skips_indirect = 0;
static UINT64 g_frame_skips_count = 0;

static int g_skip_vs_log_enabled = 0;
static int g_skip_vs_log_count = 0;

static int skip_all_instanced = 0;
static UINT64 g_skip_all_instanced_indexed_count = 0;
static UINT64 g_skip_all_instanced_non_indexed_count = 0;

static inline int get_skip_hash_index(ID3D12PipelineState *pso)
{
    if (!g_num_skip_vs || !pso) return -1;
    PipelineTrace *p = lookup_pipeline(pso);
    if (!p) return -1;
    for (UINT i = 0; i < g_num_skip_vs; i++) {
        if (p->vs_hash == g_skip_vs_hashes[i]) return (int)i;
    }
    return -1;
}

static inline int should_skip_pso(ID3D12PipelineState *pso)
{
    return get_skip_hash_index(pso) >= 0;
}

// Step 1: Hot-reload C:\highball\skip.txt every ~30 frames
static void hot_skip_reload(void)
{
    if (!g_hot_skip_enabled) return;

    FILE *f = fopen("C:\\highball\\skip.txt", "r");
    SkipRule new_rules[MAX_HOT_SKIP_RULES];
    UINT new_count = 0;

    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f) && new_count < MAX_HOT_SKIP_RULES) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0' || *p == '#' || *p == '\r' || *p == '\n') continue;
            // Trim trailing \r, \n, whitespace
            char *end = p + strlen(p) - 1;
            while (end > p && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
                *end = '\0';
                end--;
            }
            if (*p == '\0') continue;

            SkipRule *r = &new_rules[new_count];
            memset(r, 0, sizeof(*r));
            snprintf(r->text, sizeof(r->text), "%s", p);

            if (strncmp(p, "ord", 3) == 0 && (p[3] == ' ' || p[3] == '\t')) {
                char *args = p + 4;
                while (*args == ' ' || *args == '\t') args++;
                UINT f_ord = 0, t_ord = 0;
                char *dash = strchr(args, '-');
                if (dash) {
                    f_ord = (UINT)strtoul(args, NULL, 10);
                    t_ord = (UINT)strtoul(dash + 1, NULL, 10);
                } else {
                    f_ord = (UINT)strtoul(args, NULL, 10);
                    t_ord = f_ord;
                }
                r->type = SKIP_RULE_ORDINAL_RANGE;
                r->ord_from = f_ord;
                r->ord_to = t_ord;
                new_count++;
            } else if (strncmp(p, "pso", 3) == 0 && (p[3] == ' ' || p[3] == '\t')) {
                char *args = p + 4;
                while (*args == ' ' || *args == '\t') args++;
                r->type = SKIP_RULE_PSO_INDEX;
                r->pso_index = (UINT)strtoul(args, NULL, 0);
                new_count++;
            } else if (strncmp(p, "cs", 2) == 0 && (p[2] == ' ' || p[2] == '\t')) {
                char *args = p + 3;
                while (*args == ' ' || *args == '\t') args++;
                r->type = SKIP_RULE_CS_HASH;
                r->cs_hash = (UINT)strtoul(args, NULL, 16);
                new_count++;
            } else if (strncmp(p, "vs", 2) == 0 && (p[2] == ' ' || p[2] == '\t')) {
                char *args = p + 3;
                while (*args == ' ' || *args == '\t') args++;
                r->type = SKIP_RULE_VS_HASH;
                r->vs_hash = (UINT)strtoul(args, NULL, 16);
                new_count++;
            } else {
                r->type = SKIP_RULE_VS_HASH;
                r->vs_hash = (UINT)strtoul(p, NULL, 16);
                new_count++;
            }
        }
        fclose(f);
    }

    int changed = 0;
    if (!g_hot_skip_initialized || new_count != g_num_hot_skip_rules) {
        changed = 1;
    } else {
        for (UINT i = 0; i < new_count; i++) {
            if (new_rules[i].type != g_hot_skip_rules[i].type ||
                new_rules[i].vs_hash != g_hot_skip_rules[i].vs_hash ||
                new_rules[i].pso_index != g_hot_skip_rules[i].pso_index ||
                new_rules[i].cs_hash != g_hot_skip_rules[i].cs_hash ||
                new_rules[i].ord_from != g_hot_skip_rules[i].ord_from ||
                new_rules[i].ord_to != g_hot_skip_rules[i].ord_to) {
                changed = 1;
                break;
            }
        }
    }

    if (changed) {
        g_num_hot_skip_rules = new_count;
        if (new_count > 0) {
            memcpy(g_hot_skip_rules, new_rules, sizeof(new_rules));
        }
        g_hot_skip_initialized = 1;

        if (g_num_hot_skip_rules == 0) {
            LOG("HB_TSSHIM_HOT_SKIP: Active list updated: 0 rules (C:\\highball\\skip.txt missing or empty - skipping nothing)");
        } else {
            LOG("================ HB_TSSHIM_HOT_SKIP: Active list updated (%u rules) ================", g_num_hot_skip_rules);
            for (UINT i = 0; i < g_num_hot_skip_rules; i++) {
                SkipRule *r = &g_hot_skip_rules[i];
                if (r->type == SKIP_RULE_VS_HASH) {
                    LOG("  [%2u] VS hash    : %08x", i + 1, r->vs_hash);
                } else if (r->type == SKIP_RULE_PSO_INDEX) {
                    LOG("  [%2u] PSO index  : %u", i + 1, r->pso_index);
                } else if (r->type == SKIP_RULE_CS_HASH) {
                    LOG("  [%2u] CS hash    : %08x", i + 1, r->cs_hash);
                } else if (r->type == SKIP_RULE_ORDINAL_RANGE) {
                    LOG("  [%2u] Draw Ord   : %u - %u", i + 1, r->ord_from, r->ord_to);
                }
            }
            LOG("==================================================================================");
        }
    }
}

// Step 2: Sequential per-draw census recorder
#include "binding_capture.h"

static void write_census_per_draw(const char *draw_type, ID3D12GraphicsCommandList *list, ID3D12PipelineState *pso, UINT count, UINT icount, UINT ord, UINT p1, INT p2, UINT p3)
{
    if (!g_census_active || !g_census_draws_file) return;

    EnterCriticalSection(&trace_lock);
    if (g_census_draws_file) {
        if (g_num_pending_dispatches > 0) {
            for (UINT d = 0; d < g_num_pending_dispatches; d++) {
                fprintf(g_census_draws_file, "# DISPATCH: pso %u | cs %08x | groups (%u, %u, %u)\n",
                    g_pending_dispatches[d].pso_index, g_pending_dispatches[d].cs_hash,
                    g_pending_dispatches[d].x, g_pending_dispatches[d].y, g_pending_dispatches[d].z);
            }
            LOG("CENSUS: Before draw ord %u: %u compute dispatches executed", ord, g_num_pending_dispatches);
            g_num_pending_dispatches = 0;
        }

        PipelineTrace *p = lookup_pipeline(pso);
        ListTrace *lt = lookup_list_locked(list);

        char strides_buf[128] = "[]";
        char sizes_buf[128] = "[]";
        if (lt) {
            int pos = 0;
            pos += snprintf(strides_buf + pos, sizeof(strides_buf) - pos, "[");
            for (UINT s = 0; s < 16; s++) {
                if (lt->vb_strides[s] > 0) {
                    pos += snprintf(strides_buf + pos, sizeof(strides_buf) - pos, "s%u:%u ", s, lt->vb_strides[s]);
                }
            }
            if (pos > 1 && strides_buf[pos - 1] == ' ') pos--;
            snprintf(strides_buf + pos, sizeof(strides_buf) - pos, "]");

            pos = 0;
            pos += snprintf(sizes_buf + pos, sizeof(sizes_buf) - pos, "[");
            for (UINT s = 0; s < 16; s++) {
                if (lt->vb_sizes[s] > 0) {
                    pos += snprintf(sizes_buf + pos, sizeof(sizes_buf) - pos, "s%u:%u ", s, lt->vb_sizes[s]);
                }
            }
            if (pos > 1 && sizes_buf[pos - 1] == ' ') pos--;
            snprintf(sizes_buf + pos, sizeof(sizes_buf) - pos, "]");
        }

        UINT num_rtvs = lt ? lt->num_rtvs : 0;
        int has_dsv = lt ? lt->has_dsv : 0;
        UINT pso_idx = p ? p->creation_index : 0;
        UINT vs_hash = p ? p->vs_hash : 0;
        UINT ps_hash = p ? p->ps_hash : 0;

        if (strcmp(draw_type, "DrawIndexed") == 0) {
            fprintf(g_census_draws_file, "ord %5u | %-12s | pso %5u | vs %08x | ps %08x | count %6u | inst %4u | start_idx %6u | base_vtx %6d | start_inst %4u | rtvs %u | dsv %d | vb_strides %s | vb_sizes %s\n",
                ord, draw_type, pso_idx, vs_hash, ps_hash, count, icount, p1, p2, p3, num_rtvs, has_dsv, strides_buf, sizes_buf);
        } else if (strcmp(draw_type, "Draw") == 0) {
            fprintf(g_census_draws_file, "ord %5u | %-12s | pso %5u | vs %08x | ps %08x | count %6u | inst %4u | start_vtx %6u | start_inst %4u | rtvs %u | dsv %d | vb_strides %s | vb_sizes %s\n",
                ord, draw_type, pso_idx, vs_hash, ps_hash, count, icount, p1, p3, num_rtvs, has_dsv, strides_buf, sizes_buf);
        } else {
            fprintf(g_census_draws_file, "ord %5u | %-12s | pso %5u | vs %08x | ps %08x | count %6u | inst %4u | rtvs %u | dsv %d | vb_strides %s | vb_sizes %s\n",
                ord, draw_type, pso_idx, vs_hash, ps_hash, count, icount, num_rtvs, has_dsv, strides_buf, sizes_buf);
        }
        if (g_capture_bindings && lt && is_binding_target(vs_hash)) {
            write_binding_snapshot(g_census_draws_file, lt, ord);
        }
    }
    LeaveCriticalSection(&trace_lock);
}

// Draw skip evaluator
static inline int should_skip_graphics_draw(ID3D12PipelineState *pso, UINT ord, const char *draw_type, UINT icount)
{
    if (skip_all_instanced && icount > 1) {
        if (strcmp(draw_type, "DrawIndexed") == 0) g_skip_all_instanced_indexed_count++;
        else g_skip_all_instanced_non_indexed_count++;
        return 1;
    }

    if (g_hot_skip_enabled) {
        if (!g_num_hot_skip_rules) return 0;
        PipelineTrace *p = lookup_pipeline(pso);
        for (UINT i = 0; i < g_num_hot_skip_rules; i++) {
            SkipRule *r = &g_hot_skip_rules[i];
            int match = 0;
            if (r->type == SKIP_RULE_ORDINAL_RANGE) {
                if (ord >= r->ord_from && ord <= r->ord_to) match = 1;
            } else if (r->type == SKIP_RULE_PSO_INDEX) {
                if (p && p->creation_index == r->pso_index) match = 1;
            } else if (r->type == SKIP_RULE_VS_HASH) {
                if (p && p->vs_hash == r->vs_hash) match = 1;
            }
            if (match) {
                g_total_skips_indexed++;
                g_frame_skips_count++;
                if (g_skip_vs_log_enabled && g_skip_vs_log_count < 20) {
                    g_skip_vs_log_count++;
                    LOG("HB_TSSHIM_HOT_SKIP[%d]: %s ord=%u pso=%p (idx=%u) VS=%08x (matched rule: %s)",
                        g_skip_vs_log_count, draw_type, ord, (void *)pso, p ? p->creation_index : 0, p ? p->vs_hash : 0, r->text);
                }
                return 1;
            }
        }
        return 0;
    } else {
        int skip_idx = get_skip_hash_index(pso);
        if (skip_idx >= 0) {
            if (strcmp(draw_type, "DrawIndexed") == 0) g_skip_counters[skip_idx].draws_indexed++;
            else if (strcmp(draw_type, "Draw") == 0) g_skip_counters[skip_idx].draws_instanced++;
            else g_skip_counters[skip_idx].draws_indirect++;
            g_total_skips_indexed++;
            g_frame_skips_count++;
            if (g_skip_vs_log_enabled && g_skip_vs_log_count < 20) {
                g_skip_vs_log_count++;
                PipelineTrace *p = lookup_pipeline(pso);
                LOG("HB_TSSHIM_SKIP_VS_LOG[%d]: %s pso=%p (idx=%u) VS=%08x (match: %08x)",
                    g_skip_vs_log_count, draw_type, (void *)pso, p ? p->creation_index : 0, p ? p->vs_hash : 0, g_skip_vs_hashes[skip_idx]);
            }
            return 1;
        }
        return 0;
    }
}

static inline int should_skip_compute_dispatch(ID3D12PipelineState *pso, UINT x, UINT y, UINT z)
{
    if (!g_hot_skip_enabled || !g_num_hot_skip_rules || !pso) return 0;
    PipelineTrace *p = lookup_pipeline(pso);
    for (UINT i = 0; i < g_num_hot_skip_rules; i++) {
        SkipRule *r = &g_hot_skip_rules[i];
        int match = 0;
        if (r->type == SKIP_RULE_CS_HASH) {
            if (p && p->cs_hash == r->cs_hash) match = 1;
        } else if (r->type == SKIP_RULE_PSO_INDEX) {
            if (p && p->creation_index == r->pso_index) match = 1;
        }
        if (match) {
            g_frame_skips_count++;
            if (g_skip_vs_log_enabled && g_skip_vs_log_count < 20) {
                g_skip_vs_log_count++;
                LOG("HB_TSSHIM_HOT_SKIP[%d]: Dispatch (%ux%ux%u) pso=%p (idx=%u) CS=%08x (matched rule: %s)",
                    g_skip_vs_log_count, x, y, z, (void *)pso, p ? p->creation_index : 0, p ? p->cs_hash : 0, r->text);
            }
            return 1;
        }
    }
    return 0;
}


#include "input_offsets.h"

// ---- Forward Declarations & Hooks -----------------------------------------------------------

static void patch_list_vtbl(void *iface);

static HRESULT (STDMETHODCALLTYPE *real_CreateCommandSignature)(ID3D12Device *, const D3D12_COMMAND_SIGNATURE_DESC *, ID3D12RootSignature *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE hook_CreateCommandSignature(ID3D12Device *dev, const D3D12_COMMAND_SIGNATURE_DESC *desc, ID3D12RootSignature *root, REFIID riid, void **out)
{
    HRESULT hr = real_CreateCommandSignature(dev, desc, root, riid, out);
    if (SUCCEEDED(hr) && out && *out) remember_signature((ID3D12CommandSignature *)*out, desc);
    return hr;
}

static HRESULT (STDMETHODCALLTYPE *real_CreatePipelineStateStream)(ID3D12Device2 *, const D3D12_PIPELINE_STATE_STREAM_DESC *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE hook_CreatePipelineStateStream(ID3D12Device2 *dev, const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID riid, void **out)
{
    D3D12_INPUT_ELEMENT_DESC elements[INPUT_OFFSET_LIMIT];
    D3D12_PIPELINE_STATE_STREAM_DESC copy;
    const D3D12_PIPELINE_STATE_STREAM_DESC *use = desc;
    void *owned;
    if (explicit_input_stream(desc, elements, &copy, &owned)) {
        use = &copy;
        LOG("INPUT_OFFSETS resolved stream layout");
    }
    HRESULT hr = real_CreatePipelineStateStream(dev, use, riid, out);
    if (SUCCEEDED(hr) && out && *out) remember_pipeline_stream((ID3D12PipelineState *)*out, use, "stream");
    free(owned);
    return hr;
}

static void patch_device2_vtbl(void *iface)
{
    ID3D12Device2Vtbl *vt = *(ID3D12Device2Vtbl **)iface;
    patch_slot((void **)&vt->CreatePipelineState, (void *)hook_CreatePipelineStateStream, (void **)&real_CreatePipelineStateStream);
}

// Pipeline Library Interception
static HRESULT (STDMETHODCALLTYPE *real_CreatePipelineLibrary)(ID3D12Device1 *, const void *, SIZE_T, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_LoadGraphicsPipeline)(ID3D12PipelineLibrary *, const WCHAR *, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_LoadComputePipeline)(ID3D12PipelineLibrary *, const WCHAR *, const D3D12_COMPUTE_PIPELINE_STATE_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_LoadPipeline)(ID3D12PipelineLibrary1 *, const WCHAR *, const D3D12_PIPELINE_STATE_STREAM_DESC *, REFIID, void **);

static HRESULT STDMETHODCALLTYPE hook_LoadGraphicsPipeline(ID3D12PipelineLibrary *lib, const WCHAR *name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID riid, void **out)
{
    D3D12_INPUT_ELEMENT_DESC elements[INPUT_OFFSET_LIMIT];
    D3D12_INPUT_LAYOUT_DESC layout;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC copy;
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC *use = desc;
    if (desc && explicit_input_layout(&desc->InputLayout, elements, &layout)) {
        copy = *desc; copy.InputLayout = layout; use = &copy;
        LOG("INPUT_OFFSETS resolved lib-graphics layout (%u elements)", layout.NumElements);
    }
    HRESULT hr = real_LoadGraphicsPipeline(lib, name, use, riid, out);
    if (SUCCEEDED(hr) && out && *out) remember_pipeline((ID3D12PipelineState *)*out, use, "lib-graphics");
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_LoadComputePipeline(ID3D12PipelineLibrary *lib, const WCHAR *name, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID riid, void **out)
{
    HRESULT hr = real_LoadComputePipeline(lib, name, desc, riid, out);
    if (SUCCEEDED(hr) && out && *out) remember_compute_pipeline((ID3D12PipelineState *)*out, desc, "lib-compute");
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_LoadPipeline(ID3D12PipelineLibrary1 *lib, const WCHAR *name, const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID riid, void **out)
{
    D3D12_INPUT_ELEMENT_DESC elements[INPUT_OFFSET_LIMIT];
    D3D12_PIPELINE_STATE_STREAM_DESC copy;
    const D3D12_PIPELINE_STATE_STREAM_DESC *use = desc;
    void *owned;
    if (explicit_input_stream(desc, elements, &copy, &owned)) {
        use = &copy;
        LOG("INPUT_OFFSETS resolved lib-stream layout");
    }
    HRESULT hr = real_LoadPipeline(lib, name, use, riid, out);
    if (SUCCEEDED(hr) && out && *out) remember_pipeline_stream((ID3D12PipelineState *)*out, use, "lib-stream");
    free(owned);
    return hr;
}

static void patch_library_vtbl(void *iface)
{
    ID3D12PipelineLibraryVtbl *vt = *(ID3D12PipelineLibraryVtbl **)iface;
    patch_slot((void **)&vt->LoadGraphicsPipeline, (void *)hook_LoadGraphicsPipeline, (void **)&real_LoadGraphicsPipeline);
    patch_slot((void **)&vt->LoadComputePipeline, (void *)hook_LoadComputePipeline, (void **)&real_LoadComputePipeline);
}

static void patch_library1_vtbl(void *iface)
{
    ID3D12PipelineLibrary1Vtbl *vt = *(ID3D12PipelineLibrary1Vtbl **)iface;
    patch_slot((void **)&vt->LoadGraphicsPipeline, (void *)hook_LoadGraphicsPipeline, (void **)&real_LoadGraphicsPipeline);
    patch_slot((void **)&vt->LoadComputePipeline, (void *)hook_LoadComputePipeline, (void **)&real_LoadComputePipeline);
    patch_slot((void **)&vt->LoadPipeline, (void *)hook_LoadPipeline, (void **)&real_LoadPipeline);
}

static void patch_library(void *lib)
{
    static const IID *const lib_ids[] = {
        &IID_ID3D12PipelineLibrary,
#ifdef __ID3D12PipelineLibrary1_INTERFACE_DEFINED__
        &IID_ID3D12PipelineLibrary1,
#endif
    };
    patch_library_vtbl(lib);
    for (int i = 0; i < (int)(sizeof lib_ids / sizeof *lib_ids); i++) {
        void *p = NULL;
        if (SUCCEEDED(((IUnknown *)lib)->lpVtbl->QueryInterface((IUnknown *)lib, lib_ids[i], &p)) && p) {
            if (i == 0) patch_library_vtbl(p);
            else patch_library1_vtbl(p);
            ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
        }
    }
}

static HRESULT STDMETHODCALLTYPE hook_CreatePipelineLibrary(ID3D12Device1 *dev, const void *blob, SIZE_T blob_size, REFIID riid, void **lib)
{
    HRESULT hr = real_CreatePipelineLibrary(dev, blob, blob_size, riid, lib);
    if (SUCCEEDED(hr) && lib && *lib) patch_library(*lib);
    return hr;
}

static void patch_device1_vtbl(void *iface)
{
    ID3D12Device1Vtbl *vt = *(ID3D12Device1Vtbl **)iface;
    patch_slot((void **)&vt->CreatePipelineLibrary, (void *)hook_CreatePipelineLibrary, (void **)&real_CreatePipelineLibrary);
}

// ---- pipeline creation ----------------------------------------------------------------------
static int no_gs;
static HRESULT (STDMETHODCALLTYPE *real_CreateGraphicsPipelineState)(ID3D12Device *, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE hook_CreateGraphicsPipelineState(ID3D12Device *dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID riid, void **out)
{
    D3D12_INPUT_ELEMENT_DESC elements[INPUT_OFFSET_LIMIT];
    D3D12_INPUT_LAYOUT_DESC layout;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC copy;
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC *use = desc;
    if (desc) {
        if (no_gs && desc->GS.pShaderBytecode) {
            copy = *desc;
            copy.GS.pShaderBytecode = NULL; copy.GS.BytecodeLength = 0;
            use = &copy;
            LOG("  -> DROPPED GS from pipeline state!");
        }
        if (no_tess && desc->HS.pShaderBytecode) {
            copy = *use;
            copy.HS.pShaderBytecode = NULL; copy.HS.BytecodeLength = 0;
            copy.DS.pShaderBytecode = NULL; copy.DS.BytecodeLength = 0;
            copy.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            use = &copy;
            LOG("  -> DROPPED HS+DS (tessellation) from pipeline state, topology forced to TRIANGLE!");
        }
        if (explicit_input_layout(&use->InputLayout, elements, &layout)) {
            copy = *use; copy.InputLayout = layout; use = &copy;
            LOG("INPUT_OFFSETS resolved graphics layout (%u elements)", layout.NumElements);
        }
    }
    HRESULT hr = real_CreateGraphicsPipelineState(dev, use, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        remember_pipeline((ID3D12PipelineState *)*out, use, "graphics");
    }
    return hr;
}

static HRESULT (STDMETHODCALLTYPE *real_CreateComputePipelineState)(ID3D12Device *, const D3D12_COMPUTE_PIPELINE_STATE_DESC *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE hook_CreateComputePipelineState(ID3D12Device *dev, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID riid, void **out)
{
    HRESULT hr = real_CreateComputePipelineState(dev, desc, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        remember_compute_pipeline((ID3D12PipelineState *)*out, desc, "compute");
    }
    return hr;
}

static HRESULT (STDMETHODCALLTYPE *real_CreateCommandList)(ID3D12Device *, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator *, ID3D12PipelineState *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE hook_CreateCommandList(ID3D12Device *dev, UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *alloc, ID3D12PipelineState *initial_pso, REFIID riid, void **out)
{
    HRESULT hr = real_CreateCommandList(dev, node_mask, type, alloc, initial_pso, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        patch_list_vtbl(*out);
        if (trace_lock_ready) {
            EnterCriticalSection(&trace_lock);
            ListTrace *lt = lookup_list_locked((ID3D12GraphicsCommandList *)*out);
            if (lt) {
                lt->is_bundle = (type == D3D12_COMMAND_LIST_TYPE_BUNDLE);
                lt->pso = initial_pso;
            }
            if (type == D3D12_COMMAND_LIST_TYPE_BUNDLE) {
                lookup_bundle_locked((ID3D12GraphicsCommandList *)*out);
            }
            LeaveCriticalSection(&trace_lock);
            if (initial_pso) {
                set_list_pipeline((ID3D12GraphicsCommandList *)*out, initial_pso);
            }
        }
    }
    return hr;
}

// ---- Draw Hooks (Instanced, Indexed, Indirect, Dispatch, OMSetRenderTargets) -----------------

static void (STDMETHODCALLTYPE *real_OMSetRenderTargets)(ID3D12GraphicsCommandList *, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE *, WINBOOL, const D3D12_CPU_DESCRIPTOR_HANDLE *);
static void STDMETHODCALLTYPE hook_OMSetRenderTargets(ID3D12GraphicsCommandList *list, UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors, WINBOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor)
{
    if (trace_lock_ready) {
        EnterCriticalSection(&trace_lock);
        ListTrace *lt = lookup_list_locked(list);
        if (lt) {
            lt->num_rtvs = (pRenderTargetDescriptors != NULL) ? NumRenderTargetDescriptors : 0;
            lt->has_dsv = (pDepthStencilDescriptor != NULL && pDepthStencilDescriptor->ptr != 0) ? 1 : 0;
        }
        LeaveCriticalSection(&trace_lock);
    }
    real_OMSetRenderTargets(list, NumRenderTargetDescriptors, pRenderTargetDescriptors, RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
}

static int no_ei;
static void (STDMETHODCALLTYPE *real_DrawInstanced)(ID3D12GraphicsCommandList *, UINT, UINT, UINT, UINT);
static void STDMETHODCALLTYPE hook_DrawInstanced(ID3D12GraphicsCommandList *list, UINT vcount, UINT icount, UINT start_v, UINT start_i)
{
    UINT ord = (UINT)InterlockedIncrement(&g_frame_draw_ordinal);
    ID3D12PipelineState *cur_pso = current_pipeline(list);

    if (g_census_active) {
        write_census_per_draw("Draw", list, cur_pso, vcount, icount, ord, start_v, 0, start_i);
    }

    if (should_skip_graphics_draw(cur_pso, ord, "Draw", icount)) {
        return;
    }
    if (trace_lock_ready) {
        EnterCriticalSection(&trace_lock);
        ListTrace *lt = lookup_list_locked(list);
        if (lt && lt->is_bundle) {
            BundleTrace *b = lookup_bundle_locked(list);
            if (b && b->draw_count < MAX_BUNDLE_DRAWS) {
                BundleDrawInfo *bd = &b->draws[b->draw_count++];
                bd->pso = lt->pso;
                bd->is_indexed = 0;
                bd->count = vcount;
                bd->icount = icount;
                memcpy(bd->vb_strides, lt->vb_strides, sizeof(bd->vb_strides));
            }
        }
        LeaveCriticalSection(&trace_lock);
    }
    // Step A: Census
    census_record_draw(list, vcount, icount);
    real_DrawInstanced(list, vcount, icount, start_v, start_i);
}

static void (STDMETHODCALLTYPE *real_DrawIndexedInstanced)(ID3D12GraphicsCommandList *, UINT, UINT, UINT, INT, UINT);
static void STDMETHODCALLTYPE hook_DrawIndexedInstanced(ID3D12GraphicsCommandList *list, UINT idx_count, UINT icount, UINT start_idx, INT base_v, UINT start_i)
{
    UINT ord = (UINT)InterlockedIncrement(&g_frame_draw_ordinal);
    ID3D12PipelineState *cur_pso = current_pipeline(list);

    if (g_census_active) {
        write_census_per_draw("DrawIndexed", list, cur_pso, idx_count, icount, ord, start_idx, base_v, start_i);
    }

    if (should_skip_graphics_draw(cur_pso, ord, "DrawIndexed", icount)) {
        return;
    }
    if (trace_lock_ready) {
        EnterCriticalSection(&trace_lock);
        ListTrace *lt = lookup_list_locked(list);
        if (lt && lt->is_bundle) {
            BundleTrace *b = lookup_bundle_locked(list);
            if (b && b->draw_count < MAX_BUNDLE_DRAWS) {
                BundleDrawInfo *bd = &b->draws[b->draw_count++];
                bd->pso = lt->pso;
                bd->is_indexed = 1;
                bd->count = idx_count;
                bd->icount = icount;
                memcpy(bd->vb_strides, lt->vb_strides, sizeof(bd->vb_strides));
            }
        }
        LeaveCriticalSection(&trace_lock);
    }
    // Step A: Census
    census_record_draw_indexed(list, idx_count, icount);
    real_DrawIndexedInstanced(list, idx_count, icount, start_idx, base_v, start_i);
}

static void (STDMETHODCALLTYPE *real_Dispatch)(ID3D12GraphicsCommandList *, UINT, UINT, UINT);
static void STDMETHODCALLTYPE hook_Dispatch(ID3D12GraphicsCommandList *list, UINT x, UINT y, UINT z)
{
    ID3D12PipelineState *cur_pso = current_pipeline(list);

    if (g_census_active) {
        census_record_dispatch(list, x, y, z);
        EnterCriticalSection(&trace_lock);
        if (g_num_pending_dispatches < MAX_PENDING_DISPATCHES) {
            PipelineTrace *p = lookup_pipeline(cur_pso);
            DispatchRecord *rec = &g_pending_dispatches[g_num_pending_dispatches++];
            rec->pso_index = p ? p->creation_index : 0;
            rec->cs_hash = p ? p->cs_hash : 0;
            rec->x = x; rec->y = y; rec->z = z;
        }
        LeaveCriticalSection(&trace_lock);
    }

    if (should_skip_compute_dispatch(cur_pso, x, y, z)) {
        return;
    }

    real_Dispatch(list, x, y, z);
}

static void (STDMETHODCALLTYPE *real_ExecuteIndirect)(ID3D12GraphicsCommandList *, ID3D12CommandSignature *, UINT, ID3D12Resource *, UINT64, ID3D12Resource *, UINT64);
static void (STDMETHODCALLTYPE *real_SetPipelineState)(ID3D12GraphicsCommandList *, ID3D12PipelineState *);
static void STDMETHODCALLTYPE hook_SetPipelineState(ID3D12GraphicsCommandList *list, ID3D12PipelineState *pso)
{
    if (trace_lock_ready) {
        set_list_pipeline(list, pso);
    }
    real_SetPipelineState(list, pso);
}

static void (STDMETHODCALLTYPE *real_IASetVertexBuffers)(ID3D12GraphicsCommandList *, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW *);
static void STDMETHODCALLTYPE hook_IASetVertexBuffers(ID3D12GraphicsCommandList *list, UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW *pViews)
{
    if (trace_lock_ready) {
        EnterCriticalSection(&trace_lock);
        ListTrace *lt = lookup_list_locked(list);
        if (lt) {
            for (UINT i = 0; i < NumViews; i++) {
                UINT slot = StartSlot + i;
                if (slot < 16) {
                    lt->vb_addresses[slot] = pViews ? pViews[i].BufferLocation : 0;
                    lt->vb_strides[slot] = (pViews && pViews[i].BufferLocation) ? pViews[i].StrideInBytes : 0;
                    lt->vb_sizes[slot] = (pViews && pViews[i].BufferLocation) ? pViews[i].SizeInBytes : 0;
                }
            }
        }
        LeaveCriticalSection(&trace_lock);
    }
    real_IASetVertexBuffers(list, StartSlot, NumViews, pViews);
}

static void (STDMETHODCALLTYPE *real_ExecuteBundle)(ID3D12GraphicsCommandList *, ID3D12GraphicsCommandList *);
static void STDMETHODCALLTYPE hook_ExecuteBundle(ID3D12GraphicsCommandList *list, ID3D12GraphicsCommandList *bundle)
{
    g_bundles_executed_in_frame++;
    if (trace_lock_ready) {
        EnterCriticalSection(&trace_lock);
        BundleTrace *b = lookup_bundle_locked(bundle);
        if (b && g_census_active) {
            for (UINT i = 0; i < b->draw_count; i++) {
                BundleDrawInfo *bd = &b->draws[i];
                if (bd->is_indexed) {
                    census_record_draw_indexed_with_pso(bd->pso, bd->count, bd->icount, bd->vb_strides);
                } else {
                    census_record_draw_with_pso(bd->pso, bd->count, bd->icount, bd->vb_strides);
                }
            }
        }
        ID3D12PipelineState *bundle_pso = current_pipeline(bundle);
        if (bundle_pso) {
            ListTrace *lt = lookup_list_locked(list);
            if (lt) lt->pso = bundle_pso;
        }
        LeaveCriticalSection(&trace_lock);
    }
    real_ExecuteBundle(list, bundle);
    if (g_capture_bindings) {
        /* Bundle state is inherited selectively. Do not report stale direct-list bindings. */
        EnterCriticalSection(&trace_lock);
        ListTrace *lt = lookup_list_locked(list);
        if (lt) invalidate_binding_snapshot(lt);
        LeaveCriticalSection(&trace_lock);
    }
}

static HRESULT (STDMETHODCALLTYPE *real_Reset)(ID3D12GraphicsCommandList *, ID3D12CommandAllocator *, ID3D12PipelineState *);
static HRESULT STDMETHODCALLTYPE hook_Reset(ID3D12GraphicsCommandList *list, ID3D12CommandAllocator *alloc, ID3D12PipelineState *initial_pso)
{
    HRESULT hr = real_Reset(list, alloc, initial_pso);
    if (FAILED(hr)) return hr;
    if (trace_lock_ready) {
        EnterCriticalSection(&trace_lock);
        ListTrace *lt = lookup_list_locked(list);
        if (lt) {
            lt->pso = initial_pso;
            lt->num_rtvs = 0;
            lt->has_dsv = 0;
            memset(lt->vb_strides, 0, sizeof lt->vb_strides);
            memset(lt->vb_sizes, 0, sizeof lt->vb_sizes);
            memset(lt->vb_addresses, 0, sizeof lt->vb_addresses);
            memset(&lt->index_view, 0, sizeof lt->index_view);
            lt->graphics_root = NULL;
            memset(lt->root_kinds, 0, sizeof lt->root_kinds);
            memset(lt->root_constant_masks, 0, sizeof lt->root_constant_masks);
            lt->bound_heap_count = 0;
            lt->recording_generation++;
            if (lt->is_bundle) {
                BundleTrace *b = lookup_bundle_locked(list);
                if (b) b->draw_count = 0;
            }
        }
        LeaveCriticalSection(&trace_lock);
    }
    return hr;
}

static void (STDMETHODCALLTYPE *real_ResourceBarrier)(ID3D12GraphicsCommandList *, UINT, const D3D12_RESOURCE_BARRIER *);
static void STDMETHODCALLTYPE hook_ResourceBarrier(ID3D12GraphicsCommandList *list, UINT num, const D3D12_RESOURCE_BARRIER *barriers)
{
    if (num && barriers) {
        for (UINT i = 0; i < num; i++) {
            if (barriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
                if (barriers[i].Transition.StateAfter == D3D12_RESOURCE_STATE_PRESENT) {
                    g_frame_has_present = 1;
                }
            }
        }
    }
    real_ResourceBarrier(list, num, barriers);
}

static void (STDMETHODCALLTYPE *real_ExecuteCommandLists)(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);
static void STDMETHODCALLTYPE hook_ExecuteCommandLists(ID3D12CommandQueue *queue, UINT num, ID3D12CommandList *const *lists)
{
    real_ExecuteCommandLists(queue, num, lists);

    // Frame boundary detection
    if (g_frame_has_present) {
        g_frame_has_present = 0;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        static UINT64 last_present_qpc = 0;
        if (last_present_qpc != 0 && qpc_freq != 0) {
            double delta_ms = (double)(now.QuadPart - last_present_qpc) * 1000.0 / (double)qpc_freq;
            if (delta_ms < 5.0) {
                // Ignore secondary submissions within the same present burst
                return;
            }
        }
        last_present_qpc = (UINT64)now.QuadPart;

        // If world has loaded (either by high PSO count or indirect draw start)
        if (!g_world_loaded && (pipeline_hash_count >= 8000 || trace_indirect_context_count > 0)) {
            g_world_loaded = 1;
            LOG("WORLD_LOAD_DETECTED at frame boundary, total PSOs: %u", pipeline_hash_count);
        }

        // Reset draw ordinal for upcoming frame
        InterlockedExchange(&g_frame_draw_ordinal, 0);

        // Step 1: Hot-reload C:\highball\skip.txt every ~30 frames
        g_hot_skip_frame_counter++;
        if (g_hot_skip_enabled && (g_hot_skip_frame_counter % 30 == 0 || !g_hot_skip_initialized)) {
            hot_skip_reload();
        }

        if (g_world_loaded) {
            g_world_frame_index++;

            // Step 2: Counters printed at the frame boundary every ~300 frames
            if ((g_world_frame_index % 300) == 0 && (g_num_hot_skip_rules > 0 || g_num_skip_vs > 0 || skip_all_instanced)) {
                UINT64 total_all = g_total_skips_indexed + g_total_skips_instanced + g_total_skips_indirect;
                LOG("==================== HB_TSSHIM_SKIP SUMMARY (World Frame %u) ====================", g_world_frame_index);
                LOG("Total Skips: %llu (Indexed: %llu, Instanced: %llu, Indirect: %llu) | Skipped in Frame %u: %llu",
                    (unsigned long long)total_all,
                    (unsigned long long)g_total_skips_indexed,
                    (unsigned long long)g_total_skips_instanced,
                    (unsigned long long)g_total_skips_indirect,
                    g_world_frame_index,
                    (unsigned long long)g_frame_skips_count);
                if (g_hot_skip_enabled) {
                    LOG("Hot-Skip Mode: %u active rules from C:\\highball\\skip.txt", g_num_hot_skip_rules);
                } else {
                    for (UINT k = 0; k < g_num_skip_vs; k++) {
                        UINT64 h_total = g_skip_counters[k].draws_indexed + g_skip_counters[k].draws_instanced + g_skip_counters[k].draws_indirect;
                        LOG("  [VS %08x] Total: %llu (Indexed: %llu, Instanced: %llu, Indirect: %llu)",
                            g_skip_vs_hashes[k],
                            (unsigned long long)h_total,
                            (unsigned long long)g_skip_counters[k].draws_indexed,
                            (unsigned long long)g_skip_counters[k].draws_instanced,
                            (unsigned long long)g_skip_counters[k].draws_indirect);
                    }
                }
                if (skip_all_instanced) {
                    LOG("  SKIP_ALL_INSTANCED active: Indexed skipped=%llu, Non-indexed skipped=%llu",
                        (unsigned long long)g_skip_all_instanced_indexed_count,
                        (unsigned long long)g_skip_all_instanced_non_indexed_count);
                }
                LOG("==================================================================================");
            }
            g_frame_skips_count = 0;

            // Step 2: Trigger-file census check (C:\\highball\\census_now)
            DWORD attr = GetFileAttributesA("C:\\highball\\census_now");
            if (attr != INVALID_FILE_ATTRIBUTES) {
                DeleteFileA("C:\\highball\\census_now");
                g_trigger_census_pending = 1;
                LOG("CENSUS_TRIGGER_FILE detected and deleted: C:\\highball\\census_now. Census armed for NEXT frame.");
            }

            // If a census was active in the previous frame, end it now and dump
            if (g_census_active) {
                g_census_active = 0;
                EnterCriticalSection(&trace_lock);
                if (g_census_draws_file) {
                    for (UINT d = 0; d < g_num_pending_dispatches; d++) {
                        fprintf(g_census_draws_file, "# DISPATCH (post-frame): pso %u | cs %08x | groups (%u, %u, %u)\n",
                            g_pending_dispatches[d].pso_index, g_pending_dispatches[d].cs_hash,
                            g_pending_dispatches[d].x, g_pending_dispatches[d].y, g_pending_dispatches[d].z);
                    }
                    g_num_pending_dispatches = 0;
                    fclose(g_census_draws_file);
                    g_census_draws_file = NULL;
                    LOG("CENSUS: Completed writing C:\\highball\\census_draws.txt");
                }
                LeaveCriticalSection(&trace_lock);
                dump_census_table();
                LOG("DRAW_CENSUS_COMPLETED for world frame %u", g_world_frame_index - 1);
            }

            // Start census if triggered
            if (g_trigger_census_pending || (g_census_target_frame > 0 && !g_census_target_frame_done && (int)g_world_frame_index == g_census_target_frame)) {
                if (g_trigger_census_pending) {
                    g_trigger_census_pending = 0;
                    g_census_trigger_count++;
                    LOG("DRAW_CENSUS_STARTED via trigger file (run #%u, world frame %u)", g_census_trigger_count, g_world_frame_index);
                } else {
                    g_census_target_frame_done = 1;
                    LOG("DRAW_CENSUS_STARTED via frame counter (world frame %u)", g_world_frame_index);
                }
                g_census_active = 1;
                EnterCriticalSection(&trace_lock);
                g_census_entry_count = 0;
                if (g_capture_bindings) reset_upload_samples();
                memset(g_census_entries, 0, sizeof(g_census_entries));
                g_bundles_executed_in_frame = 0;
                g_num_pending_dispatches = 0;
                if (g_census_draws_file) {
                    fclose(g_census_draws_file);
                    g_census_draws_file = NULL;
                }
                g_census_draws_file = fopen("C:\\highball\\census_draws.txt", "w");
                if (g_census_draws_file) {
                    fprintf(g_census_draws_file, "# CENSUS PER-DRAW TRACE - World Frame %u\n", g_world_frame_index);
                    fprintf(g_census_draws_file, "# shim_build binding-capture-v5; CPU command-recording order, not GPU execution order; frame boundary inferred from PRESENT barriers\n");
                    fprintf(g_census_draws_file, "# Format: ord <ordinal> | <draw_type> | pso <idx> | vs <vs_hash> | ps <ps_hash> | count <count> | inst <icount> | [start_idx/vtx, base_vtx, start_inst] | rtvs <rtvs> | dsv <dsv> | vb_strides [...] | vb_sizes [...]\n");
                    fflush(g_census_draws_file);
                }
                LeaveCriticalSection(&trace_lock);
            }

            // Step B: Readback indirect args if capture was recorded
            if (capture_indirect_args && g_readback_recorded && !g_readback_completed && g_readback_buf) {
                D3D12_RANGE rrange = { 0, (SIZE_T)args_captured_count * 16 };
                void *cpu_ptr = NULL;
                HRESULT hr_map = g_readback_buf->lpVtbl->Map(g_readback_buf, 0, &rrange, &cpu_ptr);
                if (SUCCEEDED(hr_map) && cpu_ptr) {
                    g_readback_completed = 1;
                    const UINT *uvals = (const UINT *)cpu_ptr;
                    LOG("======================== RAW INDIRECT ARGUMENT VALUES (%u Samples) ========================", args_captured_count);
                    for (int s = 0; s < args_captured_count; s++) {
                        UINT vcount = uvals[s * 4 + 0];
                        UINT icount = uvals[s * 4 + 1];
                        UINT vstart = uvals[s * 4 + 2];
                        UINT istart = uvals[s * 4 + 3];
                        LOG("RAW_INDIRECT_ARG[%2d] pso=%p (PSO %u) VertexCountPerInstance=%u InstanceCount=%u StartVertexLocation=%u StartInstanceLocation=%u",
                            s + 1, arg_samples[s].pso, arg_samples[s].creation_index,
                            vcount, icount, vstart, istart);
                    }
                    LOG("==========================================================================================");
                    D3D12_RANGE wrange = { 0, 0 };
                    g_readback_buf->lpVtbl->Unmap(g_readback_buf, 0, &wrange);
                }
            }
        }
    }
}

static void STDMETHODCALLTYPE hook_ExecuteIndirect(ID3D12GraphicsCommandList *list, ID3D12CommandSignature *sig, UINT max_count, ID3D12Resource *arg_buf, UINT64 arg_offset, ID3D12Resource *count_buf, UINT64 count_offset)
{
    UINT ord = (UINT)InterlockedIncrement(&g_frame_draw_ordinal);
    ID3D12PipelineState *cur_pso = current_pipeline(list);

    if (g_census_active) {
        write_census_per_draw("Indirect", list, cur_pso, 0, max_count, ord, 0, 0, 0);
    }

    if (should_skip_graphics_draw(cur_pso, ord, "Indirect", max_count)) {
        return;
    }

    if (no_ei) {
        static UINT64 dropped;
        if ((++dropped % 1000) == 1) LOG("ExecuteIndirect: %llu calls dropped (HB_TSSHIM_NOEI=1)", (unsigned long long)dropped);
        return;
    }

    // Step A: Census
    census_record_indirect(list, max_count);

    if (trace_indirect_pso && trace_lock_ready) {
        report_indirect_context(list, sig);

        // Step 3c: Probe argument buffer attributes once
        if (!arg_probe_done && arg_buf && arg_buf->lpVtbl) {
            arg_probe_done = 1;
            D3D12_RESOURCE_DESC rd;
            memset(&rd, 0, sizeof rd);
            if (arg_buf->lpVtbl->GetDesc) arg_buf->lpVtbl->GetDesc(arg_buf, &rd);
            D3D12_HEAP_PROPERTIES hp;
            D3D12_HEAP_FLAGS hf = 0;
            memset(&hp, 0, sizeof hp);
            HRESULT hr_heap = E_FAIL;
            if (arg_buf->lpVtbl->GetHeapProperties) hr_heap = arg_buf->lpVtbl->GetHeapProperties(arg_buf, &hp, &hf);
            LOG("INDIRECT_ARG_BUF_PROBE res=%p dim=%u size=%llu flags=0x%x hr_heap=0x%08lx heap_type=%u cpu_page=%u mem_pool=%u",
                arg_buf, (unsigned)rd.Dimension, (unsigned long long)rd.Width, (unsigned)rd.Flags,
                (unsigned long)hr_heap, (unsigned)hp.Type, (unsigned)hp.CPUPageProperty, (unsigned)hp.MemoryPoolPreference);
        }

        // Step B: Capture indirect argument values (switchable via HB_TSSHIM_CAPTURE_INDIRECT_ARGS=1)
        if (capture_indirect_args && args_captured_count < MAX_ARG_SAMPLES && arg_buf && arg_buf->lpVtbl) {
            // First attempt direct CPU map (safest, zero GPU state modification)
            D3D12_RANGE rrange = { (SIZE_T)arg_offset, (SIZE_T)(arg_offset + 16) };
            void *cpu_ptr = NULL;
            HRESULT hr_map = E_FAIL;
            if (arg_buf->lpVtbl->Map) hr_map = arg_buf->lpVtbl->Map(arg_buf, 0, &rrange, &cpu_ptr);
            if (SUCCEEDED(hr_map) && cpu_ptr) {
                const UINT *vals = (const UINT *)((const BYTE *)cpu_ptr + arg_offset);
                int idx = args_captured_count++;
                PipelineTrace *p = lookup_pipeline(current_pipeline(list));
                arg_samples[idx].sample_id = idx + 1;
                arg_samples[idx].pso = current_pipeline(list);
                arg_samples[idx].creation_index = p ? p->creation_index : 0;
                arg_samples[idx].arg_offset = arg_offset;
                arg_samples[idx].vertex_count = vals[0];
                arg_samples[idx].instance_count = vals[1];
                arg_samples[idx].start_vertex = vals[2];
                arg_samples[idx].start_instance = vals[3];
                D3D12_RANGE wrange = { 0, 0 };
                arg_buf->lpVtbl->Unmap(arg_buf, 0, &wrange);

                LOG("RAW_INDIRECT_ARG[%2d] (DirectMap) pso=%p (PSO %u) VertexCountPerInstance=%u InstanceCount=%u StartVertexLocation=%u StartInstanceLocation=%u",
                    idx + 1, arg_samples[idx].pso, arg_samples[idx].creation_index,
                    vals[0], vals[1], vals[2], vals[3]);
            } else {
                // Direct map failed (standard for DEFAULT heaps). Initialize readback buffer if needed.
                if (!g_readback_buf) {
                    ID3D12Device *dev = NULL;
                    if (SUCCEEDED(list->lpVtbl->GetDevice(list, &IID_ID3D12Device, (void **)&dev)) && dev) {
                        D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_READBACK, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
                        D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, (UINT64)MAX_ARG_SAMPLES * 16, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
                        HRESULT hr_rb = dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&g_readback_buf);
                        LOG("Created indirect argument readback buffer (hr=0x%08lx, res=%p)", (unsigned long)hr_rb, g_readback_buf);
                        dev->lpVtbl->Release(dev);
                    }
                }
                if (g_readback_buf) {
                    int idx = args_captured_count++;
                    PipelineTrace *p = lookup_pipeline(current_pipeline(list));
                    arg_samples[idx].sample_id = idx + 1;
                    arg_samples[idx].pso = current_pipeline(list);
                    arg_samples[idx].creation_index = p ? p->creation_index : 0;
                    arg_samples[idx].arg_offset = arg_offset;

                    // Transition arg buffer to COPY_SOURCE, copy 16 bytes, transition back
                    D3D12_RESOURCE_BARRIER b[2];
                    memset(b, 0, sizeof b);
                    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    b[0].Transition.pResource = arg_buf;
                    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
                    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                    list->lpVtbl->ResourceBarrier(list, 1, &b[0]);

                    list->lpVtbl->CopyBufferRegion(list, g_readback_buf, idx * 16, arg_buf, arg_offset, 16);

                    b[1] = b[0];
                    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
                    list->lpVtbl->ResourceBarrier(list, 1, &b[1]);

                    g_readback_recorded = 1;
                }
            }
        }
    }

    if (dbg) {
        static UINT64 count;
        count++;
        if (count <= 10 || (count % 1000) == 0) {
            LOG("ExecuteIndirect sig=%p max=%u arg=%p+%llu count=%p+%llu (#%llu)",
                sig, max_count, arg_buf, (unsigned long long)arg_offset,
                count_buf, (unsigned long long)count_offset, (unsigned long long)count);
        }
    }

    real_ExecuteIndirect(list, sig, max_count, arg_buf, arg_offset, count_buf, count_offset);
    if (g_capture_bindings) {
        EnterCriticalSection(&trace_lock);
        SignatureTrace *signature = lookup_signature(sig);
        int modifies_bindings = !signature;
        if (signature) {
            for (UINT i = 0; i < signature->num_args && i < 16; i++) {
                if (signature->arg_types[i] >= 3 && signature->arg_types[i] <= 8) modifies_bindings = 1;
            }
        }
        if (modifies_bindings) {
            ListTrace *lt = lookup_list_locked(list);
            if (lt) invalidate_binding_snapshot(lt);
        }
        LeaveCriticalSection(&trace_lock);
    }
}

static void patch_device_vtbl(void *iface)
{
    ID3D12DeviceVtbl *vt = *(ID3D12DeviceVtbl **)iface;
    patch_slot((void **)&vt->CreateQueryHeap, (void *)hook_CreateQueryHeap, (void **)&real_CreateQueryHeap);
    patch_slot((void **)&vt->CheckFeatureSupport, (void *)hook_CheckFeatureSupport, (void **)&real_CheckFeatureSupport);
    patch_slot((void **)&vt->CreateGraphicsPipelineState, (void *)hook_CreateGraphicsPipelineState, (void **)&real_CreateGraphicsPipelineState);
    patch_slot((void **)&vt->CreateComputePipelineState, (void *)hook_CreateComputePipelineState, (void **)&real_CreateComputePipelineState);
    patch_slot((void **)&vt->CreateCommandSignature, (void *)hook_CreateCommandSignature, (void **)&real_CreateCommandSignature);
    patch_slot((void **)&vt->CreateCommandList, (void *)hook_CreateCommandList, (void **)&real_CreateCommandList);
    if (g_capture_bindings) patch_slot((void **)&vt->CreateRootSignature, (void *)hook_capture_CreateRootSignature, (void **)&real_capture_CreateRootSignature);
    if (g_capture_bindings) patch_descriptor_slots(vt);
    if (dbg || g_capture_bindings) {
        patch_slot((void **)&vt->CreateCommittedResource, (void *)hook_CreateCommittedResource, (void **)&real_CreateCommittedResource);
        patch_slot((void **)&vt->CreatePlacedResource, (void *)hook_CreatePlacedResource, (void **)&real_CreatePlacedResource);
        patch_slot((void **)&vt->CreateReservedResource, (void *)hook_CreateReservedResource, (void **)&real_CreateReservedResource);
        patch_slot((void **)&vt->CreateShaderResourceView, (void *)hook_CreateShaderResourceView, (void **)&real_CreateShaderResourceView);
        patch_slot((void **)&vt->CreateUnorderedAccessView, (void *)hook_CreateUnorderedAccessView, (void **)&real_CreateUnorderedAccessView);
        patch_slot((void **)&vt->CreateRenderTargetView, (void *)hook_CreateRenderTargetView, (void **)&real_CreateRenderTargetView);
        patch_slot((void **)&vt->CreateDepthStencilView, (void *)hook_CreateDepthStencilView, (void **)&real_CreateDepthStencilView);
    }
}

static void patch_list_vtbl(void *iface)
{
    ID3D12GraphicsCommandListVtbl *vt = *(ID3D12GraphicsCommandListVtbl **)iface;
    patch_slot((void **)&vt->BeginQuery, (void *)hook_BeginQuery, (void **)&real_BeginQuery);
    patch_slot((void **)&vt->EndQuery, (void *)hook_EndQuery, (void **)&real_EndQuery);
    patch_slot((void **)&vt->ResolveQueryData, (void *)hook_ResolveQueryData, (void **)&real_ResolveQueryData);
    patch_slot((void **)&vt->ResolveSubresource, (void *)hook_ResolveSubresource, (void **)&real_ResolveSubresource);
    patch_slot((void **)&vt->SetPipelineState, (void *)hook_SetPipelineState, (void **)&real_SetPipelineState);
    patch_slot((void **)&vt->DrawInstanced, (void *)hook_DrawInstanced, (void **)&real_DrawInstanced);
    patch_slot((void **)&vt->DrawIndexedInstanced, (void *)hook_DrawIndexedInstanced, (void **)&real_DrawIndexedInstanced);
    patch_slot((void **)&vt->Dispatch, (void *)hook_Dispatch, (void **)&real_Dispatch);
    patch_slot((void **)&vt->OMSetRenderTargets, (void *)hook_OMSetRenderTargets, (void **)&real_OMSetRenderTargets);
    patch_slot((void **)&vt->IASetVertexBuffers, (void *)hook_IASetVertexBuffers, (void **)&real_IASetVertexBuffers);
    if (g_capture_bindings) patch_binding_slots(vt);
    patch_slot((void **)&vt->ExecuteIndirect, (void *)hook_ExecuteIndirect, (void **)&real_ExecuteIndirect);
    patch_slot((void **)&vt->ExecuteBundle, (void *)hook_ExecuteBundle, (void **)&real_ExecuteBundle);
    patch_slot((void **)&vt->Reset, (void *)hook_Reset, (void **)&real_Reset);
    patch_slot((void **)&vt->ResourceBarrier, (void *)hook_ResourceBarrier, (void **)&real_ResourceBarrier);
}

static void patch_queue_vtbl(void *iface)
{
    ID3D12CommandQueueVtbl *vt = *(ID3D12CommandQueueVtbl **)iface;
    patch_slot((void **)&vt->GetTimestampFrequency, (void *)hook_GetTimestampFrequency, (void **)&real_GetTimestampFrequency);
    patch_slot((void **)&vt->GetClockCalibration, (void *)hook_GetClockCalibration, (void **)&real_GetClockCalibration);
    patch_slot((void **)&vt->ExecuteCommandLists, (void *)hook_ExecuteCommandLists, (void **)&real_ExecuteCommandLists);
}

static void patch_all(IUnknown *obj, const IID *const *ids, int n, void (*patch)(void *))
{
    int i; void *p;
    patch(obj);
    for (i = 0; i < n; i++) {
        if (FAILED(obj->lpVtbl->QueryInterface(obj, ids[i], &p)) || !p) continue;
        patch(p); ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
    }
}

static void patch_device(ID3D12Device *dev)
{
    static const IID *const dev_ids[] = {
        &IID_ID3D12Device1, &IID_ID3D12Device2, &IID_ID3D12Device3, &IID_ID3D12Device4, &IID_ID3D12Device5,
#ifdef __ID3D12Device6_INTERFACE_DEFINED__
        &IID_ID3D12Device6,
#endif
#ifdef __ID3D12Device7_INTERFACE_DEFINED__
        &IID_ID3D12Device7,
#endif
#ifdef __ID3D12Device8_INTERFACE_DEFINED__
        &IID_ID3D12Device8,
#endif
#ifdef __ID3D12Device9_INTERFACE_DEFINED__
        &IID_ID3D12Device9,
#endif
#ifdef __ID3D12Device10_INTERFACE_DEFINED__
        &IID_ID3D12Device10,
#endif
    };
    static const IID *const list_ids[] = {
        &IID_ID3D12GraphicsCommandList1, &IID_ID3D12GraphicsCommandList2, &IID_ID3D12GraphicsCommandList3,
        &IID_ID3D12GraphicsCommandList4, &IID_ID3D12GraphicsCommandList5, &IID_ID3D12GraphicsCommandList6,
#ifdef __ID3D12GraphicsCommandList7_INTERFACE_DEFINED__
        &IID_ID3D12GraphicsCommandList7,
#endif
    };
    static const D3D12_COMMAND_LIST_TYPE types[] = {
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D12_COMMAND_LIST_TYPE_BUNDLE,
        D3D12_COMMAND_LIST_TYPE_COMPUTE,
        D3D12_COMMAND_LIST_TYPE_COPY
    };
    int t;
    patch_all((IUnknown *)dev, dev_ids, sizeof dev_ids / sizeof *dev_ids, patch_device_vtbl);
    for (int i = 0; i < (int)(sizeof dev_ids / sizeof *dev_ids); i++) {
        void *iface = NULL;
        if (SUCCEEDED(((IUnknown *)dev)->lpVtbl->QueryInterface((IUnknown *)dev, dev_ids[i], &iface)) && iface) {
            patch_device1_vtbl(iface);
            if (i >= 1) patch_device2_vtbl(iface);
            ((IUnknown *)iface)->lpVtbl->Release((IUnknown *)iface);
        }
    }
    for (t = 0; t < 4; t++) {
        ID3D12CommandAllocator *alloc = NULL; ID3D12GraphicsCommandList *list = NULL; ID3D12CommandQueue *queue = NULL;
        if (types[t] != D3D12_COMMAND_LIST_TYPE_BUNDLE) {
            D3D12_COMMAND_QUEUE_DESC qd = { types[t], 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
            if (SUCCEEDED(dev->lpVtbl->CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&queue)) && queue) {
                patch_queue_vtbl(queue); queue->lpVtbl->Release(queue);
            }
        }
        if (FAILED(dev->lpVtbl->CreateCommandAllocator(dev, types[t], &IID_ID3D12CommandAllocator, (void **)&alloc)) || !alloc) continue;
        if (SUCCEEDED(dev->lpVtbl->CreateCommandList(dev, 0, types[t], alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list)) && list) {
            patch_all((IUnknown *)list, list_ids, sizeof list_ids / sizeof *list_ids, patch_list_vtbl);
            list->lpVtbl->Close(list); list->lpVtbl->Release(list);
        }
        alloc->lpVtbl->Release(alloc);
    }
    LOG("device %p patched (timestamp frequency %llu Hz)", (void *)dev, (unsigned long long)qpc_freq);
}

// ---- exports -------------------------------------------------------------------------------

HRESULT WINAPI D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL level, REFIID riid, void **out)
{
    typedef HRESULT (WINAPI *PFN)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
    PFN fn = real_dll ? (PFN)GetProcAddress(real_dll, "D3D12CreateDevice") : NULL;
    HRESULT hr;
    if (!fn) return E_FAIL;
    hr = fn(adapter, level, riid, out);
    if (SUCCEEDED(hr) && out && *out && !idle) {
        ID3D12Device *dev = NULL; IUnknown *u = (IUnknown *)*out;
        if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_ID3D12Device, (void **)&dev)) && dev) { patch_device(dev); dev->lpVtbl->Release(dev); }
    }
    return hr;
}

static PVOID g_veh_world_entry_fix = NULL;
static LONG WINAPI fh4_world_crash_filter(PEXCEPTION_POINTERS ep)
{
    if (ep && ep->ExceptionRecord && ep->ContextRecord) {
        if (ep->ExceptionRecord->ExceptionCode == 0xC000001D /* STATUS_ILLEGAL_INSTRUCTION */) {
            DWORD64 fault_ip = (DWORD64)ep->ExceptionRecord->ExceptionAddress;
            DWORD64 base = (DWORD64)GetModuleHandleA(NULL);
            DWORD64 target_func = base + 0x00D05DA0;
            
            // Check if fault occurred at or within the function entry region [0xD05DA0, 0xD05DE0)
            if (fault_ip >= target_func && fault_ip < target_func + 0x40) {
                DWORD64 *rsp_ptr = (DWORD64*)ep->ContextRecord->Rsp;
                DWORD64 ret_ip = *rsp_ptr;
                
                LOG("FH4_WORLD_ENTRY_FIX: Intercepted illegal instruction at 0x%llx (RVA 0x%llx)! Bypassing to caller 0x%llx",
                    fault_ip, fault_ip - base, ret_ip);
                
                // Advance RIP to caller return address and pop return address from stack:
                ep->ContextRecord->Rip = ret_ip;
                ep->ContextRecord->Rsp += 8;
                ep->ContextRecord->Rax = 0; // return status 0 (success / no error)
                
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        LARGE_INTEGER f;
        self = inst;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&ring_lock);
        InitializeCriticalSection(&trace_lock); trace_lock_ready = 1;
        QueryPerformanceFrequency(&f); qpc_freq = (UINT64)f.QuadPart;
        dbg = GetEnvironmentVariableA("HB_TSSHIM_DEBUG", NULL, 0) > 0;
        { char v[8]; trace_indirect_pso = GetEnvironmentVariableA("HB_TSSHIM_TRACE_INDIRECT_PSO", v, sizeof v) == 1 && v[0] == '1'; }
        { char v[8]; capture_indirect_args = GetEnvironmentVariableA("HB_TSSHIM_CAPTURE_INDIRECT_ARGS", v, sizeof v) == 1 && v[0] == '1'; }
        { char v[8]; g_census_enabled = GetEnvironmentVariableA("HB_TSSHIM_DRAW_CENSUS", v, sizeof v) == 1 && v[0] == '1'; }
        { char v[8]; g_capture_bindings = GetEnvironmentVariableA("HB_TSSHIM_CAPTURE_BINDINGS", v, sizeof v) == 1 && v[0] == '1'; }
        { char v[8]; g_explicit_input_offsets = GetEnvironmentVariableA("HB_TSSHIM_EXPLICIT_INPUT_OFFSETS", v, sizeof v) == 1 && v[0] == '1'; }
        LOG("SHIM_BUILD binding-capture-v5; bindings=%d; input_offsets=%d", g_capture_bindings, g_explicit_input_offsets);
        { char v[16]; if (GetEnvironmentVariableA("HB_TSSHIM_CENSUS_FRAME", v, sizeof v) > 0) { g_census_target_frame = (int)strtol(v, NULL, 0); g_census_enabled = 1; } }
        {
            char skip_buf[512] = {0};
            if (GetEnvironmentVariableA("HB_TSSHIM_SKIP_VS", skip_buf, sizeof skip_buf) > 0) {
                char *token = strtok(skip_buf, ", ");
                while (token && g_num_skip_vs < 32) {
                    g_skip_vs_hashes[g_num_skip_vs++] = (UINT)strtoul(token, NULL, 16);
                    token = strtok(NULL, ", ");
                }
                LOG("HB_TSSHIM_SKIP_VS: loaded %u vertex shader hashes to skip:", g_num_skip_vs);
                for (UINT k = 0; k < g_num_skip_vs; k++) {
                    LOG("  [%u] %08x", k, g_skip_vs_hashes[k]);
                }
            }
        }
        {
            char v[8];
            g_skip_vs_log_enabled = GetEnvironmentVariableA("HB_TSSHIM_SKIP_VS_LOG", v, sizeof v) == 1 && v[0] == '1';
            if (g_skip_vs_log_enabled) {
                LOG("HB_TSSHIM_SKIP_VS_LOG=1: logging first 20 skipped draws with VS hash and PSO index");
            }
        }
        {
            char v[8];
            skip_all_instanced = GetEnvironmentVariableA("HB_TSSHIM_SKIP_ALL_INSTANCED", v, sizeof v) == 1 && v[0] == '1';
            if (skip_all_instanced) {
                LOG("HB_TSSHIM_SKIP_ALL_INSTANCED=1: skipping all DrawInstanced/DrawIndexedInstanced with InstanceCount > 1");
            }
        }
        { char v[8]; capture_indirect_args = GetEnvironmentVariableA("HB_TSSHIM_CAPTURE_INDIRECT_ARGS", v, sizeof v) == 1 && v[0] == '1'; }
        { char v[8]; idle = GetEnvironmentVariableA("HB_D3D12_TSSHIM", v, sizeof v) == 1 && v[0] == '0'; }   // kill switch
        { char v[8]; pass_depth_resolve = GetEnvironmentVariableA("HB_TSSHIM_DEPTHRESOLVE", v, sizeof v) == 1 && v[0] == '1'; }
        { char v[8]; no_gs = GetEnvironmentVariableA("HB_TSSHIM_NOGS", v, sizeof v) == 1 && v[0] == '1'; }
        { char v[8]; no_tess = GetEnvironmentVariableA("HB_TSSHIM_NOTESS", v, sizeof v) == 1 && v[0] == '1'; }
        { char v[8]; no_ei = GetEnvironmentVariableA("HB_TSSHIM_NOEI", v, sizeof v) == 1 && v[0] == '1'; }
        { char v[8]; force_no_fp16 = GetEnvironmentVariableA("HB_TSSHIM_NOFP16", v, sizeof v) == 1 && v[0] == '1'; }
        { char v[16]; if (GetEnvironmentVariableA("HB_TSSHIM_MAXSM", v, sizeof v)) max_sm = (int)strtol(v, NULL, 0); }
        { char v[16]; if (GetEnvironmentVariableA("HB_TSSHIM_TILEDTIER", v, sizeof v)) override_tiled_tier = (int)strtol(v, NULL, 0); }
        { char v[8]; if (override_tiled_tier < 0 && GetEnvironmentVariableA("HB_TSSHIM_NOTILED", v, sizeof v) == 1 && v[0] == '1') override_tiled_tier = 0; }
        if (!load_real()) return FALSE;
        RESOLVE(D3D12CoreCreateLayeredDevice); RESOLVE(D3D12CoreGetLayeredDeviceSize); RESOLVE(D3D12CoreRegisterLayers);
        RESOLVE(D3D12CreateRootSignatureDeserializer); RESOLVE(D3D12CreateVersionedRootSignatureDeserializer);
        RESOLVE(D3D12EnableExperimentalFeatures); RESOLVE(D3D12GetDebugInterface); RESOLVE(D3D12SerializeRootSignature);
        RESOLVE(D3D12SerializeVersionedRootSignature); RESOLVE(GetBehaviorValue);
        {
            char v[8];
            g_hot_skip_enabled = GetEnvironmentVariableA("HB_TSSHIM_HOT_SKIP", v, sizeof v) == 1 && v[0] == '1';
            if (g_hot_skip_enabled) {
                LOG("HB_TSSHIM_HOT_SKIP=1: hot-reloading C:\\highball\\skip.txt every ~30 frames (replaces env-var skip list)");
            }
        }
        {
            char v[8];
            int enable_veh = 1;
            if (GetEnvironmentVariableA("HB_TSSHIM_FIX_WORLD_ENTRY", v, sizeof v) > 0) {
                enable_veh = (v[0] != '0');
            }
            if (enable_veh) {
                g_veh_world_entry_fix = AddVectoredExceptionHandler(1, fh4_world_crash_filter);
                LOG("HB_TSSHIM_FIX_WORLD_ENTRY=1: Registered VEH for 0x140D05DA0 (handle=%p)", g_veh_world_entry_fix);
            }
        }
        LOG("loaded in front of the real d3d12.dll");
    }
    if (reason == DLL_PROCESS_DETACH) {
        if (g_veh_world_entry_fix) {
            RemoveVectoredExceptionHandler(g_veh_world_entry_fix);
            g_veh_world_entry_fix = NULL;
        }
        if (g_census_draws_file) {
            fclose(g_census_draws_file);
            g_census_draws_file = NULL;
        }
    }
    return TRUE;
}
