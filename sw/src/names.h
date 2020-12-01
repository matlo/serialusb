/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef NAMES_H_
#define NAMES_H_

int get_vendor_string(char *buf, size_t size, u_int16_t vid);
int get_product_string(char *buf, size_t size, u_int16_t vid, u_int16_t pid);

#endif /* NAMES_H_ */
