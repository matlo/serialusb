/*
 Copyright (c) 2015 Mathieu Laurendeau
 License: GPLv3
 */

#include <stdio.h>
#include <poll.h>
#include "events.h"
#include <string.h>

static struct {
  int id;
  int (*fp_read)(int);
  int (*fp_write)(int);
  int (*fp_cleanup)(int);
  short int event;
} sources[FD_SETSIZE] = { };

static int max_source = 0;

void ev_register_source(int fd, int id, int (*fp_read)(int), int (*fp_write)(int), int (*fp_cleanup)(int)) {

  if (!fp_cleanup) {
    fprintf(stderr, "%s: the cleanup function is mandatory.", __FUNCTION__);
    return;
  }
  if (fd < FD_SETSIZE) {
    sources[fd].id = id;
    if (fp_read) {
      sources[fd].event |= POLLIN;
      sources[fd].fp_read = fp_read;
    }
    if (fp_write) {
      sources[fd].event |= POLLOUT;
      sources[fd].fp_write = fp_write;
    }
    sources[fd].fp_cleanup = fp_cleanup;
    if (fd > max_source) {
      max_source = fd;
    }
  }
}

void ev_remove_source(int fd) {

  if (fd < FD_SETSIZE) {
    memset(sources + fd, 0x00, sizeof(*sources));
  }
}

int ev_init() {
  return 1;
}

void ev_quit(void) {
  //TODO MLA: close all event sources?
}

static unsigned int fill_fds(nfds_t nfds, struct pollfd fds[nfds]) {

  unsigned int pos = 0;

  unsigned int i;
  for (i = 0; i < nfds; ++i) {
    if (sources[i].event) {
      fds[pos].fd = i;
      fds[pos].events = sources[i].event;
      ++pos;
    }
  }

  return pos;
}

void ev_pump_events(void) {

  unsigned int i;
  int res;

  while (1) {

    struct pollfd fds[max_source + 1];
    nfds_t nfds = fill_fds(max_source + 1, fds);

    if (poll(fds, nfds, -1) > 0) {
      for (i = 0; i < nfds; ++i) {
        if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
          res = sources[fds[i].fd].fp_cleanup(sources[fds[i].fd].id);
          ev_remove_source(fds[i].fd);
          if (res) {
            return;
          }
          continue;
        }
        if (fds[i].revents & POLLIN) {
          if (sources[fds[i].fd].fp_read(sources[fds[i].fd].id)) {
            return;
          }
        }
        if (fds[i].revents & POLLOUT) {
          if (sources[fds[i].fd].fp_write(sources[fds[i].fd].id)) {
            return;
          }
        }
      }
    }
  }
}
