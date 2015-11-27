/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef SW_INCLUDE_SERIALPROXY_H_
#define SW_INCLUDE_SERIALPROXY_H_

int proxy_init(int usb, int serial);
int proxy_stop(int serial);

#endif /* SW_INCLUDE_SERIALPROXY_H_ */
