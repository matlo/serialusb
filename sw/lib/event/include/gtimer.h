/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef GTIMER_H_
#define GTIMER_H_

#include <gpoll.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WIN32
#define GTIMER int
#define INVALID_GTIMER_VALUE -1
#else
#include <windows.h>
#define GTIMER HANDLE
#define INVALID_GTIMER_VALUE INVALID_HANDLE_VALUE
#endif

GTIMER gtimer_start(int user, int usec, GPOLL_READ_CALLBACK fp_read, GPOLL_CLOSE_CALLBACK fp_close,
    GPOLL_REGISTER_FD fp_register);
int gtimer_close(GTIMER tfd);
int gtimer_read(GTIMER tfd);

#ifdef __cplusplus
}
#endif

#endif /* GTIMER_H_ */
