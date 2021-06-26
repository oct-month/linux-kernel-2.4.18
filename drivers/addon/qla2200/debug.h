
#undef ENTER_TRACE

/*
* Macros use for debugging the driver.
*/
#ifdef ENTER_TRACE
#define ENTER(x)	do { printk("qla2100 : Entering %s()\n", x); } while (0)
#define LEAVE(x)	do { printk("qla2100 : Leaving %s()\n", x);  } while (0)
#define ENTER_INTR(x)	do { printk("qla2100 : Entering %s()\n", x); } while (0)
#define LEAVE_INTR(x)	do { printk("qla2100 : Leaving %s()\n", x);  } while (0)
#else
#define ENTER(x)	do {} while (0)
#define LEAVE(x)	do {} while (0)
#define ENTER_INTR(x) 	do {} while (0)
#define LEAVE_INTR(x)   do {} while (0)
#endif
#ifdef QL_DEBUG_LEVEL_3
#define DEBUG3(x)	do {x;} while (0)
#else
#define DEBUG3(x)	do {} while (0)
#endif

#if  QLA2100_COMTRACE
#define COMTRACE(x)     do {printk(x);} while (0)
#else
#define COMTRACE(x)	do {} while (0)
#endif

#if  DEBUG_QLA2100
#define DEBUG(x)	do {x;} while (0)
#define DEBUG4(x)	do {} while (0)
#else
#define DEBUG(x)	do {} while (0)
#define DEBUG4(x)	do {} while (0)
#endif

#ifdef QL_DEBUG_LEVEL_2
#define DEBUG2(x)       do {x;} while (0)
#else
#define DEBUG2(x)	do {} while (0)
#endif
#ifdef QL_DEBUG_LEVEL_5
#define DEBUG5(x)          do {x;} while (0)
#else
#define DEBUG5(x)	do {} while (0)
#endif

