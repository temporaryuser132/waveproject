/* Copyright (C) 1996, 1997, 1998 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1996.

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

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>


#define LOOKUP_TYPE	struct hostent
#define FUNCTION_NAME	gethostbyname
#define DATABASE_NAME	hosts
#define ADD_PARAMS	const char *name
#define ADD_VARIABLES	name
#define NEED_H_ERRNO	1
#define NEED__RES_HCONF	1
#define POSTPROCESS \
  if (status == NSS_STATUS_SUCCESS)					      \
    _res_hconf_reorder_addrs (resbuf);

#define HANDLE_DIGITS_DOTS	1
#define HAVE_LOOKUP_BUFFER	1

#include "../nss/getXXbyYY_r.c"
