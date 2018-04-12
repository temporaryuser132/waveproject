/* Support for dynamic linking code in static libc.
   Copyright (C) 1996-2002, 2003 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

/* This file defines some things that for the dynamic linker are defined in
   rtld.c and dl-sysdep.c in ways appropriate to bootstrap dynamic linking.  */

#include <errno.h>
#include <libintl.h>
#include <stdlib.h>
#include <unistd.h>
#include <ldsodefs.h>
#include <dl-machine.h>
#include <bits/libc-lock.h>
#include <dl-cache.h>
#include <dl-librecon.h>
#include <unsecvars.h>
#include <hp-timing.h>

extern char *__progname;
char **_dl_argv = &__progname;	/* This is checked for some error messages.  */

/* Name of the architecture.  */
const char *_dl_platform;
size_t _dl_platformlen;

int _dl_debug_mask;
int _dl_lazy;
int _dl_dynamic_weak;

/* If nonzero print warnings about problematic situations.  */
int _dl_verbose;

/* We never do profiling.  */
const char *_dl_profile;

/* Names of shared object for which the RUNPATHs and RPATHs should be
   ignored.  */
const char *_dl_inhibit_rpath;

/* The map for the object we will profile.  */
struct link_map *_dl_profile_map;

/* This is the address of the last stack address ever used.  */
void *__libc_stack_end;

/* Path where the binary is found.  */
const char *_dl_origin_path;

/* Nonzero if runtime lookup should not update the .got/.plt.  */
int _dl_bind_not;

/* Initially empty list of loaded objects.  */
struct link_map *_dl_loaded;
/* Number of object in the _dl_loaded list.  */
unsigned int _dl_nloaded;

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

/* Nonzero during startup.  */
int _dl_starting_up = 1;

/* Get architecture specific initializer.  */
#include <dl-procinfo.c>

/* We expect less than a second for relocation.  */
#ifdef HP_SMALL_TIMING_AVAIL
# undef HP_TIMING_AVAIL
# define HP_TIMING_AVAIL HP_SMALL_TIMING_AVAIL
#endif

/* Initial value of the CPU clock.  */
#ifndef HP_TIMING_NONAVAIL
hp_timing_t _dl_cpuclock_offset;
#endif

/* This is zero at program start to signal that the global scope map is
   allocated by rtld.  Later it keeps the size of the map.  It might be
   reset if in _dl_close if the last global object is removed.  */
size_t _dl_global_scope_alloc;

size_t _dl_pagesize;

unsigned int _dl_osversion;

/* All known directories in sorted order.  */
struct r_search_path_elem *_dl_all_dirs;

/* All directories after startup.  */
struct r_search_path_elem *_dl_init_all_dirs;

/* The object to be initialized first.  */
struct link_map *_dl_initfirst;

/* Descriptor to write debug messages to.  */
int _dl_debug_fd = STDERR_FILENO;

int _dl_correct_cache_id = _DL_CACHE_DEFAULT_ID;

struct ElfW(Phdr) *_dl_phdr;
size_t _dl_phnum;

#ifdef NEED_DL_SYSINFO
/* Needed for improved syscall handling on at least x86/Linux.  */
uintptr_t _dl_sysinfo = DL_SYSINFO_DEFAULT;
#endif

/* During the program run we must not modify the global data of
   loaded shared object simultanously in two threads.  Therefore we
   protect `_dl_open' and `_dl_close' in dl-close.c.

   This must be a recursive lock since the initializer function of
   the loaded object might as well require a call to this function.
   At this time it is not anymore a problem to modify the tables.  */
/* KV
__rtld_lock_define_initialized_recursive (, _dl_load_lock)
*/

#ifdef HAVE_AUX_VECTOR
int _dl_clktck;

void
internal_function
_dl_aux_init (ElfW(auxv_t) *av)
{
  for (; av->a_type != AT_NULL; ++av)
    switch (av->a_type)
      {
      case AT_PAGESZ:
	GL(dl_pagesize) = av->a_un.a_val;
	break;
      case AT_CLKTCK:
	GL(dl_clktck) = av->a_un.a_val;
	break;
      case AT_PHDR:
	GL(dl_phdr) = av->a_un.a_ptr;
	break;
      case AT_PHNUM:
	GL(dl_phnum) = av->a_un.a_val;
	break;
#ifdef NEED_DL_SYSINFO
      case AT_SYSINFO:
	GL(dl_sysinfo) = av->a_un.a_val;
	break;
#endif
      }
}
#endif


void
internal_function
_dl_non_dynamic_init (void)
{
  if (HP_TIMING_AVAIL)
    HP_TIMING_NOW (_dl_cpuclock_offset);

  if (!_dl_pagesize)
    _dl_pagesize = __getpagesize ();

  _dl_verbose = *(getenv ("LD_WARN") ?: "") == '\0' ? 0 : 1;

  /* Initialize the data structures for the search paths for shared
     objects.  */
  _dl_init_paths (getenv ("LD_LIBRARY_PATH"));

  _dl_lazy = *(getenv ("LD_BIND_NOW") ?: "") == '\0';

  _dl_bind_not = *(getenv ("LD_BIND_NOT") ?: "") != '\0';

  _dl_dynamic_weak = *(getenv ("LD_DYNAMIC_WEAK") ?: "") == '\0';

  if (__libc_enable_secure)
    {
      static const char *unsecure_envvars[] =
      {
	UNSECURE_ENVVARS,
#ifdef EXTRA_UNSECURE_ENVVARS
	EXTRA_UNSECURE_ENVVARS
#endif
      };
      size_t cnt;

      for (cnt = 0;
	   cnt < sizeof (unsecure_envvars) / sizeof (unsecure_envvars[0]);
	   ++cnt)
	unsetenv (unsecure_envvars[cnt]);

      if (__access ("/etc/suid-debug", F_OK) != 0)
	unsetenv ("MALLOC_CHECK_");
    }

#ifdef DL_PLATFORM_INIT
  DL_PLATFORM_INIT;
#endif

#ifdef DL_OSVERSION_INIT
  DL_OSVERSION_INIT;
#endif

  /* Now determine the length of the platform string.  */
  if (_dl_platform != NULL)
    _dl_platformlen = strlen (_dl_platform);
}


const struct r_strlenpair *
internal_function
_dl_important_hwcaps (const char *platform, size_t platform_len, size_t *sz,
		      size_t *max_capstrlen)
{
  static struct r_strlenpair result;
  static char buf[1];

  result.str = buf;	/* Does not really matter.  */
  result.len = 0;

  *sz = 1;
  return &result;
}


#ifdef DL_SYSINFO_IMPLEMENTATION
DL_SYSINFO_IMPLEMENTATION
#endif
