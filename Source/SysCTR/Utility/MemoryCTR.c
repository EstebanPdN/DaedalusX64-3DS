
// Taken from PicoDrive's libretro implementation for 3DS.
#include <3ds.h>
#include <stdio.h>

#include "MemoryCTR.h"

typedef s32 (*ctr_callback_type)(void);

int __stacksize__ = 2 * 1024 * 1024;

static int hack3dsSvcInitialized = 0;

static unsigned int s1, s2, s3, s0;

//-----------------------------------------------------------------------------
// Internal function that enables all services. This must be run via
// svcBackdoor.
//-----------------------------------------------------------------------------
static s32 ctrEnableAllServices(void)
{
	__asm__ volatile("cpsid aif");

	unsigned int *svc_access_control = *(*(unsigned int***)0xFFFF9000 + 0x22) - 0x6;

	s0 = svc_access_control[0];
	s1 = svc_access_control[1];
	s2 = svc_access_control[2];
	s3 = svc_access_control[3];

	svc_access_control[0]=0xFFFFFFFE;
	svc_access_control[1]=0xFFFFFFFF;
	svc_access_control[2]=0xFFFFFFFF;
	svc_access_control[3]=0x3FFFFFFF;
	return 0;

}

//-----------------------------------------------------------------------------
// Sets the permissions for memory.
// You must ensutre that buffer and size are 0x1000-aligned.
//-----------------------------------------------------------------------------
int _SetMemoryPermission(void *buffer, int size, int permission)
{
	Handle currentHandle;
	if (!buffer || size <= 0 || ((uintptr_t)buffer & 4095) || (size & 4095))
		return -1;
	Result duplicate = svcDuplicateHandle(&currentHandle, CUR_PROCESS_HANDLE);
	if (R_FAILED(duplicate)) return duplicate;
	int res = svcControlProcessMemory(currentHandle, (u32)buffer, 0, size, MEMOP_PROT, permission);
	svcCloseHandle(currentHandle);

	return res;
}

//-----------------------------------------------------------------------------
// Initializes the hack to gain kernel access to all services.
// The one that we really are interested is actually the 
// svcControlProcessMemory, because once we have kernel access, we can
// grant read/write/execute access to memory blocks, and which means we
// can do dynamic recompilation.
//-----------------------------------------------------------------------------
int _InitializeSvcHack(void)
{
		if (R_FAILED(svcBackdoor(ctrEnableAllServices))) return 0;
		if (R_FAILED(svcBackdoor(ctrEnableAllServices))) return 0;

#if 0
		printf("svc_access_control: %x %x %x %x\n", s0, s1, s2, s3);
#endif

		if (s0 != 0xFFFFFFFE || s1 != 0xFFFFFFFF || s2 != 0xFFFFFFFF || s3 != 0x3FFFFFFF)
		{
				printf("svcHack failed: svcBackdoor unsuccessful.\n");
				return 0;
		}

		hack3dsSvcInitialized = 1;
		return 1;
}