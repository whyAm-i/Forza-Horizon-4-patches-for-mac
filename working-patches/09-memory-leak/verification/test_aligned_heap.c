#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Stub only the patch's bounded log output. All heap/image operations are real
   APIs in this fixture process; no game, dump or bottle log is needed. */
static BOOL WINAPI no_log_directory(LPCSTR path,LPSECURITY_ATTRIBUTES attributes) {
    (void)path;(void)attributes;return TRUE;
}
static HANDLE WINAPI no_log_file(LPCSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES attributes,DWORD disposition,DWORD flags,HANDLE template_file) {
    (void)path;(void)access;(void)share;(void)attributes;(void)disposition;(void)flags;(void)template_file;return INVALID_HANDLE_VALUE;
}
#define CreateDirectoryA no_log_directory
#define CreateFileA no_log_file
#include "../../02-rendering/source/aligned_heap_fix.h"
#undef CreateDirectoryA
#undef CreateFileA
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s (error=%lu)\n",__LINE__,#x,(unsigned long)GetLastError());ExitProcess(1);} } while(0)
static unsigned checks;
static BYTE *image;
static BOOL fixture_copy_on_write;
static const char *fixture_file;
static DWORD fixture_protection=PAGE_READONLY;
static AhAlloc fixture_alloc;
static AhFree fixture_free;
static void *failed_free_pointer;
static BOOL WINAPI fail_free(HANDLE heap,DWORD flags,LPVOID pointer) {
    (void)heap;(void)flags;failed_free_pointer=pointer;SetLastError(0x9abc);return FALSE;
}
static void *make_aligned(SIZE_T n) {
    void *raw=ah_alloc_impl(ah_heap,0,n,ah_base+AH_ALLOC_RET);CHECK(raw);
    uintptr_t p=ah_align((uintptr_t)raw);((uintptr_t*)p)[-1]=(uintptr_t)raw;
    memset((void*)p,0x5a,n-39);return (void*)p;
}
static void basic_cases(void) {
    const SIZE_T sizes[]={4135,8231,16423,32807};
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<100;j++) {
        void *p=make_aligned(sizes[i]);uintptr_t raw=((uintptr_t*)p)[-1];
        SetLastError(0x1357);CHECK(ah_free_impl(ah_heap,0,p,ah_base+AH_FREE_RET));CHECK(GetLastError()==0x1357);
        CHECK(!ah_lookup((uintptr_t)p));CHECK(!HeapValidate(ah_heap,0,(void*)raw));checks++;
    }
    void *p=make_aligned(4135);uintptr_t raw=((uintptr_t*)p)[-1];
    CHECK(ah_free_impl(ah_heap,0,(void*)raw,ah_base+AH_FREE_RET));CHECK(!ah_lookup((uintptr_t)p));checks++;
    /* Valid raw allocations from other callers remain ordinary frees. */
    void *q=ah_alloc_impl(ah_heap,0,4135,0x1234);CHECK(q);CHECK(!ah_lookup(ah_align((uintptr_t)q)));
    CHECK(ah_free_impl(ah_heap,0,q,0x5678));checks++;
    q=ah_alloc_impl(ah_heap,HEAP_ZERO_MEMORY,4135,ah_base+AH_ALLOC_RET);CHECK(q);
    CHECK(!ah_lookup(ah_align((uintptr_t)q)));CHECK(ah_free_impl(ah_heap,0,q,ah_base+AH_FREE_RET));checks++;
}
static void negative_cases(void) {
    void *p=make_aligned(4135);uintptr_t raw=((uintptr_t*)p)[-1];LONG64 repaired=ah_repaired;
    CHECK(!ah_free_impl(ah_heap,0,p,ah_base+AH_FREE_RET+1));CHECK(HeapValidate(ah_heap,0,(void*)raw));
    HANDLE other=HeapCreate(0,0,0);CHECK(other);
    CHECK(!ah_free_impl(other,0,p,ah_base+AH_FREE_RET));CHECK(HeapValidate(ah_heap,0,(void*)raw));
    CHECK(HeapDestroy(other));CHECK(ah_repaired==repaired);
    CHECK(!ah_free_impl(ah_heap,HEAP_NO_SERIALIZE,p,ah_base+AH_FREE_RET));
    CHECK(HeapValidate(ah_heap,0,(void*)raw));
    ((uintptr_t*)p)[-1]=raw+8;
    CHECK(!ah_free_impl(ah_heap,0,p,ah_base+AH_FREE_RET));CHECK(!ah_lookup((uintptr_t)p));
    CHECK(HeapValidate(ah_heap,0,(void*)raw));CHECK(ah_free_impl(ah_heap,0,(void*)raw,0));checks+=4;
    /* Unknown inaccessible pointers are tested in separate child processes:
       CrossOver's original HeapFree itself faults for this invalid input. */
    /* A real allocation of the wrong size cannot be recovered. */
    void *q=ah_alloc_impl(ah_heap,0,2048+39,ah_base+AH_ALLOC_RET);CHECK(q);
    uintptr_t aligned=ah_align((uintptr_t)q);((uintptr_t*)aligned)[-1]=(uintptr_t)q;
    CHECK(!ah_free_impl(ah_heap,0,(void*)aligned,ah_base+AH_FREE_RET));CHECK(HeapFree(ah_heap,0,q));checks++;
    /* Expired record: do not retry an aligned pointer after a successful free. */
    p=make_aligned(8231);CHECK(ah_free_impl(ah_heap,0,p,ah_base+AH_FREE_RET));repaired=ah_repaired;
    CHECK(!ah_free_impl(ah_heap,0,p,ah_base+AH_FREE_RET));CHECK(ah_repaired==repaired);checks++;
    /* A convincing back-pointer without an observed allocation is insufficient. */
    q=HeapAlloc(ah_heap,0,4135);CHECK(q);aligned=ah_align((uintptr_t)q);((uintptr_t*)aligned)[-1]=(uintptr_t)q;
    CHECK(!ah_free_impl(ah_heap,0,(void*)aligned,ah_base+AH_FREE_RET));CHECK(HeapValidate(ah_heap,0,q));
    CHECK(HeapFree(ah_heap,0,q));checks++;
    /* The live heap's requested size must still match the allocation record. */
    p=make_aligned(4135);raw=((uintptr_t*)p)[-1];
    CHECK(HeapReAlloc(ah_heap,HEAP_REALLOC_IN_PLACE_ONLY,(void*)raw,4100)==(void*)raw);
    CHECK(!ah_free_impl(ah_heap,0,p,ah_base+AH_FREE_RET));CHECK(!ah_lookup((uintptr_t)p));
    CHECK(HeapValidate(ah_heap,0,(void*)raw));CHECK(HeapFree(ah_heap,0,(void*)raw));checks++;
    /* A failed raw release is reported as failure and retains the record. */
    p=make_aligned(4135);raw=((uintptr_t*)p)[-1];AhFree original=ah_original_free;ah_original_free=fail_free;
    repaired=ah_repaired;CHECK(!ah_free_impl(ah_heap,0,p,ah_base+AH_FREE_RET));CHECK(GetLastError()==0x9abc);
    CHECK(failed_free_pointer==(void*)raw&&ah_repaired==repaired&&ah_lookup((uintptr_t)p));
    ah_original_free=original;CHECK(ah_free_impl(ah_heap,0,p,ah_base+AH_FREE_RET));checks++;
    p=make_aligned(4135);raw=((uintptr_t*)p)[-1];ah_enabled=0;
    CHECK(!ah_free_impl(ah_heap,0,p,ah_base+AH_FREE_RET));CHECK(HeapValidate(ah_heap,0,(void*)raw));
    ah_enabled=1;CHECK(ah_free_impl(ah_heap,0,(void*)raw,ah_base+AH_FREE_RET));checks++;
}
static DWORD WINAPI thread_stress(void *value) {
    uintptr_t seed=(uintptr_t)value;
    for(unsigned i=0;i<10000;i++) {
        SIZE_T bytes=((i+seed)&1)?4135:8231;void *p=make_aligned(bytes);
        CHECK(ah_free_impl(ah_heap,0,p,ah_base+AH_FREE_RET));
    }
    return 0;
}
static void saturation_case(void) {
    /* Force all 32 entries in this probe sequence occupied, with unrelated
       keys. A new record must be dropped, never authorize an untracked free. */
    void *raw=HeapAlloc(ah_heap,0,4135);CHECK(raw);uintptr_t p=ah_align((uintptr_t)raw);unsigned h=ah_hash(p);
    AcquireSRWLockExclusive(&ah_lock);
    for(unsigned i=0;i<AH_PROBES;i++)ah_records[(h+i)&(AH_CAPACITY-1)]=(AhRecord){(uintptr_t)0x100+i,0,0,0};
    LONG64 before=ah_dropped;ah_record(ah_heap,raw,4135);CHECK(ah_dropped==before+1);CHECK(!ah_lookup(p));
    for(unsigned i=0;i<AH_PROBES;i++)memset(&ah_records[(h+i)&(AH_CAPACITY-1)],0,sizeof(AhRecord));
    ReleaseSRWLockExclusive(&ah_lock);CHECK(HeapFree(ah_heap,0,raw));checks++;
}
static void prepare_fixture(void) {
    if(fixture_copy_on_write) {
        /* Map our generated inert PE without executing or resolving imports.
           Writes affect this fixture's private mapped pages only. */
        HANDLE file=CreateFileA(fixture_file,GENERIC_READ,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
        HANDLE mapping=CreateFileMappingA(file,NULL,PAGE_READONLY|SEC_IMAGE,0,0,NULL);CHECK(mapping);
        image=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);CHECK(image);
        CloseHandle(mapping);CloseHandle(file);DWORD old;
        CHECK(VirtualProtect(image,4096,PAGE_READWRITE,&old));
        CHECK(VirtualProtect(image+0x37fe000,4096,PAGE_READWRITE,&old));
        CHECK(VirtualProtect(image+0x8820000,4096,PAGE_READWRITE,&old));
    } else {
    image=VirtualAlloc(NULL,0x9b10e00,MEM_RESERVE,PAGE_NOACCESS);CHECK(image);
    CHECK(VirtualAlloc(image,4096,MEM_COMMIT,PAGE_READWRITE));
    CHECK(VirtualAlloc(image+0x37fe000,4096,MEM_COMMIT,PAGE_READWRITE));
    CHECK(VirtualAlloc(image+0x8820000,4096,MEM_COMMIT,PAGE_READWRITE));
    ((IMAGE_DOS_HEADER*)image)->e_lfanew=256;
    }
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)image;dos->e_magic=IMAGE_DOS_SIGNATURE;
    IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(image+dos->e_lfanew);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;nt->FileHeader.TimeDateStamp=0x655e0821;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt->OptionalHeader.SizeOfImage=0x9b10e00;
    memcpy(image+0x37fe7ac,ah_alloc_signature,sizeof(ah_alloc_signature));
    memcpy(image+0x37fe98e,ah_free_signature,sizeof(ah_free_signature));
    *(PVOID*)(image+AH_ALLOC_IAT)=(PVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"),"HeapAlloc");
    *(PVOID*)(image+AH_FREE_IAT)=(PVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"),"HeapFree");
    DWORD old;CHECK(VirtualProtect(image+0x8820000,4096,fixture_protection,&old));
}
static void installer_cases(void) {
    prepare_fixture();uintptr_t base=(uintptr_t)image;IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);
    PVOID alloc=*(PVOID*)(image+AH_ALLOC_IAT),free_fn=*(PVOID*)(image+AH_FREE_IAT);
    nt->FileHeader.TimeDateStamp++;CHECK(ah_install(base)==-1);nt->FileHeader.TimeDateStamp--;checks++;
    CHECK(SetEnvironmentVariableA("HB_TSSHIM_FIX_ALIGNED_HEAP","0"));CHECK(ah_install(base)==-1);
    CHECK(SetEnvironmentVariableA("HB_TSSHIM_FIX_ALIGNED_HEAP",NULL));checks++;
    image[0x37fe7ac]^=1;CHECK(ah_install(base)==0);image[0x37fe7ac]^=1;
    CHECK(*(PVOID*)(image+AH_ALLOC_IAT)==alloc&&*(PVOID*)(image+AH_FREE_IAT)==free_fn);checks++;
    DWORD old;CHECK(VirtualProtect(image+0x8820000,4096,PAGE_READWRITE,&old));
    *(PVOID*)(image+AH_ALLOC_IAT)=(PVOID)0x1234;CHECK(VirtualProtect(image+0x8820000,4096,fixture_protection,&old));
    CHECK(ah_install(base)==-1);CHECK(*(PVOID*)(image+AH_FREE_IAT)==free_fn);
    CHECK(VirtualProtect(image+0x8820000,4096,PAGE_READWRITE,&old));*(PVOID*)(image+AH_ALLOC_IAT)=alloc;
    CHECK(VirtualProtect(image+0x8820000,4096,fixture_protection,&old));checks++;
    BYTE original_page[4096];memcpy(original_page,image+0x8820000,sizeof(original_page));
    CHECK(ah_install(base)==1);CHECK(ah_enabled);CHECK(ah_install(base)==1);
    MEMORY_BASIC_INFORMATION mbi;CHECK(VirtualQuery(image+AH_FREE_IAT,&mbi,sizeof(mbi)));
    CHECK(mbi.Protect==fixture_protection);checks++;
    for(unsigned i=0;i<4096;i++)if(i<0x388||i>=0x398)CHECK(original_page[i]==image[0x8820000+i]);checks++;
    CHECK(ah_writable_protection(PAGE_EXECUTE_WRITECOPY)==PAGE_EXECUTE_READWRITE);
    CHECK(ah_writable_protection(PAGE_EXECUTE_READ)==PAGE_EXECUTE_READWRITE);
    CHECK(!ah_writable_protection(PAGE_READWRITE|PAGE_GUARD)&&!ah_writable_protection(PAGE_NOACCESS));checks++;
}
static void make_call_stub(DWORD ret,DWORD iat) {
    /* Synthetic fixture only: sub rsp,40; call [IAT]; add rsp,40; ret. */
    BYTE *start=image+ret-10;const BYTE prologue[]={0x48,0x83,0xec,0x28,0xff,0x15};
    const BYTE epilogue[]={0x48,0x83,0xc4,0x28,0xc3};
    memcpy(start,prologue,sizeof(prologue));int32_t displacement=(int32_t)(iat-ret);
    memcpy(start+6,&displacement,4);memcpy(image+ret,epilogue,sizeof(epilogue));
}
static void hook_caller_case(void) {
    make_call_stub(AH_ALLOC_RET,AH_ALLOC_IAT);make_call_stub(AH_FREE_RET,AH_FREE_IAT);
    DWORD old;CHECK(VirtualProtect(image+0x37fe000,4096,PAGE_EXECUTE_READ,&old));
    CHECK(FlushInstructionCache(GetCurrentProcess(),image+0x37fe000,4096));
    fixture_alloc=(AhAlloc)(image+AH_ALLOC_RET-10);fixture_free=(AhFree)(image+AH_FREE_RET-10);
    LONG64 before=ah_repaired;void *raw=fixture_alloc(ah_heap,0,4135);CHECK(raw);
    uintptr_t p=ah_align((uintptr_t)raw);((uintptr_t*)p)[-1]=(uintptr_t)raw;
    CHECK(fixture_free(ah_heap,0,(void*)p));CHECK(ah_repaired==before+1);checks++;
}
static LONG CALLBACK fault_exit(EXCEPTION_POINTERS *ep) {
    MEMORY_BASIC_INFORMATION mbi;
    if(VirtualQuery(ep->ExceptionRecord->ExceptionAddress,&mbi,sizeof(mbi))&&
       mbi.AllocationBase==GetModuleHandleA("ntdll.dll"))ExitProcess(77);
    ExitProcess(78);return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc,char **argv) {
    if(argc>1&&!strcmp(argv[1],"--cow-image")) {
        CHECK(argc==3);fixture_file=argv[2];fixture_copy_on_write=TRUE;fixture_protection=PAGE_EXECUTE_WRITECOPY;
    }
    if(argc>1&&!strcmp(argv[1],"--start-guard")) { ah_start();CHECK(!ah_enabled);puts("PASS non-FH4 process guard");return 0; }
    if(argc>1&&(!strcmp(argv[1],"--invalid-original")||!strcmp(argv[1],"--invalid-patched"))) {
        SetErrorMode(SEM_NOGPFAULTERRORBOX);CHECK(AddVectoredExceptionHandler(1,fault_exit));
        ah_original_free=HeapFree;ah_heap=GetProcessHeap();ah_base=0x140000000;ah_enabled=1;
        void *guard=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);CHECK(guard);
        if(!strcmp(argv[1],"--invalid-original"))HeapFree(ah_heap,0,guard);
        else ah_free_impl(ah_heap,0,guard,ah_base+AH_FREE_RET);
        return 79; /* Expected NTDLL fault must match in both children. */
    }
    installer_cases();hook_caller_case();basic_cases();negative_cases();saturation_case();
    HANDLE threads[8];LONG64 before=ah_repaired;ULONGLONG start=GetTickCount64();
    for(uintptr_t i=0;i<8;i++){threads[i]=CreateThread(NULL,0,thread_stress,(void*)i,0,NULL);CHECK(threads[i]);}
    CHECK(WaitForMultipleObjects(8,threads,TRUE,60000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<8;i++)CloseHandle(threads[i]);CHECK(ah_repaired-before==80000);checks++;
    for(unsigned i=0;i<AH_CAPACITY;i++)CHECK(!ah_records[i].aligned);
    printf("PASS %u cases, 80000 concurrent allocation/free pairs in %llu ms; repaired=%lld; bytes=%lld; table=%zu bytes; IAT protection=0x%lx\n",
        checks,GetTickCount64()-start,ah_repaired,ah_bytes,sizeof(ah_records),(unsigned long)fixture_protection);
    return 0;
}
