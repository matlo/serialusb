/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef ADAPTER_H_
#define ADAPTER_H_

#include <protocol.h>

typedef int (* ADAPTER_READ_CALLBACK)(int user, s_packet * packet);
typedef int (* ADAPTER_WRITE_CALLBACK)(int user, int transfered);
typedef int (* ADAPTER_CLOSE_CALLBACK)(int user);

int adapter_open(const char * port, ADAPTER_READ_CALLBACK fp_read, ADAPTER_WRITE_CALLBACK fp_write, ADAPTER_CLOSE_CALLBACK fp_close);
int adapter_send(int adapter, unsigned char type, const unsigned char * data, unsigned int count);

#endif /* ADAPTER_H_ */
