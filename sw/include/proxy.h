/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef PROXY_H_
#define PROXY_H_

int proxy_init(char * port);
int proxy_start();
void proxy_stop();
int proxy_clean();

#endif /* PROXY_H_ */
