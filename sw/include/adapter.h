/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef ADAPTER_H_
#define ADAPTER_H_

#include <protocol.h>

typedef int (* ADAPTER_PACKET_CALLBACK)(int user, s_packet * packet);

int adapter_add(int serial, ADAPTER_PACKET_CALLBACK fp_packet_cb);
int adapter_recv(int adapter, const void * buf, unsigned int count);
int adapter_send(int adapter, unsigned char type, const unsigned char * data, unsigned int count, unsigned int timeout);

#endif /* ADAPTER_H_ */
