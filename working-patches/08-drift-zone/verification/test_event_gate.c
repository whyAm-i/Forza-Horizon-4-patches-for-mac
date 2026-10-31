#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
/* Suppress observer log I/O only. Memory, thread and ABI operations below
   use the real Win32 APIs in this test process. */
static BOOL WINAPI test_log_directory(LPCSTR path,LPSECURITY_ATTRIBUTES attrs){(void)path;(void)attrs;return TRUE;}
static HANDLE WINAPI test_log_file(LPCSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES attrs,DWORD disposition,DWORD flags,HANDLE template_file){
    (void)path;(void)access;(void)share;(void)attrs;(void)disposition;(void)flags;(void)template_file;return INVALID_HANDLE_VALUE;
}
#define CreateDirectoryA test_log_directory
#define CreateFileA test_log_file
#include "../../02-rendering/source/drift_event_gate.h"
#undef CreateDirectoryA
#undef CreateFileA

static volatile unsigned forwarded;
static void *expected_object;
static const void *expected_value;
__attribute__((noinline)) static void WINAPI original_probe(void *object,const void *value) {
    assert(object==expected_object&&value==expected_value);forwarded++;
}
static void put(DWORD64 addr,const void *data,SIZE_T size) {
    DWORD old,ignored;assert(VirtualProtect((void*)addr,size,PAGE_EXECUTE_READWRITE,&old));
    memcpy((void*)addr,data,size);assert(VirtualProtect((void*)addr,size,old,&ignored));
    assert(FlushInstructionCache(GetCurrentProcess(),(void*)addr,size));
}
static void make_fixture(void) {
    BYTE *base=VirtualAlloc((void*)0x140000000ULL,0x9b11000,MEM_RESERVE,PAGE_NOACCESS);
    assert(base==(BYTE*)0x140000000ULL);
    assert(VirtualAlloc(base,4096,MEM_COMMIT,PAGE_READWRITE)==base);
    IMAGE_DOS_HEADER *dos=(void*)base;dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    IMAGE_NT_HEADERS64 *nt=(void*)(base+dos->e_lfanew);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;nt->FileHeader.TimeDateStamp=0x655e0821;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt->OptionalHeader.SizeOfImage=0x9b10e00;
    const DWORD pages[]={0x4106000,0x88f000,0x895000,0x5cd0000,0x7e56000};
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++)assert(VirtualAlloc(base+pages[i],4096,MEM_COMMIT,PAGE_READWRITE)==base+pages[i]);
    DWORD64 table[]={0x1440fabc0ULL,0x144106480ULL,0x1440ff1f0ULL};
    memcpy((void*)0x145cd0be0ULL,table,sizeof(table));
    DWORD ignored;
    assert(VirtualProtect(base+0x5cd0000,4096,PAGE_READONLY,&ignored));
    for(unsigned i=0;i<3;i++)assert(VirtualProtect(base+pages[i],4096,PAGE_EXECUTE_READ,&ignored));
    assert(VirtualProtect(base,4096,PAGE_READONLY,&ignored));
}
static void copy_runtime_code(void) {
    /* Minimal recorded compatibility/call-site bytes, in a synthetic private
       fixture. No FH4 executable, dump, or full runtime code page is needed. */
    static const BYTE method[]={0x48,0x8b,0xc4,0x41,0x56,0x48,0x81,0xec,0xa0,0,0,0,0x48,0xc7,0x40,0x88,0xfe,0xff,0xff,0xff};
    static const BYTE single[]={0x48,0x8b,0x01,0x49,0x8b,0xd7,0xff,0x50,0x08};
    static const BYTE broadcast[]={0x48,0x8b,0x0f,0x49,0x8b,0xd7,0x48,0x8b,0x01,0xff,0x50,0x08};
    static const BYTE tail[]={0xe9,0x71,0x5a,0,0};
    put(0x144106480ULL,method,sizeof(method));put(0x144106306ULL,single,sizeof(single));
    put(0x1441063f0ULL,broadcast,sizeof(broadcast));put(0x14088f7daULL,tail,sizeof(tail));
    put(0x140895250ULL,drift_reset_signature,sizeof(drift_reset_signature));
}
static void make_descriptor(unsigned index) {
    DWORD64 s[4]={(DWORD64)drift_events[index].name,0,drift_events[index].length,31};
    put(0x140000000ULL+drift_events[index].rva,s,sizeof(s));
}
typedef void (WINAPI *CallSite)(void*,const void*,DWORD64,void*);
extern int probe_gate_abi(CallSite,void*,const void*,DWORD64,void*);
static DWORD WINAPI finish_unpacking(void *unused) {
    (void)unused;Sleep(350);put(0x140895250ULL,drift_reset_signature,sizeof(drift_reset_signature));return 0;
}
int main(void) {
    assert((DWORD64)GetModuleHandleA(NULL)==0x180000000ULL);
    /* Synthetic fixture: no running game is opened or changed. */
    make_fixture();HMODULE game=(HMODULE)0x140000000ULL;
    DWORD64 *table=(DWORD64*)0x145cd0be0ULL;DWORD64 original_table[3];memcpy(original_table,table,sizeof(original_table));
    /* Refuse encrypted/unready code and explicitly disabled operation. */
    assert(drift_install_event_gate((DWORD64)game)==0);assert(!memcmp(original_table,table,sizeof(original_table)));
    SetEnvironmentVariableA("HB_TSSHIM_FIX_DRIFT_ZONE","0");assert(drift_install_event_gate((DWORD64)game)==-1);
    SetEnvironmentVariableA("HB_TSSHIM_FIX_DRIFT_ZONE",NULL);
    SetEnvironmentVariableA("HB_TSSHIM_FIX_WORLD_ENTRY","0");assert(drift_install_event_gate((DWORD64)game)==-1);
    SetEnvironmentVariableA("HB_TSSHIM_FIX_WORLD_ENTRY","1");
    copy_runtime_code();
    DWORD64 wrong=0;put((DWORD64)&table[1],&wrong,8);assert(drift_install_event_gate((DWORD64)game)==-1);assert(table[1]==0);
    put((DWORD64)table,original_table,sizeof(original_table));
    BYTE reset_original[20];memcpy(reset_original,(void*)0x140895250ULL,sizeof(reset_original));
    BYTE wrong_entry=0x90;put(0x140895250ULL,&wrong_entry,1);
    assert(drift_install_event_gate((DWORD64)game)==0);assert(!memcmp(original_table,table,sizeof(original_table)));assert(!drift_reset_patched);
    /* A still-unknown signature must time out without changing either target. */
    assert(drift_wait_event_gate((DWORD64)game,1)==0);assert(*(BYTE*)0x140895250ULL==wrong_entry);assert(!memcmp(original_table,table,sizeof(original_table)));
    put(0x140895250ULL,reset_original,sizeof(reset_original));
    MEMORY_BASIC_INFORMATION code_before,code_after;assert(VirtualQuery((void*)0x140895250ULL,&code_before,sizeof(code_before)));
    MEMORY_BASIC_INFORMATION before,after;assert(VirtualQuery(table,&before,sizeof(before)));
    /* Reproduce the real startup transition: ordinary code is ready but the
       reset page still has a transient signature, then becomes ready later. */
    put(0x140895250ULL,&wrong_entry,1);HANDLE unpacker=CreateThread(NULL,0,finish_unpacking,NULL,0,NULL);assert(unpacker);
    assert(drift_wait_event_gate((DWORD64)game,8)==1);assert(WaitForSingleObject(unpacker,1000)==WAIT_OBJECT_0);CloseHandle(unpacker);
    assert(table[0]==original_table[0]&&table[2]==original_table[2]);
    assert(table[1]==(DWORD64)drift_method_gate);assert(VirtualQuery(table,&after,sizeof(after)));assert(after.Protect==before.Protect);
    assert(*(BYTE*)0x140895250ULL==0xc3);assert(!memcmp((BYTE*)0x140895251ULL,reset_original+1,19));assert(drift_reset_patched);
    assert(VirtualQuery((void*)0x140895250ULL,&code_after,sizeof(code_after)));assert(code_after.Protect==code_before.Protect);
    assert(drift_install_event_gate((DWORD64)game)==1);
    for(unsigned i=0;i<4;i++)make_descriptor(i);
    drift_original_method=original_probe;

    /* Real recorded virtual-call instructions at their observed VAs. Only the
       test image's surrounding code is replaced by bounded ABI fixtures. */
    BYTE single[]={0x55,0x41,0x57,0x48,0x83,0xec,0x28,0x4c,0x89,0xc5,0x49,0x89,0xd7,0xe9,0,0,0,0};
    int32_t rel=(int32_t)(0x144106306ULL-(0x1441062e0ULL+sizeof(single)));memcpy(single+14,&rel,4);put(0x1441062e0ULL,single,sizeof(single));
    /* Keep the recorded LEA continuation; then return through our test prologue. */
    BYTE single_end[]={0x48,0x8d,0x4c,0x24,0x20,0x48,0x83,0xc4,0x28,0x41,0x5f,0x5d,0xc3};put(0x14410630fULL,single_end,sizeof(single_end));
    BYTE broadcast[]={0x55,0x41,0x57,0x57,0x48,0x81,0xec,0x80,0,0,0,
        0x4c,0x89,0x44,0x24,0x70,0x4c,0x89,0xcf,0x49,0x89,0xd7,
        0x48,0xc7,0xc5,0x34,0x12,0,0,0xe9,0,0,0,0};
    rel=(int32_t)(0x1441063f0ULL-(0x1441063a0ULL+sizeof(broadcast)));memcpy(broadcast+30,&rel,4);put(0x1441063a0ULL,broadcast,sizeof(broadcast));
    BYTE broadcast_end[]={0x48,0x81,0xc4,0x80,0,0,0,0x5f,0x41,0x5f,0x5d,0xc3};put(0x1441063fcULL,broadcast_end,sizeof(broadcast_end));
    struct {DWORD64 *vptr;BYTE enabled;} object={table,1};int value=42;
    expected_object=&object;expected_value=&value;void *one=&object;
    assert(!probe_gate_abi((CallSite)0x140895250ULL,&object,&value,0,&one));
    /* Execute the real abort routine's recorded tail-jump instruction, which
       reaches reset directly rather than through the notification vtable. */
    assert(!probe_gate_abi((CallSite)0x14088f7daULL,&object,&value,0,&one));
    CallSite a=(CallSite)0x1441062e0ULL,b=(CallSite)0x1441063a0ULL;
    for(unsigned i=0;i<4;i++) {
        assert(!probe_gate_abi(a,&object,&value,0x140000000ULL+drift_events[i].rva,&one));
        assert(!probe_gate_abi(b,&object,&value,0x140000000ULL+drift_events[i].rva,&one));
        assert(drift_event_counts[i]==2);assert(forwarded==(i+1)*2);
    }
    /* Named drift callbacks, activation, unrelated names and unknown callers
       all reach the original method once with identical arguments. */
    unsigned baseline=forwarded;assert(baseline==8);
    assert(!probe_gate_abi(a,&object,&value,0x147e56738ULL,&one));assert(!probe_gate_abi(b,&object,&value,0x147e567b0ULL,&one));assert(forwarded==baseline+2);
    ((DriftMethod)table[1])(&object,&value);assert(forwarded==baseline+3);
    DWORD64 clone[4]={(DWORD64)drift_events[0].name,0,18,31};a(&object,&value,(DWORD64)clone,&one);assert(forwarded==baseline+4);
    DWORD64 bad_length=999;put(0x147e56770ULL,&bad_length,8);a(&object,&value,0x147e56760ULL,&one);assert(forwarded==baseline+5);make_descriptor(0);
    const char bad_text[]="DriftZoneCompleteX";DWORD64 bad_pointer=(DWORD64)bad_text;put(0x147e56760ULL,&bad_pointer,8);a(&object,&value,0x147e56760ULL,&one);assert(forwarded==baseline+6);make_descriptor(0);
    assert(drift_event_from_call(0x147e56760ULL,(DWORD64*)0x140001000ULL)==-1);
    assert(drift_event_from_call(0x147e56760ULL,(DWORD64*)1)==-1);
    drift_original_method=(DriftMethod)original_table[1];drift_remove_event_gate();assert(!memcmp(original_table,table,sizeof(original_table)));
    assert(!memcmp((void*)0x140895250ULL,reset_original,20));assert(!drift_reset_patched);
    assert(VirtualQuery((void*)0x140895250ULL,&code_after,sizeof(code_after)));assert(code_after.Protect==code_before.Protect);
    puts("PASS: all four named drift callbacks and both dispatch paths forward exactly once with original args and preserved registers; unrelated/invalid event calls also forward; startup readiness/timeout, entry/vtable restoration, protections and direct-reset/tail-jump ABI tests pass.");
    assert(VirtualFree((void*)game,0,MEM_RELEASE));
    return 0;
}
