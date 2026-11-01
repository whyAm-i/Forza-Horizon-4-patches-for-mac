#include <windows.h>
#include <stdio.h>
int main(int argc,char **argv) {
    if(argc<2||argc>3)return 2;
    if(!SetEnvironmentVariableA("HB_D3D12_REAL",argc==3?argv[2]:"C:\\windows\\system32\\apd12.dll"))return 3;
    HMODULE dll=LoadLibraryA(argv[1]);
    if(!dll){printf("FAIL shim load %lu\n",(unsigned long)GetLastError());return 4;}
    char path[1024]={0};DWORD length=GetModuleFileNameA(dll,path,sizeof(path));
    if(!length||length>=sizeof(path)||lstrcmpiA(path,argv[1])) {
        printf("FAIL staged path mismatch: loaded %s, expected %s\n",path,argv[1]);return 9;
    }
    HMODULE provider=GetModuleHandleA("apd12.dll");
    if(!provider||provider==dll){puts("FAIL actual provider not loaded");return 5;}
    FARPROC create=GetProcAddress(dll,"D3D12CreateDevice");
    if(!create||create!=GetProcAddress(dll,(LPCSTR)101)||!GetProcAddress(dll,(LPCSTR)100))return 6;
    const char *exports[]={"GetBehaviorValue","D3D12CoreCreateLayeredDevice","D3D12CoreGetLayeredDeviceSize",
        "D3D12CoreRegisterLayers","D3D12CreateRootSignatureDeserializer","D3D12CreateVersionedRootSignatureDeserializer",
        "D3D12EnableExperimentalFeatures","D3D12GetDebugInterface","D3D12SerializeRootSignature","D3D12SerializeVersionedRootSignature"};
    for(unsigned i=0;i<sizeof(exports)/sizeof(exports[0]);i++) {
        unsigned char *jump=(unsigned char*)GetProcAddress(dll,exports[i]);
        if(!jump||jump[0]!=0xff||jump[1]!=0x25)return 7;
        LONG displacement;CopyMemory(&displacement,jump+2,4);FARPROC resolved;
        CopyMemory(&resolved,jump+6+displacement,sizeof(resolved));
        if(resolved!=GetProcAddress(provider,exports[i])) {printf("FAIL forward %s\n",exports[i]);return 8;}
    }
    printf("PASS staged shim %s loaded actual apd12 provider; ordinals 100/101 and all 10 forwarding targets verified.\n",path);
    FreeLibrary(dll);return 0;
}
