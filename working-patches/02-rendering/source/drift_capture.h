/* Targeted crash evidence only: never changes CONTEXT or swallows exceptions. */
#ifndef FH4_DRIFT_CAPTURE_H
#define FH4_DRIFT_CAPTURE_H
#include <dbghelp.h>
static volatile LONG drift_captured;
static PVOID drift_priority_handler;
static volatile LONG drift_priority_armed;
static BOOL drift_in_priority;
typedef struct { EXCEPTION_RECORD record; CONTEXT context; DWORD thread; char prefix[MAX_PATH]; } DriftCapture;
static BOOL drift_matches(const EXCEPTION_POINTERS *ep, DWORD64 base) {
    return ep && ep->ExceptionRecord && ep->ContextRecord &&
        ((ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
          (DWORD64)ep->ExceptionRecord->ExceptionAddress == base + 0x8952ac) || drift_secondary_matches(ep,base));
}
static void drift_save(const char *prefix,const char *suffix,const void *data,DWORD size) {
    char name[MAX_PATH]; snprintf(name,sizeof(name),"%s%s",prefix,suffix);
    HANDLE f=CreateFileA(name,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f!=INVALID_HANDLE_VALUE){ DWORD written=0; WriteFile(f,data,size,&written,NULL); CloseHandle(f); }
}
static DWORD WINAPI drift_dump_worker(void *arg) {
    DriftCapture *d=(DriftCapture*)arg;
    typedef BOOL (WINAPI *WriteDump)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
    HMODULE lib=LoadLibraryExW(L"dbghelp.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    WriteDump fn=lib?(WriteDump)(void*)GetProcAddress(lib,"MiniDumpWriteDump"):NULL;
    BOOL ok=FALSE; DWORD err=ERROR_PROC_NOT_FOUND;
    if(fn) {
        char name[MAX_PATH];snprintf(name,sizeof(name),"%s.dmp",d->prefix);
        HANDLE f=CreateFileA(name,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(f!=INVALID_HANDLE_VALUE) {
            EXCEPTION_POINTERS ep={&d->record,&d->context};
            MINIDUMP_EXCEPTION_INFORMATION ei={d->thread,&ep,FALSE};
            ok=fn(GetCurrentProcess(),GetCurrentProcessId(),f,(MINIDUMP_TYPE)(MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules|MiniDumpWithFullMemoryInfo),&ei,NULL,NULL);
            err=ok?0:GetLastError();CloseHandle(f);
        } else err=GetLastError();
    }
    char text[128];int n=snprintf(text,sizeof(text),"MiniDumpWriteDump success=%d error=%lu\r\n",ok,(unsigned long)err);
    drift_save(d->prefix,".dump-status.txt",text,(DWORD)n);
    HeapFree(GetProcessHeap(),0,d);return 0;
}
static void drift_capture_exception(EXCEPTION_POINTERS *ep,DWORD64 base) {
    if(!drift_matches(ep,base)||InterlockedCompareExchange(&drift_captured,1,0))return;
    DriftCapture *d=(DriftCapture*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*d));if(!d)return;
    d->record=*ep->ExceptionRecord;d->record.ExceptionRecord=NULL;d->context=*ep->ContextRecord;d->thread=GetCurrentThreadId();
    CreateDirectoryA("C:\\highball",NULL);CreateDirectoryA("C:\\highball\\logs",NULL);
    snprintf(d->prefix,sizeof(d->prefix),"C:\\highball\\logs\\drift-%lu-%lu-%llu",(unsigned long)GetCurrentProcessId(),(unsigned long)d->thread,(unsigned long long)GetTickCount64());
    NT_TIB *tib=(NT_TIB*)NtCurrentTeb();ULONG_PTR low=(ULONG_PTR)tib->StackLimit,high=(ULONG_PTR)tib->StackBase;
    CONTEXT *c=&d->context;char text[4096];
    int n=snprintf(text,sizeof(text),
        "FH4 drift capture v5: original context unchanged\r\n"
        "base=%llx exception=%08lx address=%llx access_kind=%llu target=%llx\r\n"
        "RIP=%llx RSP=%llx RBP=%llx RAX=%llx RBX=%llx RCX=%llx RDX=%llx\r\n"
        "RSI=%llx RDI=%llx R8=%llx R9=%llx R10=%llx R11=%llx R12=%llx R13=%llx R14=%llx R15=%llx\r\n"
        "StackLimit=%llx StackBase=%llx rsp_in_thread_stack=%d priority_capture=%d\r\n"
        "event_gate_installed=%ld reset_entry_installed=%ld\r\n",
        (unsigned long long)base,(unsigned long)d->record.ExceptionCode,(unsigned long long)d->record.ExceptionAddress,
        (unsigned long long)d->record.ExceptionInformation[0],(unsigned long long)d->record.ExceptionInformation[1],
        c->Rip,c->Rsp,c->Rbp,c->Rax,c->Rbx,c->Rcx,c->Rdx,c->Rsi,c->Rdi,c->R8,c->R9,c->R10,c->R11,c->R12,c->R13,c->R14,c->R15,
        (unsigned long long)low,(unsigned long long)high,c->Rsp>=low&&c->Rsp<high,drift_in_priority,
        InterlockedCompareExchange(&drift_event_installed,0,0),InterlockedCompareExchange(&drift_reset_patched,0,0));
    drift_save(d->prefix,".txt",text,(DWORD)n);drift_save(d->prefix,".context.bin",c,sizeof(*c));
    BYTE code[256];SIZE_T got=0;
    if(ReadProcessMemory(GetCurrentProcess(),(void*)(base+0x895250),code,64,&got)&&got)drift_save(d->prefix,".drift-entry.bin",code,(DWORD)got);
    got=0;
    if(ReadProcessMemory(GetCurrentProcess(),(void*)(c->Rip-64),code,sizeof(code),&got)&&got)drift_save(d->prefix,".code.bin",code,(DWORD)got);
    /* Use the genuine TIB stack bounds, since RSP may point into executable memory. */
    if(high>low&&high-low<=64*1024*1024) {
        SIZE_T size=high-low;if(size>4*1024*1024){low=high-4*1024*1024;size=4*1024*1024;}
        BYTE *stack=(BYTE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size);
        if(stack) {
            for(SIZE_T off=0;off<size;off+=4096){SIZE_T count=size-off<4096?size-off:4096,read=0;ReadProcessMemory(GetCurrentProcess(),(void*)(low+off),stack+off,count,&read);}
            /* Runtime caller pages: on-disk code is encrypted and cannot validate return sites. */
            SIZE_T stride=sizeof(DWORD64)+4096; DWORD pages=0; DWORD64 seen[128];
            BYTE *sites=(BYTE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,128*stride);
            if(sites) {
                for(SIZE_T off=size&~(SIZE_T)7;off>=8 && pages<128;) {
                    off-=8; DWORD64 ptr=0;memcpy(&ptr,stack+off,8);
                    if(ptr<base+0x1000 || ptr>=base+0x9b11000)continue;
                    DWORD64 page=ptr&~(DWORD64)4095;BOOL duplicate=FALSE;
                    for(DWORD i=0;i<pages;i++)if(seen[i]==page){duplicate=TRUE;break;}
                    if(duplicate)continue;
                    SIZE_T read=0;
                    if(ReadProcessMemory(GetCurrentProcess(),(void*)page,sites+pages*stride+8,4096,&read)&&read==4096) {
                        memcpy(sites+pages*stride,&page,8);seen[pages++]=page;
                    }
                }
                drift_save(d->prefix,".caller-pages.bin",sites,(DWORD)(pages*stride));
                HeapFree(GetProcessHeap(),0,sites);
            }
            drift_save(d->prefix,".stack.bin",stack,(DWORD)size);
            char info[160];int len=snprintf(info,sizeof(info),"stack.bin base=%llx size=%llu (unreadable pages zero-filled)\r\n",(unsigned long long)low,(unsigned long long)size);
            drift_save(d->prefix,".stack-info.txt",info,(DWORD)len);HeapFree(GetProcessHeap(),0,stack);
        }
    }
    HANDLE thread=CreateThread(NULL,0,drift_dump_worker,d,0,NULL);
    if(thread){WaitForSingleObject(thread,15000);CloseHandle(thread);}else HeapFree(GetProcessHeap(),0,d);
}
static LONG WINAPI drift_priority_filter(EXCEPTION_POINTERS *ep) {
    DWORD64 base=(DWORD64)GetModuleHandleA(NULL);
    if(drift_matches(ep,base)) {
        drift_in_priority=TRUE;
        drift_capture_exception(ep,base);
        drift_in_priority=FALSE;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void drift_arm_first_chance(void) {
    if(InterlockedCompareExchange(&drift_priority_armed,1,0))return;
    drift_priority_handler=AddVectoredExceptionHandler(1,drift_priority_filter);
}
static void drift_disarm_first_chance(void) {
    if(drift_priority_handler){RemoveVectoredExceptionHandler(drift_priority_handler);drift_priority_handler=NULL;}
}
#endif
