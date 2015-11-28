/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <GE.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>
#include <unistd.h>

#include <usbasync.h>
#include <serialasync.h>
#include <protocol.h>
#include <serialproxy.h>

#ifdef WIN32
#define REGISTER_FUNCTION GE_AddSourceHandle
#else
#define REGISTER_FUNCTION GE_AddSource
#endif

static int usb = -1;
static int serial = -1;

static volatile int done = 0;

static void terminate(int sig) {
  done = 1;
}

#ifdef WIN32
BOOL WINAPI ConsoleHandler(DWORD dwType) {
  switch(dwType) {
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:

    done = 1; //signal the main thread to terminate

    //Returning would make the process exit!
    //We just make the handler sleep until the main thread exits,
    //or until the maximum execution time for this handler is reached.
    Sleep(10000);

    return TRUE;
    default:
    break;
  }
  return FALSE;
}
#endif

static void dump(const unsigned char * packet, unsigned char length) {

  int i;
  for (i = 0; i < length; ++i) {
    if (i && !(i % 8)) {
      printf("\n");
    }
    printf("0x%02x ", packet[i]);
  }
  printf("\n");
}

char * usb_select() {

  char * path = NULL;

  s_usb_dev * usb_devs = usbasync_enumerate(0x0000, 0x0000);
  if (usb_devs == NULL) {
    fprintf(stderr, "No USB device detected!\n");
    return NULL;
  }
  printf("Available USB devices:\n");
  unsigned int index = 0;
  s_usb_dev * current;
  for (current = usb_devs; current != NULL; ++current) {
    printf("%2d VID 0x%04x PID 0x%04x PATH %s\n", index++, current->vendor_id, current->product_id, current->path);
    if (current->next == 0) {
      break;
    }
  }

  printf("Select the USB device number: ");
  unsigned int choice = UINT_MAX;
  if (scanf("%d", &choice) == 1 && choice < index) {
    path = strdup(usb_devs[choice].path);
    if(path == NULL) {
      fprintf(stderr, "can't duplicate path.\n");
    }
  } else {
    fprintf(stderr, "Invalid choice.\n");
  }

  usbasync_free_enumeration(usb_devs);

  return path;
}

int main(int argc, char* argv[]) {

  if (!GE_initialize()) {
    fprintf(stderr, "GE_initialize failed\n");
    exit(-1);
  }

  (void) signal(SIGINT, terminate);
  (void) signal(SIGTERM, terminate);

  char * path = usb_select();

  if(path == NULL) {
    fprintf(stderr, "No USB device selected!\n");
    GE_quit();
    exit(-1);
  }

  usb = usbasync_open_path(path);

  if (usb >= 0) {

    const s_usb_descriptors * descriptors = usbasync_get_usb_descriptors(usb);

    printf("Opened device: VID 0x%04x PID 0x%04x PATH %s\n", descriptors->device.idVendor, descriptors->device.idProduct, path);

    usbasync_print_descriptors(usb);

    serial = serialasync_open("/dev/ttyUSB0", 500000);

    if (serial >= 0) {

      int ret = proxy_init(usb, serial);
      if(ret == 0) {

        while (!done) {

          usleep(100000);
        }
      }

      proxy_stop(serial);

      serialasync_close(serial);
    }
    else {

      fprintf(stderr, "error opening serial device\n");
    }

    usbasync_close(usb);
  }

  free(path);

  GE_quit();

  printf("Exiting\n");

  return 0;
}
