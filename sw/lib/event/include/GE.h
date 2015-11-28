/*
 Copyright (c) 2011 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef GE_H_
#define GE_H_

#include <stdint.h>

#ifdef WIN32
#include "GE_Windows.h"
typedef void* HANDLE;
#else
#include "GE_Linux.h"
#include <sys/time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

int GE_initialize();
void GE_quit();
void GE_AddSource(int fd, int id, int (*fp_read)(int), int (*fp_write)(int), int (*fp_cleanup)(int));
#ifdef WIN32
void GE_AddSourceHandle(HANDLE handle, int id, int (*fp_read)(int), int (*fp_write)(int), int (*fp_cleanup)(int));
void GE_RemoveSourceHandle(HANDLE handle);
#endif
void GE_RemoveSource(int fd);
void GE_PumpEvents();

#ifdef __cplusplus
}
#endif

#endif /* GE_H_ */
