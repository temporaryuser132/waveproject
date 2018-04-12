/* Support for dynamic linking code in static libc.
   Copyright (C) 1996, 1997, 1998, 1999 Free Software Foundation, Inc.
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

/* This file defines some things that for the dynamic linker are defined in
   rtld.c and dl-sysdep.c in ways appropriate to bootstrap dynamic linking.  */

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <elf/ldsodefs.h>
#include <dl-machine.h>

extern char *__progname;
char **_dl_argv = &__progname;	/* This is checked for some error messages.  */

/* Name of the architecture.  */
const char *_dl_platform;
size_t _dl_platformlen;

int _dl_debug_libs;
int _dl_debug_impcalls;
int _dl_debug_bindings;
int _dl_debug_symbols;
int _dl_debug_versions;
int _dl_debug_reloc;
int _dl_debug_files;
int _dl_lazy;

/* If nonzero print warnings about problematic situations.  */
int _dl_verbose;

/* Structure to store information about search paths.  */
struct r_search_path *_dl_search_paths;

/* We never do profiling.  */
const char *_dl_profile;

/* Names of shared object for which the RPATHs should be ignored.  */
const char *_dl_inhibit_rpath;

/* The map for the object we will profile.  */
struct link_map *_dl_profile_map;

/* This is the address of the last stack address ever used.  */
void *__libc_stack_end;

/* Path where the binary is found.  */
const char *_dl_origin_path;

/* Initially empty list of loaded objects.  */
struct link_map *_dl_loaded;

/* Fake scope.  In dynamically linked binaries this is the scope of the
   main application but here we don't have something like this.  So
   create a fake scope containing nothing.  */
struct r_scope_elem _dl_initial_searchlist;
/* Variable which can be used in lookup to process the global scope.  */
struct r_scope_elem *_dl_global_scope[2] = { &_dl_initial_searchlist, NULL };
/* This is a global pointer to this structure which is public.  It is
   used by dlopen/dlclose to add and remove objects from what is regarded
   to be the global scope.  */
struct r_scope_elem *_dl_main_searchlist = &_dl_initial_searchlist;


static void non_dynamic_init (void) __attribute__ ((unused));

static void
non_dynamic_init (void)
{
  _dl_verbose = *(getenv ("LD_WARN") ?: "") == '\0' ? 0 : 1;

  _dl_pagesize = __getpagesize ();

  /* Initialize the data structures for the search paths for shared
     objects.  */
  _dl_init_paths (getenv ("LD_LIBRARY_PATH"));

  _dl_lazy = *(getenv ("LD_BIND_NOW") ?: "") == '\0';

#ifdef DL_PLATFORM_INIT
  DL_PLATFORM_INIT;
#endif

  /* Now determine the length of the platform string.  */
  if (_dl_platform != NULL)
    _dl_platformlen = strlen (_dl_platform);
}
text_set_element (__libc_subinit, non_dynamic_init);

const struct r_strlenpair *
internal_function
_dl_important_hwcaps (const char *platform, size_t platform_len, size_t *sz,
		      size_t *max_capstrlen)
{
  struct r_strlenpair *result;

  /* XXX We don't try to find the capabilities in this case.  */
  result = (struct r_strlenpair *) malloc (sizeof (*result));
  if (result == NULL)
    _dl_signal_error (ENOMEM, NULL, "cannot create capability list");

  result[0].str = (char *) result;	/* Does not really matter.  */
  result[0].len = 0;

  *sz = 1;
  return result;
}
