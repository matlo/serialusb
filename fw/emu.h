/*
 * Copyright 2015  Mathieu Laurendeau (mat.lau [at] laposte [dot] net)
 * License: GPLv3
 */

#ifndef _EMU_H_
#define _EMU_H_

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <stdbool.h>
#include <string.h>

#include <LUFA/Version.h>
#include <LUFA/Drivers/USB/USB.h>
#include <LUFA/Drivers/Board/LEDs.h>

void EVENT_USB_Device_Connect(void);
void EVENT_USB_Device_Disconnect(void);
void EVENT_USB_Device_ConfigurationChanged(void);
void EVENT_USB_Device_UnhandledControlRequest(void);

#endif
