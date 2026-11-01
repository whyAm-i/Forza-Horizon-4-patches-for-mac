/* An original, inert PE image for SEC_IMAGE/COW tests. Never loaded/executed.
 * Its .bss provides the address range of the synthetic FH4-layout fixture. */
#include <windows.h>
volatile unsigned char fixture_space[0x9b11000];
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved) {
    (void)module;(void)reason;(void)reserved;return TRUE;
}
