#include <linux/rhconfig.h>
#if defined(__module__smp)
#define UTS_RELEASE "2.4.18-14smp"
#elif defined(__module__BOOT)
#define UTS_RELEASE "2.4.18-14BOOT"
#elif defined(__module__bigmem)
#define UTS_RELEASE "2.4.18-14bigmem"
#elif defined(__module__debug)
#define UTS_RELEASE "2.4.18-14debug"
#else
#define UTS_RELEASE "2.4.18-14"
#endif
#define LINUX_VERSION_CODE 132114
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
