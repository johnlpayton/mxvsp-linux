#ifndef MEMLOCKW32_H
#define MEMLOCKW32_H
/*
*==========================================================================
*
*   memlockW32.h
*   (c) 2012 jlp
*
*   defines for W32 memory and locking
*
*==========================================================================
*/

#if USEGDK
typedef GRecMutex t_lock;
#endif

#if USEWIN32
typedef CRITICAL_SECTION t_lock;
#endif

//======== externs (probably should go in a different file)
extern t_lock* InitLockSection(t_lock* lock);
extern t_lock* FreeLockSection(t_lock* lock);
extern void LockSection(t_lock* lock);
extern void UnLockSection(t_lock* lock);

#endif
