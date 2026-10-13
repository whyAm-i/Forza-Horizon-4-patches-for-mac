/* Descriptor metadata only; descriptor handles are never dereferenced. */
#define CAPTURE_VIEW_INITIAL_CAPACITY 65536
#define CAPTURE_VIEW_MAX_CAPACITY (4 * 1024 * 1024)
typedef struct CaptureView {
    SIZE_T handle;
    UINT kind, size, format, dimension;
    UINT64 address;
    ID3D12Resource *resource;
} CaptureView;
static CaptureView *capture_views;
static UINT capture_view_capacity, capture_view_count;
typedef struct CaptureHeap {
    UINT64 cpu, gpu;
    UINT count, increment;
    D3D12_DESCRIPTOR_HEAP_TYPE type;
} CaptureHeap;
static CaptureHeap capture_heaps[256];
static UINT capture_heap_count;
static int capture_view_overflow;

static UINT capture_view_hash(SIZE_T handle, UINT capacity)
{
    UINT64 x = (UINT64)handle >> 3;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (UINT)x & (capacity - 1);
}

static int grow_capture_views(void)
{
    UINT capacity = capture_view_capacity ? capture_view_capacity * 2 : CAPTURE_VIEW_INITIAL_CAPACITY;
    if (capacity > CAPTURE_VIEW_MAX_CAPACITY) return 0;
    CaptureView *next = calloc(capacity, sizeof *next);
    if (!next) return 0;
    for (UINT i = 0; i < capture_view_capacity; i++) {
        if (!capture_views[i].handle) continue;
        UINT slot = capture_view_hash(capture_views[i].handle, capacity);
        while (next[slot].handle) slot = (slot + 1) & (capacity - 1);
        next[slot] = capture_views[i];
    }
    free(capture_views); capture_views = next; capture_view_capacity = capacity;
    return 1;
}

static CaptureView *capture_view_slot(SIZE_T handle, int insert)
{
    if (!handle) return NULL;
    if (!capture_view_capacity) {
        if (!insert) return NULL;
        if (!grow_capture_views()) { capture_view_overflow = 1; return NULL; }
    }
    if (insert && capture_view_count >= capture_view_capacity * 3 / 5 && capture_view_capacity < CAPTURE_VIEW_MAX_CAPACITY)
        if (!grow_capture_views()) capture_view_overflow = 1;
    UINT bucket = capture_view_hash(handle, capture_view_capacity);
    for (UINT k = 0; k < 1024; k++) {
        CaptureView *v = &capture_views[(bucket + k) & (capture_view_capacity - 1)];
        if (v->handle == handle) return v;
        if (!v->handle) {
            if (insert) { v->handle = handle; capture_view_count++; return v; }
            return NULL;
        }
    }
    if (insert) capture_view_overflow = 1;
    return NULL;
}

static void capture_view(UINT kind, SIZE_T handle, UINT64 address, UINT size, ID3D12Resource *resource, UINT format, UINT dimension)
{
    EnterCriticalSection(&trace_lock);
    CaptureView *v = capture_view_slot(handle, 1);
    if (v) {
        v->kind = kind; v->address = address; v->size = size;
        v->resource = resource; v->format = format; v->dimension = dimension;
    }
    LeaveCriticalSection(&trace_lock);
}

/* Called with trace_lock held. Scan only the first 32 entries of each table.
 * Actual binding range/register mapping comes from the dumped root signature. */
static void write_table_snapshot(FILE *f, UINT ord, UINT param, UINT64 gpu)
{
    for (UINT h = capture_heap_count; h > 0; h--) {
        CaptureHeap *heap = &capture_heaps[h - 1];
        if (!heap->gpu || !heap->increment || gpu < heap->gpu) continue;
        UINT64 delta = gpu - heap->gpu;
        if (delta % heap->increment || delta / heap->increment >= heap->count) continue;
        UINT first = (UINT)(delta / heap->increment);
        fprintf(f, "# TABLE ord %u | param %u | heap %u | first %u | captured_prefix 32 | overflow %d | tracked %u | capacity %u\n",
            ord, param, h - 1, first, capture_view_overflow, capture_view_count, capture_view_capacity);
        for (UINT k = 0; k < 32 && k < heap->count - first; k++) {
            SIZE_T cpu = (SIZE_T)(heap->cpu + (UINT64)(first + k) * heap->increment);
            CaptureView *v = capture_view_slot(cpu, 0);
            if (!v || !v->kind) continue;
            fprintf(f, "# DESC ord %u | param %u | offset %u | kind %u | address_or_first_element %016llx | size_or_structure_stride %u | resource %p | format %u | dimension %u\n",
                ord, param, k, v->kind, (unsigned long long)v->address,
                v->size, v->resource, v->format, v->dimension);
            /* The first five positions cover the CBV ranges in the three decoded roots.
             * Later descriptors are metadata only, avoiding samples of adjacent tables. */
            if (v->kind == 2 && k < 5) write_upload_sample(f, ord, param, k, v->address, v->size);
        }
        return;
    }
    fprintf(f, "# TABLE ord %u | param %u | unresolved\n", ord, param);
}

static HRESULT (STDMETHODCALLTYPE *real_capture_CreateDescriptorHeap)(ID3D12Device *, const D3D12_DESCRIPTOR_HEAP_DESC *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE hook_capture_CreateDescriptorHeap(ID3D12Device *dev, const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID iid, void **out)
{
    HRESULT hr = real_capture_CreateDescriptorHeap(dev, desc, iid, out);
    if (SUCCEEDED(hr) && desc && out && *out) {
        ID3D12DescriptorHeap *heap = NULL;
        IUnknown *u = *out;
        if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_ID3D12DescriptorHeap, (void **)&heap)) && heap) {
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = { 0 };
            heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap, &cpu);
            D3D12_GPU_DESCRIPTOR_HANDLE gpu = { 0 };
            if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
                heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(heap, &gpu);
            EnterCriticalSection(&trace_lock);
            /* A newly created heap may reuse freed CPU handle addresses. */
            UINT increment = dev->lpVtbl->GetDescriptorHandleIncrementSize(dev, desc->Type);
            UINT64 end = (UINT64)cpu.ptr + (UINT64)desc->NumDescriptors * increment;
            for (UINT i = 0; i < capture_view_capacity; i++) {
                CaptureView *v = &capture_views[i];
                if (v->handle >= cpu.ptr && v->handle < end) v->kind = 0;
            }
            if (capture_heap_count < 256) {
                CaptureHeap *h = &capture_heaps[capture_heap_count++];
                h->cpu = cpu.ptr; h->gpu = gpu.ptr; h->count = desc->NumDescriptors;
                h->increment = increment;
                h->type = desc->Type;
            } else LOG("BINDING_CAPTURE descriptor heap capacity exceeded; metadata incomplete");
            LeaveCriticalSection(&trace_lock);
            heap->lpVtbl->Release(heap);
        }
    }
    return hr;
}

static void (STDMETHODCALLTYPE *real_capture_CreateConstantBufferView)(ID3D12Device *, const D3D12_CONSTANT_BUFFER_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void STDMETHODCALLTYPE hook_capture_CreateConstantBufferView(ID3D12Device *dev, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    real_capture_CreateConstantBufferView(dev, desc, handle);
    capture_view(2, handle.ptr, desc ? desc->BufferLocation : 0, desc ? desc->SizeInBytes : 0, NULL, 0, 0);
}

static void copy_view_metadata(SIZE_T dst, SIZE_T src)
{
    CaptureView *from = capture_view_slot(src, 0);
    CaptureView value = { 0 };
    if (from) value = *from;
    CaptureView *to = capture_view_slot(dst, 1);
    if (to) { *to = value; to->handle = dst; }
}

static void (STDMETHODCALLTYPE *real_capture_CopyDescriptorsSimple)(ID3D12Device *, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE);
static void STDMETHODCALLTYPE hook_capture_CopyDescriptorsSimple(ID3D12Device *dev, UINT count, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    real_capture_CopyDescriptorsSimple(dev, count, dst, src, type);
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) return;
    UINT inc = dev->lpVtbl->GetDescriptorHandleIncrementSize(dev, type);
    EnterCriticalSection(&trace_lock);
    for (UINT i = 0; i < count; i++) copy_view_metadata(dst.ptr + (SIZE_T)i * inc, src.ptr + (SIZE_T)i * inc);
    LeaveCriticalSection(&trace_lock);
}

static void (STDMETHODCALLTYPE *real_capture_CopyDescriptors)(ID3D12Device *, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE *, const UINT *, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE *, const UINT *, D3D12_DESCRIPTOR_HEAP_TYPE);
static void STDMETHODCALLTYPE hook_capture_CopyDescriptors(ID3D12Device *dev, UINT nd, const D3D12_CPU_DESCRIPTOR_HANDLE *dst, const UINT *ds, UINT ns, const D3D12_CPU_DESCRIPTOR_HANDLE *src, const UINT *ss, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    real_capture_CopyDescriptors(dev, nd, dst, ds, ns, src, ss, type);
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || !dst || !src) return;
    UINT inc = dev->lpVtbl->GetDescriptorHandleIncrementSize(dev, type);
    UINT d = 0, s = 0, di = 0, si = 0;
    EnterCriticalSection(&trace_lock);
    while (d < nd && s < ns) {
        UINT dn = ds ? ds[d] : 1, sn = ss ? ss[s] : 1;
        if (di >= dn) { d++; di = 0; continue; }
        if (si >= sn) { s++; si = 0; continue; }
        copy_view_metadata(dst[d].ptr + (SIZE_T)di * inc, src[s].ptr + (SIZE_T)si * inc);
        di++; si++;
    }
    LeaveCriticalSection(&trace_lock);
}

static void patch_descriptor_slots(ID3D12DeviceVtbl *vt)
{
#define DESCRIPTOR_SLOT(Name) patch_slot((void **)&vt->Name, (void *)hook_capture_##Name, (void **)&real_capture_##Name)
    DESCRIPTOR_SLOT(CreateDescriptorHeap);
    DESCRIPTOR_SLOT(CreateConstantBufferView);
    DESCRIPTOR_SLOT(CopyDescriptors);
    DESCRIPTOR_SLOT(CopyDescriptorsSimple);
#undef DESCRIPTOR_SLOT
}
