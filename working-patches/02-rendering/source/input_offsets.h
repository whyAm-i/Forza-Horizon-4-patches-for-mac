/* Resolve APPEND offsets before D3DMetal interprets packed vertex formats.
 * Opt-in: HB_TSSHIM_EXPLICIT_INPUT_OFFSETS=1. App-owned descriptors stay intact. */
#define INPUT_OFFSET_LIMIT 32
static UINT input_format_bytes(DXGI_FORMAT f)
{
    /* Uncompressed DXGI format groups. Unsupported formats are forwarded. */
    if (f >= DXGI_FORMAT_R32G32B32A32_TYPELESS && f <= DXGI_FORMAT_R32G32B32A32_SINT) return 16;
    if (f >= DXGI_FORMAT_R32G32B32_TYPELESS && f <= DXGI_FORMAT_R32G32B32_SINT) return 12;
    if (f >= DXGI_FORMAT_R16G16B16A16_TYPELESS && f <= DXGI_FORMAT_X32_TYPELESS_G8X24_UINT) return 8;
    if (f >= DXGI_FORMAT_R10G10B10A2_TYPELESS && f <= DXGI_FORMAT_X24_TYPELESS_G8_UINT) return 4;
    if (f >= DXGI_FORMAT_R8G8_TYPELESS && f <= DXGI_FORMAT_R16_SINT) return 2;
    if (f >= DXGI_FORMAT_R8_TYPELESS && f <= DXGI_FORMAT_A8_UNORM) return 1;
    if (f == DXGI_FORMAT_R9G9B9E5_SHAREDEXP || f == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) return 4;
    return 0;
}

/* Returns true only for a fully validated layout containing APPEND entries. */
static int explicit_input_layout(const D3D12_INPUT_LAYOUT_DESC *in,
                                D3D12_INPUT_ELEMENT_DESC elements[INPUT_OFFSET_LIMIT],
                                D3D12_INPUT_LAYOUT_DESC *out)
{
    UINT cursor[32] = {0};
    int changed = 0;
    if (!g_explicit_input_offsets || !in || !in->pInputElementDescs ||
        !in->NumElements || in->NumElements > INPUT_OFFSET_LIMIT) return 0;
    for (UINT i = 0; i < in->NumElements; i++) {
        const D3D12_INPUT_ELEMENT_DESC *e = &in->pInputElementDescs[i];
        UINT size = input_format_bytes(e->Format), offset = e->AlignedByteOffset;
        if (!size || e->InputSlot >= 32) return 0;
        if (offset == D3D12_APPEND_ALIGNED_ELEMENT) {
            UINT alignment = size < 4 ? size : 4;
            if (cursor[e->InputSlot] > UINT_MAX - (alignment - 1)) return 0;
            offset = (cursor[e->InputSlot] + alignment - 1) & ~(alignment - 1);
            changed = 1;
        }
        if (offset > UINT_MAX - size) return 0;
        cursor[e->InputSlot] = offset + size;
        elements[i] = *e;
        elements[i].AlignedByteOffset = offset;
    }
    if (!changed) return 0;
    *out = *in;
    out->pInputElementDescs = elements;
    return 1;
}

static int input_stream_payload(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, SIZE_T *size, SIZE_T *alignment)
{
#define PAYLOAD(T) do { *size = sizeof(T); *alignment = __alignof__(T); return 1; } while (0)
    switch (type) {
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: PAYLOAD(ID3D12RootSignature *);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: PAYLOAD(D3D12_SHADER_BYTECODE);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: PAYLOAD(D3D12_STREAM_OUTPUT_DESC);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: PAYLOAD(D3D12_BLEND_DESC);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: PAYLOAD(UINT);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: PAYLOAD(D3D12_RASTERIZER_DESC);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: PAYLOAD(D3D12_DEPTH_STENCIL_DESC);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: PAYLOAD(D3D12_DEPTH_STENCIL_DESC1);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: PAYLOAD(D3D12_INPUT_LAYOUT_DESC);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: PAYLOAD(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: PAYLOAD(D3D12_PRIMITIVE_TOPOLOGY_TYPE);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: PAYLOAD(struct D3D12_RT_FORMAT_ARRAY);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: PAYLOAD(DXGI_FORMAT);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: PAYLOAD(DXGI_SAMPLE_DESC);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: PAYLOAD(D3D12_CACHED_PIPELINE_STATE);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: PAYLOAD(D3D12_PIPELINE_STATE_FLAGS);
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: PAYLOAD(D3D12_VIEW_INSTANCING_DESC);
    default: return 0;
    }
#undef PAYLOAD
}

/* Validates the entire stream before copying it. Caller frees owned after forwarding. */
static int explicit_input_stream(const D3D12_PIPELINE_STATE_STREAM_DESC *in,
                                D3D12_INPUT_ELEMENT_DESC elements[INPUT_OFFSET_LIMIT],
                                D3D12_PIPELINE_STATE_STREAM_DESC *out, void **owned)
{
    SIZE_T off = 0, layout_offset = 0;
    int found = 0;
    D3D12_INPUT_LAYOUT_DESC layout, resolved;
    const BYTE *bytes;
    *owned = NULL;
    if (!g_explicit_input_offsets || !in || !in->pPipelineStateSubobjectStream ||
        !in->SizeInBytes || in->SizeInBytes > 1024 * 1024) return 0;
    bytes = in->pPipelineStateSubobjectStream;
    while (off < in->SizeInBytes) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        SIZE_T size, alignment, payload, end;
        if (in->SizeInBytes - off < sizeof(type)) return 0;
        memcpy(&type, bytes + off, sizeof(type));
        if (!input_stream_payload(type, &size, &alignment)) return 0;
        payload = (off + sizeof(type) + alignment - 1) & ~(alignment - 1);
        end = (payload + size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
        if (end > in->SizeInBytes) return 0;
        if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT) {
            if (found) return 0;
            found = 1; layout_offset = payload;
            memcpy(&layout, bytes + payload, sizeof(layout));
        }
        off = end;
    }
    if (!found || !explicit_input_layout(&layout, elements, &resolved)) return 0;
    *owned = malloc(in->SizeInBytes);
    if (!*owned) return 0;
    memcpy(*owned, bytes, in->SizeInBytes);
    memcpy((BYTE *)*owned + layout_offset, &resolved, sizeof(resolved));
    *out = *in; out->pPipelineStateSubobjectStream = *owned;
    return 1;
}
