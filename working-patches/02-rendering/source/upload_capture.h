/* Census-only samples of CPU UPLOAD memory. No GPU work or retained COM references.
 * Release and QueryInterface hooks keep weak resource aliases valid under trace_lock.
 * Files describe the first CPU recording sample, not GPU execution-time contents. */
#define UPLOAD_RESOURCE_LIMIT 4096
#define UPLOAD_ALIAS_LIMIT 16
#define UPLOAD_VTABLE_LIMIT 64
#define UPLOAD_SAMPLE_LIMIT 2048
#define UPLOAD_SAMPLE_BYTES_LIMIT (8 * 1024 * 1024)
typedef struct UploadResource {
    ID3D12Resource *resource;
    UINT64 address, width;
    IUnknown *aliases[UPLOAD_ALIAS_LIMIT];
    UINT alias_count;
    int readable;
} UploadResource;
typedef struct UploadVtable {
    IUnknownVtbl *vtable;
    HRESULT (STDMETHODCALLTYPE *query)(IUnknown *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *release)(IUnknown *);
} UploadVtable;
typedef struct UploadSample {
    UINT64 address;
    UINT size;
    char file[96], status[48];
} UploadSample;
static UploadResource upload_resources[UPLOAD_RESOURCE_LIMIT];
static UINT upload_resource_count;
static UploadVtable upload_vtables[UPLOAD_VTABLE_LIMIT];
static UINT upload_vtable_count;
static UploadSample upload_samples[UPLOAD_SAMPLE_LIMIT];
static UINT upload_sample_count, upload_sample_bytes, upload_capture_epoch;

static UploadResource *find_upload_alias(IUnknown *object)
{
    for (UINT i = 0; i < upload_resource_count; i++) {
        UploadResource *r = &upload_resources[i];
        if (!r->resource) continue;
        for (UINT a = 0; a < r->alias_count; a++)
            if (r->aliases[a] == object) return r;
    }
    return NULL;
}
static UploadVtable *find_upload_vtable(IUnknownVtbl *vtable)
{
    for (UINT i = 0; i < upload_vtable_count; i++)
        if (upload_vtables[i].vtable == vtable) return &upload_vtables[i];
    return NULL;
}
static HRESULT STDMETHODCALLTYPE hook_upload_QueryInterface(IUnknown *, REFIID, void **);
static ULONG STDMETHODCALLTYPE hook_upload_Release(IUnknown *);

/* Lock held; a failure disables reads before an untracked alias can escape. */
static void register_upload_alias(UploadResource *r, IUnknown *object)
{
    if (!object) { r->readable = 0; return; }
    for (UINT i = 0; i < r->alias_count; i++) if (r->aliases[i] == object) return;
    if (r->alias_count == UPLOAD_ALIAS_LIMIT) { r->readable = 0; return; }
    IUnknownVtbl *vt = (IUnknownVtbl *)object->lpVtbl;
    UploadVtable *saved = find_upload_vtable(vt);
    if (!saved) {
        if (upload_vtable_count == UPLOAD_VTABLE_LIMIT) { r->readable = 0; return; }
        saved = &upload_vtables[upload_vtable_count++];
        saved->vtable = vt;
        saved->query = vt->QueryInterface;
        saved->release = vt->Release;
        patch_slot((void **)&vt->QueryInterface, (void *)hook_upload_QueryInterface, (void **)&saved->query);
        patch_slot((void **)&vt->Release, (void *)hook_upload_Release, (void **)&saved->release);
    }
    r->aliases[r->alias_count++] = object;
}

static HRESULT STDMETHODCALLTYPE hook_upload_QueryInterface(IUnknown *object, REFIID iid, void **out)
{
    EnterCriticalSection(&trace_lock);
    UploadVtable *saved = find_upload_vtable((IUnknownVtbl *)object->lpVtbl);
    HRESULT (STDMETHODCALLTYPE *query)(IUnknown *, REFIID, void **) = saved ? saved->query : NULL;
    LeaveCriticalSection(&trace_lock);
    if (!query) return E_NOINTERFACE;
    HRESULT hr = query(object, iid, out);
    if (SUCCEEDED(hr) && out && *out) {
        EnterCriticalSection(&trace_lock);
        UploadResource *r = find_upload_alias(object);
        if (r) register_upload_alias(r, *out);
        LeaveCriticalSection(&trace_lock);
    }
    return hr;
}

static ULONG STDMETHODCALLTYPE hook_upload_Release(IUnknown *object)
{
    EnterCriticalSection(&trace_lock);
    UploadVtable *saved = find_upload_vtable((IUnknownVtbl *)object->lpVtbl);
    ULONG (STDMETHODCALLTYPE *release)(IUnknown *) = saved ? saved->release : NULL;
    UploadResource *r = find_upload_alias(object);
    if (!r) {
        LeaveCriticalSection(&trace_lock);
        return release ? release(object) : 0;
    }
    /* Keep destruction mutually exclusive with Map/copy/Unmap. No use after real Release. */
    ULONG refs = release(object);
    if (!refs) memset(r, 0, sizeof *r);
    LeaveCriticalSection(&trace_lock);
    return refs;
}

static void register_upload_resource(ID3D12Resource *resource, IUnknown *original, UINT64 address, UINT64 width)
{
    if (!address || !width) return;
    EnterCriticalSection(&trace_lock);
    UploadResource *r = find_upload_alias((IUnknown *)resource);
    if (!r) {
        UINT i;
        for (i = 0; i < upload_resource_count; i++) if (!upload_resources[i].resource) break;
        if (i == UPLOAD_RESOURCE_LIMIT) {
            LeaveCriticalSection(&trace_lock);
            LOG("UPLOAD_CAPTURE resource capacity exceeded; new allocation unreadable");
            return;
        }
        if (i == upload_resource_count) upload_resource_count++;
        r = &upload_resources[i];
        memset(r, 0, sizeof *r);
        r->resource = resource; r->address = address; r->width = width; r->readable = 1;
        register_upload_alias(r, (IUnknown *)resource);
        register_upload_alias(r, original);
    }
    LeaveCriticalSection(&trace_lock);
}

static void reset_upload_samples(void)
{
    upload_capture_epoch++;
    upload_sample_count = upload_sample_bytes = 0;
    memset(upload_samples, 0, sizeof upload_samples);
}

/* Caller holds trace_lock. Never map DEFAULT, READBACK or untracked memory. */
static void write_upload_sample(FILE *f, UINT ord, UINT param, UINT table_offset, UINT64 address, UINT size)
{
    UploadSample *sample = NULL;
    for (UINT i = 0; i < upload_sample_count; i++)
        if (upload_samples[i].address == address && upload_samples[i].size == size) { sample = &upload_samples[i]; break; }
    if (!sample) {
        if (upload_sample_count == UPLOAD_SAMPLE_LIMIT) {
            fprintf(f, "# CB_DATA ord %u | param %u | offset %u | status sample_limit\n", ord, param, table_offset);
            return;
        }
        sample = &upload_samples[upload_sample_count++];
        sample->address = address; sample->size = size;
        strcpy(sample->status, "not_tracked_upload");
        UploadResource *r = NULL;
        for (UINT i = upload_resource_count; i > 0; i--) {
            UploadResource *candidate = &upload_resources[i - 1];
            if (candidate->resource && candidate->readable && address >= candidate->address &&
                address - candidate->address <= candidate->width && size <= candidate->width - (address - candidate->address)) {
                r = candidate; break;
            }
        }
        if (!size || size > 65536) strcpy(sample->status, "invalid_size");
        else if (upload_sample_bytes + size > UPLOAD_SAMPLE_BYTES_LIMIT) strcpy(sample->status, "byte_limit");
        else if (r) {
            SIZE_T offset = (SIZE_T)(address - r->address);
            D3D12_RANGE range = { offset, offset + size }, written = { 0, 0 };
            void *mapped = NULL;
            HRESULT hr = r->resource->lpVtbl->Map(r->resource, 0, &range, &mapped);
            if (FAILED(hr)) snprintf(sample->status, sizeof sample->status, "map_failed_%08lx", (unsigned long)hr);
            else {
                BYTE *copy = mapped ? malloc(size) : NULL;
                if (copy) memcpy(copy, (BYTE *)mapped + offset, size);
                r->resource->lpVtbl->Unmap(r->resource, 0, &written);
                if (!copy) strcpy(sample->status, mapped ? "allocation_failed" : "null_map");
                else {
                    char path[MAX_PATH];
                    CreateDirectoryA("C:\\highball\\cbdata", NULL);
                    snprintf(sample->file, sizeof sample->file, "cb-%lu-%u-%u.bin", (unsigned long)GetCurrentProcessId(), upload_capture_epoch, upload_sample_count);
                    snprintf(path, sizeof path, "C:\\highball\\cbdata\\%s", sample->file);
                    FILE *data = fopen(path, "wb");
                    int okay = 0;
                    if (data) {
                        size_t count = fwrite(copy, 1, size, data);
                        int close_result = fclose(data);
                        okay = count == size && !close_result;
                    }
                    free(copy);
                    if (okay) { strcpy(sample->status, "cpu_upload_first_sample"); upload_sample_bytes += size; }
                    else { strcpy(sample->status, "file_failed"); sample->file[0] = 0; }
                }
            }
        }
    }
    fprintf(f, "# CB_DATA ord %u | param %u | offset %u | address %016llx | size %u | status %s | file %s\n",
        ord, param, table_offset, (unsigned long long)address, size, sample->status, sample->file[0] ? sample->file : "none");
}
