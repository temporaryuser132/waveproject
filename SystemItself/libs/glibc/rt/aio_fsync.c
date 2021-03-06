/* Synchronize I/O in given file descriptor.
   Copyright (C) 1997, 1999 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1997.

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


/* We use an UGLY hack to prevent gcc from finding us cheating.  The
   implementation of aio_fsync and aio_fsync64 are identical and so
   we want to avoid code duplication by using aliases.  But gcc sees
   the different parameter lists and prints a warning.  We define here
   a function so that aio_fsync64 has no prototype.  */
#define aio_fsync64 XXX
#include <aio.h>
/* And undo the hack.  */
#undef aio_fsync64
#include <errno.h>

#include "aio_misc.h"


int
aio_fsync (int op, struct aiocb *aiocbp)
{
  if (op != O_DSYNC && op != O_SYNC)
    {
      __set_errno (EINVAL);
      return -1;
    }

  return (__aio_enqueue_request ((aiocb_union *) aiocbp,
				 op == O_SYNC ? LIO_SYNC : LIO_DSYNC) == NULL
	  ? -1 : 0);
}

weak_alias (aio_fsync, aio_fsync64)
