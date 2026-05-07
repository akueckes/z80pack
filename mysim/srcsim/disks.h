#ifndef DISKS_INC
#define DISKS_INC

#ifdef HAS_TARBELL_FDC
#include "tarbell_fdc.h"
#else
#include "imsai-fif.h"
#endif

#ifndef DISKMAP
#define DISKMAP     "disk.map"
#endif

#define LAST_DISK   'D'
#define _MAX_DISK   (LAST_DISK - '@')

#define DISKNAME(A) disks[A]

#endif
