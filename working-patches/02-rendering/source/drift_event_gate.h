#ifndef FH4_DRIFT_EVENT_GATE_H
#define FH4_DRIFT_EVENT_GATE_H
#include <intrin.h>

/* FH4 1.478.564.0: observe reflective drift-finish listeners without blocking
   them, and return at the recorded failing reset entry before its body runs.
   Runtime changes: one vtable slot and one guarded entry byte. No EXE file edit. */
typedef void (WINAPI *DriftMethod)(void *, const void *);
static DriftMethod drift_original_method;
static DWORD64 drift_event_base;
static volatile LONG drift_event_installed, drift_event_started;
static volatile LONG drift_event_thread, drift_event_tick;
static volatile LONG drift_event_counts[4];
static volatile LONG drift_event_records;
static volatile LONG drift_reset_patched;
static DWORD drift_reset_protection;
static const BYTE drift_reset_signature[]={0x5c,0x7d,0x55,0x39,0xd9,0x7d,0xb9,0x03,0x21,0x33,0x1a,0x5b,0xff,0x33,0x2b,0xd9,0x21,0x8d,0xcf,0x60};
typedef struct { DWORD rva; const char *name; SIZE_T length; } DriftEvent;
static const DriftEvent drift_events[4]={
    {0x7e56760,"DriftZoneCompleted",18},
    {0x7e56788,"DriftZoneAborted",16},
    {0x7e567d8,"AnyDriftZoneCompleted",21},
    {0x7e56800,"AnyDriftZoneAborted",19}
};
static BOOL drift_event_read(DWORD64 address,void *data,SIZE_T size) {
    SIZE_T read=0;return ReadProcessMemory(GetCurrentProcess(),(void*)address,data,size,&read)&&read==size;
}
static BOOL drift_event_bytes(DWORD64 address,const BYTE *expected,SIZE_T size) {
    BYTE data[64];return size<=sizeof(data)&&drift_event_read(address,data,size)&&memcmp(data,expected,size)==0;
}
static void drift_event_log(const char *message) {
    char path[MAX_PATH],line[512];SYSTEMTIME t;GetLocalTime(&t);
    CreateDirectoryA("C:\\highball",NULL);CreateDirectoryA("C:\\highball\\logs",NULL);
    snprintf(path,sizeof(path),"C:\\highball\\logs\\drift-event-gate-%lu.txt",(unsigned long)GetCurrentProcessId());
    int n=snprintf(line,sizeof(line),"%02u:%02u:%02u.%03u FH4_DRIFT_OBSERVER v3: %s\r\n",t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,message);
    if(n<=0||n>=(int)sizeof(line))return;
    HANDLE f=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f!=INVALID_HANDLE_VALUE){DWORD written;WriteFile(f,line,(DWORD)n,&written,NULL);CloseHandle(f);}
}
static BOOL drift_restore_reset_entry(void) {
    if(!InterlockedCompareExchange(&drift_reset_patched,0,0))return TRUE;
    volatile CHAR *entry=(volatile CHAR*)(drift_event_base+0x895250);BYTE current;
    if(!drift_event_read((DWORD64)entry,&current,1)||current!=0xc3) {
        drift_event_log("reset entry changed after installation; original byte not forced over it");return FALSE;
    }
    DWORD old,ignored;
    if(!VirtualProtect((void*)entry,1,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    CHAR previous=_InterlockedCompareExchange8(entry,(CHAR)drift_reset_signature[0],(CHAR)0xc3);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),(void*)entry,1);
    BOOL protected=VirtualProtect((void*)entry,1,drift_reset_protection,&ignored);
    if((BYTE)previous!=0xc3||!flushed||!protected)return FALSE;
    InterlockedExchange(&drift_reset_patched,0);return TRUE;
}
static BOOL drift_patch_reset_entry(void) {
    DWORD64 address=drift_event_base+0x895250;
    if(!drift_event_bytes(address,drift_reset_signature,sizeof(drift_reset_signature))) {
        drift_event_log("reset entry signature mismatch; no runtime patches installed");return FALSE;
    }
    DWORD old,ignored;
    if(!VirtualProtect((void*)address,1,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    drift_reset_protection=old;
    CHAR previous=_InterlockedCompareExchange8((volatile CHAR*)address,(CHAR)0xc3,(CHAR)drift_reset_signature[0]);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),(void*)address,1);
    BOOL protected=VirtualProtect((void*)address,1,old,&ignored);
    if((BYTE)previous!=drift_reset_signature[0])return FALSE;
    InterlockedExchange(&drift_reset_patched,1);
    if(!flushed||!protected) {
        drift_restore_reset_entry();drift_event_log("reset entry flush/protection failed; installation declined");return FALSE;
    }
    return TRUE;
}
/* Exact descriptor identity plus bounded content verification. No arbitrary strings. */
static int drift_event_index(DWORD64 name) {
    for(int i=0;i<4;i++)if(name==drift_event_base+drift_events[i].rva) {
        DWORD64 s[4];char text[24];const DriftEvent *e=&drift_events[i];
        if(!drift_event_read(name,s,sizeof(s))||s[2]!=e->length||s[3]<e->length||s[3]>4096)return -1;
        DWORD64 chars=s[3]<16?name:s[0];
        if(!drift_event_read(chars,text,e->length+1)||text[e->length]||memcmp(text,e->name,e->length))return -1;
        return i;
    }
    return -1;
}
static int drift_event_from_call(DWORD64 caller_rbp,const DWORD64 *return_slot) {
    NT_TIB *tib=(NT_TIB*)NtCurrentTeb();DWORD64 p=(DWORD64)return_slot;
    DWORD64 low=(DWORD64)tib->StackLimit,high=(DWORD64)tib->StackBase;
    if((p&7)||p<low||p>=high||high-p<8)return -1;
    DWORD64 ret=*return_slot,name;
    if(ret==drift_event_base+0x410630f)name=caller_rbp;
    else if(ret==drift_event_base+0x41063fc) {
        /* Broadcast loop reuses RBP for its copied listener vector. The caller
           at 0x40ECF7A preserves the descriptor in RSI; the dispatcher's prologue
           saves that RSI at body RSP+0x70, i.e. return-slot+0x78. */
        if(high-p<0x80)return -1;
        name=*(const DWORD64*)(p+0x78);
    } else return -1;
    return drift_event_index(name);
}
__attribute__((used,noinline)) static void WINAPI drift_method_gate_impl(
    void *object,const void *value,DWORD64 caller_rbp,const DWORD64 *return_slot) {
    int event=drift_event_from_call(caller_rbp,return_slot);
    if(event>=0) {
        InterlockedExchange(&drift_event_thread,(LONG)GetCurrentThreadId());
        InterlockedExchange(&drift_event_tick,(LONG)GetTickCount());
        LONG count=InterlockedIncrement(&drift_event_counts[event]);
        if(count<=32&&InterlockedIncrement(&drift_event_records)<=128) {
            char line[256];snprintf(line,sizeof(line),"forwarding MethodObjectInfo for %s; count=%ld; caller=%llx; object=%p; tid=%lu",
                drift_events[event].name,count,*return_slot,object,(unsigned long)GetCurrentThreadId());
            drift_event_log(line);
        }
    }
    /* Every notification, including the four observed drift events, reaches
       its original method exactly once with the same two arguments. */
    drift_original_method(object,value);
}
__attribute__((naked)) static void WINAPI drift_method_gate(void *object,const void *value) {
    __asm__ volatile("mov %rbp, %r8\n\tmov %rsp, %r9\n\tjmp drift_method_gate_impl\n\t");
}
/* 1=installed, 0=code not decrypted yet, -1=disabled/unsupported/installation error. */
static int drift_install_event_gate(DWORD64 base) {
    if(InterlockedCompareExchange(&drift_event_installed,0,0))return 1;
    char env[8];
    if((GetEnvironmentVariableA("HB_TSSHIM_FIX_DRIFT_ZONE",env,sizeof(env))>0&&env[0]=='0')||
       (GetEnvironmentVariableA("HB_TSSHIM_FIX_WORLD_ENTRY",env,sizeof(env))>0&&env[0]=='0'))return -1;
    IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    if(!drift_event_read(base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>0x10000||
       !drift_event_read(base+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.FileHeader.TimeDateStamp!=0x655e0821||
       nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt.OptionalHeader.SizeOfImage!=0x9b10e00) {
        drift_event_log("unsupported executable; no hook installed");return -1;
    }
    static const BYTE method[]={0x48,0x8b,0xc4,0x41,0x56,0x48,0x81,0xec,0xa0,0,0,0,0x48,0xc7,0x40,0x88,0xfe,0xff,0xff,0xff};
    static const BYTE single[]={0x48,0x8b,0x01,0x49,0x8b,0xd7,0xff,0x50,0x08};
    static const BYTE broadcast[]={0x48,0x8b,0x0f,0x49,0x8b,0xd7,0x48,0x8b,0x01,0xff,0x50,0x08};
    if(!drift_event_bytes(base+0x4106480,method,sizeof(method))||
       !drift_event_bytes(base+0x4106306,single,sizeof(single))||
       !drift_event_bytes(base+0x41063f0,broadcast,sizeof(broadcast)))return 0;
    /* The live code can pass through more than one unpacking state. Refuse
       mutation until the full recorded runtime signature is present, but keep
       waiting rather than permanently aborting on a transitional signature. */
    if(!drift_event_bytes(base+0x895250,drift_reset_signature,sizeof(drift_reset_signature)))return 0;
    DWORD64 table[3];
    if(!drift_event_read(base+0x5cd0be0,table,sizeof(table))||
       table[0]!=base+0x40fabc0||table[1]!=base+0x4106480||table[2]!=base+0x40ff1f0) {
        drift_event_log("MethodObjectInfo vtable mismatch; no hook installed");return -1;
    }
    drift_event_base=base;
    if(!drift_patch_reset_entry()) {
        if(InterlockedCompareExchange(&drift_reset_patched,0,0))return -1;
        return drift_event_bytes(base+0x895250,drift_reset_signature,sizeof(drift_reset_signature))?-1:0;
    }
    PVOID volatile *slot=(PVOID volatile *)(base+0x5cd0be8);DWORD old;
    if(!VirtualProtect((void*)slot,sizeof(*slot),PAGE_READWRITE,&old)) {
        drift_restore_reset_entry();drift_event_log("cannot make vtable slot writable; runtime patches declined");return -1;
    }
    drift_event_base=base;drift_original_method=(DriftMethod)(base+0x4106480);
    PVOID previous=InterlockedCompareExchangePointer(slot,(PVOID)drift_method_gate,(PVOID)drift_original_method);
    DWORD ignored;BOOL restored=VirtualProtect((void*)slot,sizeof(*slot),old,&ignored);
    if(previous!=(PVOID)drift_original_method){drift_restore_reset_entry();drift_event_log("vtable changed concurrently; runtime patches declined");return -1;}
    if(!restored) {
        InterlockedCompareExchangePointer(slot,(PVOID)drift_original_method,(PVOID)drift_method_gate);
        VirtualProtect((void*)slot,sizeof(*slot),old,&ignored);
        drift_restore_reset_entry();
        drift_event_log("cannot restore vtable page protection; hook rolled back");return -1;
    }
    InterlockedExchange(&drift_event_installed,1);
    drift_event_log("installed: MethodObjectInfo observer forwards every callback; one-byte RET at reset RVA 0x895250; EXE file unchanged; exception recovery disabled");
    return 1;
}
static int drift_wait_event_gate(DWORD64 base,unsigned attempts) {
    int result=0;
    for(unsigned i=0;i<attempts&&result==0;i++){Sleep(250);result=drift_install_event_gate(base);}
    return result;
}
static DWORD WINAPI drift_event_worker(void *module) {
    int result=drift_wait_event_gate((DWORD64)GetModuleHandleA(NULL),1200);
    if(!result)drift_event_log("runtime code signatures never became ready; no hook installed");
    FreeLibraryAndExitThread((HMODULE)module,0);return 0;
}
static void drift_start_event_gate(void) {
    if(InterlockedCompareExchange(&drift_event_started,1,0))return;
    if(drift_install_event_gate((DWORD64)GetModuleHandleA(NULL))!=0)return;
    drift_event_log("waiting up to 300 seconds for exact runtime code signatures; no runtime changes yet");
    HMODULE module=NULL;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCSTR)(uintptr_t)drift_event_worker,&module))return;
    HANDLE thread=CreateThread(NULL,0,drift_event_worker,module,0,NULL);
    if(thread)CloseHandle(thread);else FreeLibrary(module);
}
static void drift_remove_event_gate(void) {
    if(!InterlockedCompareExchange(&drift_event_installed,0,0))return;
    PVOID volatile *slot=(PVOID volatile *)(drift_event_base+0x5cd0be8);DWORD old,ignored;
    if(VirtualProtect((void*)slot,sizeof(*slot),PAGE_READWRITE,&old)) {
        InterlockedCompareExchangePointer(slot,(PVOID)drift_original_method,(PVOID)drift_method_gate);
        VirtualProtect((void*)slot,sizeof(*slot),old,&ignored);
        InterlockedExchange(&drift_event_installed,0);
        drift_restore_reset_entry();
    }
}
static BOOL drift_secondary_matches(const EXCEPTION_POINTERS *ep,DWORD64 base) {
    return ep&&ep->ExceptionRecord&&ep->ContextRecord&&
        (ep->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION||ep->ExceptionRecord->ExceptionCode==EXCEPTION_ILLEGAL_INSTRUCTION||ep->ExceptionRecord->ExceptionCode==EXCEPTION_PRIV_INSTRUCTION)&&
        (DWORD64)ep->ExceptionRecord->ExceptionAddress>=base+0x1000&&
        (DWORD64)ep->ExceptionRecord->ExceptionAddress<base+0x9b10e00&&
        (DWORD)InterlockedCompareExchange(&drift_event_thread,0,0)==GetCurrentThreadId()&&
        (DWORD)(GetTickCount()-(DWORD)InterlockedCompareExchange(&drift_event_tick,0,0))<30000;
}
#endif
