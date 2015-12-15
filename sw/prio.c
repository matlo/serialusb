/*
 Copyright (c) 2015 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <sched.h>
#include <stdio.h>

#define PRINT_ERROR_ERRNO(msg) fprintf(stderr, "%s:%d %s: %s failed with error: %m\n", __FILE__, __LINE__, __func__, msg);

int set_prio() {
  /*
   * Set highest priority & scheduler policy.
   */
  struct sched_param p = { .sched_priority = sched_get_priority_max(SCHED_FIFO) };

  if (sched_setscheduler(0, SCHED_FIFO, &p) < 0) {
    PRINT_ERROR_ERRNO("sched_setscheduler ");
    return -1;
  }
  return 0;
}
