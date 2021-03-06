/* Copyright (C) 1991,92,93,94,95,96,97,98 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include <sys/types.h>
#include <sys/time.h>
#include <hurd.h>
#include <hurd/fd.h>

/* Check the first NFDS descriptors each in READFDS (if not NULL) for read
   readiness, in WRITEFDS (if not NULL) for write readiness, and in EXCEPTFDS
   (if not NULL) for exceptional conditions.  If TIMEOUT is not NULL, time out
   after waiting the interval specified therein.  Returns the number of ready
   descriptors, or -1 for errors.  */
int
__select (nfds, readfds, writefds, exceptfds, timeout)
     int nfds;
     fd_set *readfds;
     fd_set *writefds;
     fd_set *exceptfds;
     struct timeval *timeout;
{
  struct timespec ts, *to;

  if (timeout)
    {
      to = &ts;
      TIMEVAL_TO_TIMESPEC (timeout, to);
    }
  else
    to = NULL;

  return _hurd_select (nfds, NULL, readfds, writefds, exceptfds, to, NULL);
}

weak_alias (__select, select)
