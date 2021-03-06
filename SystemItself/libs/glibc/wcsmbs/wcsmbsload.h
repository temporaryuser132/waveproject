/* Copyright (C) 1998 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1998.

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

#include <locale.h>
#include <wchar.h>
#include <locale/localeinfo.h>


/* Contains pointers to the used functions in the `gconv' modules.  */
struct gconv_fcts
  {
    struct gconv_step *towc;
    struct gconv_step *tomb;
  };

/* Set of currently active conversion functions.  */
extern struct gconv_fcts __wcsmbs_gconv_fcts;


/* Last loaded locale for LC_CTYPE.  */
extern const struct locale_data *__wcsmbs_last_locale;


/* Load conversion functions for the currently selected locale.  */
extern void __wcsmbs_load_conv (const struct locale_data *new_category)
     internal_function;


/* Check whether the LC_CTYPE locale changed since the last call.
   Update the pointers appropriately.  */
static inline void
update_conversion_ptrs (void)
{
  if (__wcsmbs_last_locale != _nl_current_LC_CTYPE)
    __wcsmbs_load_conv (_nl_current_LC_CTYPE);
}
