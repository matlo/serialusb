/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef ADAPTER_H_
#define ADAPTER_H_

#include <gimxpoll/include/gpoll.h>

#include <protocol.h>

struct adapter_device;

typedef int (* ADAPTER_READ_CALLBACK)(void * user, s_packet * packet);
typedef int (* ADAPTER_WRITE_CALLBACK)(void * user, int transfered);
typedef int (* ADAPTER_CLOSE_CALLBACK)(void * user);
#ifndef WIN32
typedef GPOLL_REGISTER_FD ADAPTER_REGISTER_SOURCE;
typedef GPOLL_REMOVE_FD ADAPTER_REMOVE_SOURCE;
#else
typedef GPOLL_REGISTER_HANDLE ADAPTER_REGISTER_SOURCE;
typedef GPOLL_REMOVE_HANDLE ADAPTER_REMOVE_SOURCE;
#endif

typedef struct {
    ADAPTER_READ_CALLBACK fp_read;       // called on data reception
    ADAPTER_WRITE_CALLBACK fp_write;     // called on write completion
    ADAPTER_CLOSE_CALLBACK fp_close;     // called on failure
    ADAPTER_REGISTER_SOURCE fp_register; // to register device to event sources
    ADAPTER_REMOVE_SOURCE fp_remove;     // to remove device from event sources
} ADAPTER_CALLBACKS;

struct adapter_device * adapter_open(const char * port, const ADAPTER_CALLBACKS * callbacks);
int adapter_send(void * user, unsigned char type, const unsigned char * data, unsigned int count);
int adapter_close(void * user);

#endif /* ADAPTER_H_ */
