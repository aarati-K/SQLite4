/*
** 2007 August 14
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file contains low-level memory allocation drivers for when
** SQLite will use the standard C-library malloc/realloc/free interface
** to obtain the memory it needs.
**
** This file contains implementations of the low-level memory allocation
** routines specified in the sqlite4_mem_methods object.  The content of
** this file is only used if SQLITE4_SYSTEM_MALLOC is defined.  The
** SQLITE4_SYSTEM_MALLOC macro is defined automatically if neither the
** SQLITE4_MEMDEBUG nor the SQLITE4_WIN32_MALLOC macros are defined.  The
** default configuration is to use memory allocation routines in this
** file.
**
** C-preprocessor macro summary:
**
**    HAVE_MALLOC_USABLE_SIZE     The configure script sets this symbol if
**                                the malloc_usable_size() interface exists
**                                on the target platform.  Or, this symbol
**                                can be set manually, if desired.
**                                If an equivalent interface exists by
**                                a different name, using a separate -D
**                                option to rename it.  This symbol will
**                                be enabled automatically on windows
**                                systems, and malloc_usable_size() will
**                                be redefined to _msize(), unless the
**                                SQLITE4_WITHOUT_MSIZE macro is defined.
**    
**    SQLITE4_WITHOUT_ZONEMALLOC   Some older macs lack support for the zone
**                                memory allocator.  Set this symbol to enable
**                                building on older macs.
**
**    SQLITE4_WITHOUT_MSIZE        Set this symbol to disable the use of
**                                _msize() on windows systems.  This might
**                                be necessary when compiling for Delphi,
**                                for example.
*/
#include "sqliteInt.h"

/*
** This version of the memory allocator is the default.  It is
** used when no other memory allocator is specified using compile-time
** macros.
*/
#ifdef SQLITE4_SYSTEM_MALLOC

/*
** Windows systems have malloc_usable_size() but it is called _msize().
** The use of _msize() is automatic, but can be disabled by compiling
** with -DSQLITE4_WITHOUT_MSIZE
*/
#if !defined(HAVE_MALLOC_USABLE_SIZE) && SQLITE4_OS_WIN \
      && !defined(SQLITE4_WITHOUT_MSIZE)
# define HAVE_MALLOC_USABLE_SIZE 1
# define SQLITE4_MALLOCSIZE _msize
#endif

#if defined(__APPLE__) && !defined(SQLITE4_WITHOUT_ZONEMALLOC)

/*
** Use the zone allocator available on apple products unless the
** SQLITE4_WITHOUT_ZONEMALLOC symbol is defined.
*/
#include <sys/sysctl.h>
#include <malloc/malloc.h>
#include <libkern/OSAtomic.h>
static malloc_zone_t* _sqliteZone_;
#define SQLITE4_MALLOC(x) malloc_zone_malloc(_sqliteZone_, (x))
#define SQLITE4_FREE(x) malloc_zone_free(_sqliteZone_, (x));
#define SQLITE4_REALLOC(x,y) malloc_zone_realloc(_sqliteZone_, (x), (y))
#define SQLITE4_MALLOCSIZE(x) \
        (_sqliteZone_ ? _sqliteZone_->size(_sqliteZone_,x) : malloc_size(x))

#else /* if not __APPLE__ */

/*
** Use standard C library malloc and free on non-Apple systems.  
** Also used by Apple systems if SQLITE4_WITHOUT_ZONEMALLOC is defined.
*/
#define SQLITE4_MALLOC(x)    malloc(x)
#define SQLITE4_FREE(x)      free(x)
#define SQLITE4_REALLOC(x,y) realloc((x),(y))

#ifdef HAVE_MALLOC_USABLE_SIZE
# ifndef SQLITE4_MALLOCSIZE
#  include <malloc.h>
#  define SQLITE4_MALLOCSIZE(x) malloc_usable_size(x)
# endif
#else
# undef SQLITE4_MALLOCSIZE
#endif

#endif /* __APPLE__ or not __APPLE__ */

/*
** Like malloc(), but remember the size of the allocation
** so that we can find it later using sqlite4MemSize().
**
** For this low-level routine, we are guaranteed that nByte>0 because
** cases of nByte<=0 will be intercepted and dealt with by higher level
** routines.
*/
static void *sqlite4MemMalloc(void *NotUsed, sqlite4_size_t nByte){
#ifdef SQLITE4_MALLOCSIZE
  void *p = SQLITE4_MALLOC( nByte );
  UNUSED_PARAMETER(NotUsed);
  if( p==0 ){
    testcase( sqlite4DefaultEnv.xLog!=0 );
    sqlite4_log(0,SQLITE4_NOMEM, "failed to allocate %u bytes of memory", nByte);
  }
  return p;
#else
  sqlite4_int64 *p;
  assert( nByte>0 );
  UNUSED_PARAMETER(NotUsed);
  nByte = ROUND8(nByte);
  p = SQLITE4_MALLOC( nByte+8 );
  if( p ){
    p[0] = nByte;
    p++;
  }else{
    testcase( sqlite4DefaultEnv.xLog!=0 );
    sqlite4_log(0,SQLITE4_NOMEM, "failed to allocate %u bytes of memory", nByte);
  }
  return (void *)p;
#endif
}

/*
** Like free() but works for allocations obtained from sqlite4MemMalloc()
** or sqlite4MemRealloc().
**
** For this low-level routine, we already know that pPrior!=0 since
** cases where pPrior==0 will have been intecepted and dealt with
** by higher-level routines.
*/
static void sqlite4MemFree(void *NotUsed, void *pPrior){
#ifdef SQLITE4_MALLOCSIZE
  UNUSED_PARAMETER(NotUsed);
  SQLITE4_FREE(pPrior);
#else
  sqlite4_int64 *p = (sqlite4_int64*)pPrior;
  UNUSED_PARAMETER(NotUsed);
  assert( pPrior!=0 );
  p--;
  SQLITE4_FREE(p);
#endif
}

/*
** Report the allocated size of a prior return from xMalloc()
** or xRealloc().
*/
static sqlite4_size_t sqlite4MemSize(void *NotUsed, void *pPrior){
#ifdef SQLITE4_MALLOCSIZE
  UNUSED_PARAMETER(NotUsed);
  return pPrior ? (int)SQLITE4_MALLOCSIZE(pPrior) : 0;
#else
  sqlite4_int64 *p;
  UNUSED_PARAMETER(NotUsed);
  if( pPrior==0 ) return 0;
  p = (sqlite4_int64*)pPrior;
  p--;
  return (sqlite4_size_t)p[0];
#endif
}

/*
** Like realloc().  Resize an allocation previously obtained from
** sqlite4MemMalloc().
**
** For this low-level interface, we know that pPrior!=0.  Cases where
** pPrior==0 while have been intercepted by higher-level routine and
** redirected to xMalloc.  Similarly, we know that nByte>0 becauses
** cases where nByte<=0 will have been intercepted by higher-level
** routines and redirected to xFree.
*/
static void *sqlite4MemRealloc(void *NotUsed, void *pPrior, int nByte){
#ifdef SQLITE4_MALLOCSIZE
  void *p = SQLITE4_REALLOC(pPrior, nByte);
  UNUSED_PARAMETER(NotUsed);
  if( p==0 ){
    testcase( sqlite4DefaultEnv.xLog!=0 );
    sqlite4_log(0,SQLITE4_NOMEM,
      "failed memory resize %u to %u bytes",
      SQLITE4_MALLOCSIZE(pPrior), nByte);
  }
  return p;
#else
  sqlite4_int64 *p = (sqlite4_int64*)pPrior;
  assert( pPrior!=0 && nByte>0 );
  assert( nByte==ROUND8(nByte) ); /* EV: R-46199-30249 */
  UNUSED_PARAMETER(NotUsed);
  p--;
  p = SQLITE4_REALLOC(p, nByte+8 );
  if( p ){
    p[0] = nByte;
    p++;
  }else{
    testcase( sqlite4DefaultEnv.xLog!=0 );
    sqlite4_log(0,SQLITE4_NOMEM,
      "failed memory resize %u to %u bytes",
      sqlite4MemSize(0, pPrior), nByte);
  }
  return (void*)p;
#endif
}

/*
** Initialize this module.
*/
static int sqlite4MemInit(void *NotUsed){
#if defined(__APPLE__) && !defined(SQLITE4_WITHOUT_ZONEMALLOC)
  int cpuCount;
  size_t len;
  if( _sqliteZone_ ){
    return SQLITE4_OK;
  }
  len = sizeof(cpuCount);
  /* One usually wants to use hw.acctivecpu for MT decisions, but not here */
  sysctlbyname("hw.ncpu", &cpuCount, &len, NULL, 0);
  if( cpuCount>1 ){
    /* defer MT decisions to system malloc */
    _sqliteZone_ = malloc_default_zone();
  }else{
    /* only 1 core, use our own zone to contention over global locks, 
    ** e.g. we have our own dedicated locks */
    bool success;		
    malloc_zone_t* newzone = malloc_create_zone(4096, 0);
    malloc_set_zone_name(newzone, "Sqlite_Heap");
    do{
      success = OSAtomicCompareAndSwapPtrBarrier(NULL, newzone, 
                                 (void * volatile *)&_sqliteZone_);
    }while(!_sqliteZone_);
    if( !success ){	
      /* somebody registered a zone first */
      malloc_destroy_zone(newzone);
    }
  }
#endif
  UNUSED_PARAMETER(NotUsed);
  return SQLITE4_OK;
}

/*
** Deinitialize this module.
*/
static void sqlite4MemShutdown(void *NotUsed){
  UNUSED_PARAMETER(NotUsed);
  return;
}

/*
** This routine is the only routine in this file with external linkage.
**
** Populate the low-level memory allocation function pointers in
** sqlite4DefaultEnv.m with pointers to the routines in this file.
*/
void sqlite4MemSetDefault(sqlite4_env *pEnv){
  // static const sqlite4_mem_methods defaultMethods = {
  //    sqlite4MemMalloc,
  //    sqlite4MemFree,
  //    sqlite4MemRealloc,
  //    sqlite4MemSize,
  //    sqlite4MemInit,
  //    sqlite4MemShutdown,
  //    0,
  //    0,
  //    0
  // };
#if 0
  pEnv->m = defaultMethods;
  pEnv->m.pMemEnv = (void*)pEnv;
#endif
}

#endif /* SQLITE4_SYSTEM_MALLOC */
