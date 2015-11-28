
#ifndef EVENTS_H_
#define EVENTS_H_

#include <GE.h>

#define MAX_EVENTS 256

int ev_init();
void ev_quit();
void ev_pump_events();
void ev_register_source(int fd, int id, int (*fp_read)(int), int (*fp_write)(int), int (*fp_cleanup)(int));
#ifdef WIN32
void ev_register_source_handle(HANDLE handle, int id, int (*fp_read)(int), int (*fp_write)(int), int (*fp_cleanup)(int));
void ev_remove_source_handle(HANDLE handle);
#endif
void ev_remove_source(int fd);

#endif /* EVENTS_H_ */
