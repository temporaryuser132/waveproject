/* Copyright (C) 1991, 1992, 1996, 1997, 1998 Free Software Foundation, Inc.
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

/*
 *	POSIX Standard: 5.6.6 Set File Access and Modification Times  <utime.h>
 */

#ifndef	_UTIME_H
#define	_UTIME_H	1

#include <features.h>

__BEGIN_DECLS

#include <bits/types.h>

#ifdef __USE_XOPEN
# define __need_time_t
# include <time.h>
#endif

/* Structure describing file times.  */
struct utimbuf
  {
    __time_t actime;		/* Access time.  */
    __time_t modtime;		/* Modification time.  */
  };

/* Set the access and modification times of FILE to those given in
   *FILE_TIMES.  If FILE_TIMES is NULL, set them to the current time.  */
extern int utime __P ((__const char *__file,
		       __const struct utimbuf *__file_times));

__END_DECLS

#endif /* utime.h */
