/*
 Copyright (c) 2011 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <GE.h>
#include <events.h>
#include <stddef.h>
#include <timer.h>

static int initialized = 0;

/*
 * \bried Initializes the GE library.
 *
 * \return 1 if successful
 *         0 in case of error
 */
int GE_initialize()
{
  if (!ev_init())
  {
    return 0;
  }

  initialized = 1;

  return 1;
}

/*
 * \brief Quit the GE library (free allocated data, release devices...).
 */
void GE_quit()
{
  ev_quit();

  initialized = 0;
}

/*
 * Add an event source.
 */
void GE_AddSource(int fd, int id, int (*fp_read)(int), int (*fp_write)(int), int (*fp_cleanup)(int))
{
  ev_register_source(fd, id, fp_read, fp_write, fp_cleanup);
}

#ifdef WIN32
/*
 * Add an event source.
 */
void GE_AddSourceHandle(HANDLE handle, int id, int (*fp_read)(int), int (*fp_write)(int), int (*fp_cleanup)(int))
{
  ev_register_source_handle(handle, id, fp_read, fp_write, fp_cleanup);
}
/*
 * Remove an event source.
 */
void GE_RemoveSourceHandle(HANDLE handle)
{
  ev_remove_source_handle(handle);
}
#endif

/*
 * Remove an event source.
 */
void GE_RemoveSource(int fd)
{
  ev_remove_source(fd);
}

/*
 * \brief Get events from devices.
 *        In Linux:
 *        - it is mandatory to call GE_SetCallback once before calling this function.
 *        - if GE_TimerStart wasn't previously called, this function will block undefinitely.
 *        In Windows:
 *        - this function queues all pending events and returns.
 */
void GE_PumpEvents()
{
  ev_pump_events();
}


/*
 * \brief Start a timer to make GE_PumpEvents return periodically.
 *
 * \param period  the period of the timer (microseconds).
 */
void GE_TimerStart(int usec)
{
#ifndef WIN32
  int tfd = timer_start(usec);

  if(tfd >= 0)
  {
    ev_register_source(tfd, 0, &timer_read, NULL, &timer_close);
  }
#else
  timer_start(usec);
#endif
}

/*
 * \brief Stop the timer.
 */
void GE_TimerClose()
{
#ifndef WIN32
  int tfd = timer_get();

  if(tfd >= 0)
  {
    ev_remove_source(tfd);
  }
#endif
  timer_close(0);
}
