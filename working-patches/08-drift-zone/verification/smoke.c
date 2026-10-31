#include <windows.h>
#include <stdio.h>
int main(int argc,char **argv){HMODULE m=argc==2?LoadLibraryA(argv[1]):LoadLibraryW(L"d3d12.dll");if(!m){printf("FAIL LoadLibrary: %lu\n",GetLastError());return 1;}void *a=(void*)GetProcAddress(m,"D3D12CreateDevice"),*b=(void*)GetProcAddress(m,(LPCSTR)101);if(!a||a!=b||!GetProcAddress(m,(LPCSTR)100)){puts("FAIL exports");return 2;}puts("PASS: d3d12 provider load and exports 100/101.");return 0;}
