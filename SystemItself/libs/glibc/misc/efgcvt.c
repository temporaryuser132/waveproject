/* Compatibility functions for floating point formatting.
   Copyright (C) 1995, 1996, 1997, 1999 Free Software Foundation, Inc.
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <float.h>
#include <bits/libc-lock.h>

#ifndef FLOAT_TYPE
# define FLOAT_TYPE double
# define FUNC_PREFIX
# define FLOAT_FMT_FLAG
/* Actually we have to write (DBL_DIG + log10 (DBL_MAX_10_EXP)) but we
   don't have log10 available in the preprocessor.  */
# define MAXDIG (NDIGIT_MAX + 3)
# if DBL_MANT_DIG == 53
#  define NDIGIT_MAX 17
# elif DBL_MANT_DIG == 24
#  define NDIGIT_MAX 9
# elif DBL_MANT_DIG == 56
#  define NDIGIT_MAX 18
# else
/* See IEEE 854 5.6, table 2 for this formula.  Unfortunately we need a
   compile time constant here, so we cannot use it.  */
#  error "NDIGIT_MAX must be precomputed"
#  define NDIGIT_MAX (lrint (ceil (M_LN2 / M_LN10 * DBL_MANT_DIG + 1.0)))
# endif
#endif

#define APPEND(a, b) APPEND2 (a, b)
#define APPEND2(a, b) a##b


#define FCVT_BUFFER APPEND (FUNC_PREFIX, fcvt_buffer)
#define ECVT_BUFFER APPEND (FUNC_PREFIX, ecvt_buffer)


static char FCVT_BUFFER[MAXDIG];
static char ECVT_BUFFER[MAXDIG];


char *
APPEND (FUNC_PREFIX, fcvt) (value, ndigit, decpt, sign)
     FLOAT_TYPE value;
     int ndigit, *decpt, *sign;
{
  (void) APPEND (FUNC_PREFIX, fcvt_r) (value, ndigit, decpt, sign,
				       FCVT_BUFFER, MAXDIG);

  return FCVT_BUFFER;
}


char *
APPEND (FUNC_PREFIX, ecvt) (value, ndigit, decpt, sign)
     FLOAT_TYPE value;
     int ndigit, *decpt, *sign;
{
  (void) APPEND (FUNC_PREFIX, ecvt_r) (value, ndigit, decpt, sign,
				       ECVT_BUFFER, MAXDIG);

  return ECVT_BUFFER;
}

char *
APPEND (FUNC_PREFIX, gcvt) (value, ndigit, buf)
     FLOAT_TYPE value;
     int ndigit;
     char *buf;
{
  sprintf (buf, "%.*" FLOAT_FMT_FLAG "g", MIN (ndigit, NDIGIT_MAX), value);
  return buf;
}
