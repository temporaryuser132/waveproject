/* Load a shared object at run time.
   Copyright (C) 1995, 1996, 1997, 1998, 1999 Free Software Foundation, Inc.
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

#ifndef __ATHEOS__

#include <dlfcn.h>
#include <stddef.h>
#include <elf/ldsodefs.h>

struct dlopen_args
{
  /* The arguments for dlopen_doit.  */
  const char *file;
  int mode;
  /* The return value of dlopen_doit.  */
  struct link_map *new;
  /* Address of the caller.  */
  const void *caller;
};


static void
dlopen_doit (void *a)
{
  struct dlopen_args *args = (struct dlopen_args *) a;

  args->new = _dl_open (args->file ?: "", args->mode, args->caller);
}


void *
__dlopen_check (const char *file, int mode)
{
  struct dlopen_args args;
  args.file = file;
  args.mode = mode;
  args.caller = __builtin_return_address (0);

  return _dlerror_run (dlopen_doit, &args) ? NULL : args.new;
}
#if defined PIC && defined DO_VERSIONING
default_symbol_version (__dlopen_check, dlopen, GLIBC_2.1);
#else
weak_alias (__dlopen_check, dlopen)
#endif

#else	/* __ATHEOS__ */

#include <dlfcn.h>
#include <stdlib.h>
#include <atheos/image.h>

void *
__dlopen(const char *file, int mode)
{
	int flags=0;
	int* handle;

	if( file == 0 )
		return( NULL );	/* FIXME: We do not set an error for dlerror() */

	handle = malloc( sizeof( int ) );

	/* FIXME: mode is currently ignored; we should attempt to */
	/* map mode onto flags...                                 */

	*handle = load_library( file, flags );
	return( (void*)handle );
}

#if defined PIC && defined DO_VERSIONING
default_symbol_version (__dlopen, dlopen, GLIBC_2.1);
#else
weak_alias (__dlopen, dlopen)
#endif

#endif	/* __ATHEOS__ */

