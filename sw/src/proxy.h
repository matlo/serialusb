/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef PROXY_H_
#define PROXY_H_

int proxy_init();
int proxy_start(char * port);
void proxy_stop();

#endif /* PROXY_H_ */
