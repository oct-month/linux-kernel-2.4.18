/* ugly hack to work around driver brokenness */
#define ISP2300
#define FAILOVER 
#define LINUX 
#define linux 

#include "settings.h"
#include "qla2x00.h"
#include "listops.h"

#include "qla2x00.c"
#include "multipath.c"
#include "flash.c"
#include "ioctl.c"

