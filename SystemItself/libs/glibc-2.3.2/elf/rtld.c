/* Run time dynamic linker.
   Copyright (C) 1995-2002, 2003 Free Software Foundation, Inc.
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

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>		/* Check if MAP_ANON is defined.  */
#include <sys/param.h>
#include <sys/stat.h>
#include <ldsodefs.h>
#include <stdio-common/_itoa.h>
#include <entry.h>
#include <fpu_control.h>
#include <hp-timing.h>
#include <bits/libc-lock.h>
#include "dynamic-link.h"
#include "dl-librecon.h"
#include <unsecvars.h>
#include <dl-cache.h>
#include <dl-procinfo.h>
#include <tls.h>

#include <assert.h>

/* Avoid PLT use for our local calls at startup.  */
extern __typeof (__mempcpy) __mempcpy attribute_hidden;

/* GCC has mental blocks about _exit.  */
extern __typeof (_exit) exit_internal asm ("_exit") attribute_hidden;
#define _exit exit_internal

/* Helper function to handle errors while resolving symbols.  */
static void print_unresolved (int errcode, const char *objname,
			      const char *errsting);

/* Helper function to handle errors when a version is missing.  */
static void print_missing_version (int errcode, const char *objname,
				   const char *errsting);

/* Print the various times we collected.  */
static void print_statistics (void);

/* This is a list of all the modes the dynamic loader can be in.  */
enum mode { normal, list, verify, trace };

/* Process all environments variables the dynamic linker must recognize.
   Since all of them start with `LD_' we are a bit smarter while finding
   all the entries.  */
static void process_envvars (enum mode *modep);

int _dl_argc attribute_hidden;
char **_dl_argv = NULL;
INTDEF(_dl_argv)

/* Nonzero if we were run directly.  */
unsigned int _dl_skip_args attribute_hidden;

/* Set nonzero during loading and initialization of executable and
   libraries, cleared before the executable's entry point runs.  This
   must not be initialized to nonzero, because the unused dynamic
   linker loaded in for libc.so's "ld.so.1" dep will provide the
   definition seen by libc.so's initializer; that value must be zero,
   and will be since that dynamic linker's _dl_start and dl_main will
   never be called.  */
int _dl_starting_up = 0;
INTVARDEF(_dl_starting_up)

/* This is the structure which defines all variables global to ld.so
   (except those which cannot be added for some reason).  */
struct rtld_global _rtld_global =
  {
    /* Get architecture specific initializer.  */
#include <dl-procinfo.c>
    ._dl_debug_fd = STDERR_FILENO,
#ifdef NEED_DL_SYSINFO
    ._dl_sysinfo = DL_SYSINFO_DEFAULT,
#endif
    ._dl_lazy = 1,
    ._dl_fpu_control = _FPU_DEFAULT,
    ._dl_correct_cache_id = _DL_CACHE_DEFAULT_ID,
    ._dl_hwcap_mask = HWCAP_IMPORTANT,
#ifdef _LIBC_REENTRANT
    ._dl_load_lock = _RTLD_LOCK_RECURSIVE_INITIALIZER
#endif
  };
strong_alias (_rtld_global, _rtld_local);

static void dl_main (const ElfW(Phdr) *phdr, ElfW(Word) phnum,
		     ElfW(Addr) *user_entry);

static struct libname_list _dl_rtld_libname;
static struct libname_list _dl_rtld_libname2;

/* We expect less than a second for relocation.  */
#ifdef HP_SMALL_TIMING_AVAIL
# undef HP_TIMING_AVAIL
# define HP_TIMING_AVAIL HP_SMALL_TIMING_AVAIL
#endif

/* Variable for statistics.  */
#ifndef HP_TIMING_NONAVAIL
static hp_timing_t rtld_total_time;
static hp_timing_t relocate_time;
static hp_timing_t load_time;
static hp_timing_t start_time;
#endif

/* Additional definitions needed by TLS initialization.  */
#ifdef TLS_INIT_HELPER
TLS_INIT_HELPER
#endif

/* Helper function for syscall implementation.  */
#ifdef DL_SYSINFO_IMPLEMENTATION
DL_SYSINFO_IMPLEMENTATION
#endif

/* Before ld.so is relocated we must not access variables which need
   relocations.  This means variables which are exported.  Variables
   declared as static are fine.  If we can mark a variable hidden this
   is fine, too.  The latter is impotant here.  We can avoid setting
   up a temporary link map for ld.so if we can mark _rtld_global as
   hidden.  */
#if defined PI_STATIC_AND_HIDDEN && defined HAVE_HIDDEN \
    && defined HAVE_VISIBILITY_ATTRIBUTE
# define DONT_USE_BOOTSTRAP_MAP	1
#endif

#ifdef DONT_USE_BOOTSTRAP_MAP
static ElfW(Addr) _dl_start_final (void *arg);
#else
struct dl_start_final_info
{
  struct link_map l;
#if !defined HP_TIMING_NONAVAIL && HP_TIMING_INLINE
  hp_timing_t start_time;
#endif
};
static ElfW(Addr) _dl_start_final (void *arg,
				   struct dl_start_final_info *info);
#endif

/* These defined magically in the linker script.  */
extern char _begin[] attribute_hidden;
extern char _end[] attribute_hidden;


#ifdef RTLD_START
RTLD_START
#else
# error "sysdeps/MACHINE/dl-machine.h fails to define RTLD_START"
#endif

#ifndef VALIDX
# define VALIDX(tag) (DT_NUM + DT_THISPROCNUM + DT_VERSIONTAGNUM \
		      + DT_EXTRANUM + DT_VALTAGIDX (tag))
#endif
#ifndef ADDRIDX
# define ADDRIDX(tag) (DT_NUM + DT_THISPROCNUM + DT_VERSIONTAGNUM \
		       + DT_EXTRANUM + DT_VALNUM + DT_ADDRTAGIDX (tag))
#endif

/* This is the second half of _dl_start (below).  It can be inlined safely
   under DONT_USE_BOOTSTRAP_MAP, where it is careful not to make any GOT
   references.  When the tools don't permit us to avoid using a GOT entry
   for _dl_rtld_global (no attribute_hidden support), we must make sure
   this function is not inlined (see below).  */

#ifdef DONT_USE_BOOTSTRAP_MAP
static inline ElfW(Addr) __attribute__ ((always_inline))
_dl_start_final (void *arg)
#else
static ElfW(Addr) __attribute__ ((noinline))
_dl_start_final (void *arg, struct dl_start_final_info *info)
#endif
{
  ElfW(Addr) start_addr;

  if (HP_TIMING_AVAIL)
    {
      /* If it hasn't happen yet record the startup time.  */
      if (! HP_TIMING_INLINE)
	HP_TIMING_NOW (start_time);
#if !defined DONT_USE_BOOTSTRAP_MAP && !defined HP_TIMING_NONAVAIL
      else
	start_time = info->start_time;
#endif

      /* Initialize the timing functions.  */
      HP_TIMING_DIFF_INIT ();
    }

  /* Transfer data about ourselves to the permanent link_map structure.  */
#ifndef DONT_USE_BOOTSTRAP_MAP
  GL(dl_rtld_map).l_addr = info->l.l_addr;
  GL(dl_rtld_map).l_ld = info->l.l_ld;
  memcpy (GL(dl_rtld_map).l_info, info->l.l_info,
	  sizeof GL(dl_rtld_map).l_info);
  GL(dl_rtld_map).l_mach = info->l.l_mach;
#endif
  _dl_setup_hash (&GL(dl_rtld_map));
  GL(dl_rtld_map).l_opencount = 1;
  GL(dl_rtld_map).l_map_start = (ElfW(Addr)) _begin;
  GL(dl_rtld_map).l_map_end = (ElfW(Addr)) _end;
  /* Copy the TLS related data if necessary.  */
#if USE_TLS && !defined DONT_USE_BOOTSTRAP_MAP
# if USE___THREAD
  assert (info->l.l_tls_modid != 0);
  GL(dl_rtld_map).l_tls_blocksize = info->l.l_tls_blocksize;
  GL(dl_rtld_map).l_tls_align = info->l.l_tls_align;
  GL(dl_rtld_map).l_tls_initimage_size = info->l.l_tls_initimage_size;
  GL(dl_rtld_map).l_tls_initimage = info->l.l_tls_initimage;
  GL(dl_rtld_map).l_tls_offset = info->l.l_tls_offset;
  GL(dl_rtld_map).l_tls_modid = 1;
# else
  assert (info->l.l_tls_modid == 0);
# endif

#endif

#if HP_TIMING_AVAIL
  HP_TIMING_NOW (GL(dl_cpuclock_offset));
#endif

  /* Call the OS-dependent function to set up life so we can do things like
     file access.  It will call `dl_main' (below) to do all the real work
     of the dynamic linker, and then unwind our frame and run the user
     entry point on the same stack we entered on.  */
  start_addr =  _dl_sysdep_start (arg, &dl_main);

#ifndef HP_TIMING_NONAVAIL
  if (HP_TIMING_AVAIL)
    {
      hp_timing_t end_time;

      /* Get the current time.  */
      HP_TIMING_NOW (end_time);

      /* Compute the difference.  */
      HP_TIMING_DIFF (rtld_total_time, start_time, end_time);
    }
#endif

  if (__builtin_expect (GL(dl_debug_mask) & DL_DEBUG_STATISTICS, 0))
    print_statistics ();

  return start_addr;
}

static ElfW(Addr) __attribute_used__ internal_function
_dl_start (void *arg)
{
#ifdef DONT_USE_BOOTSTRAP_MAP
# define bootstrap_map GL(dl_rtld_map)
#else
  struct dl_start_final_info info;
# define bootstrap_map info.l
#endif

  /* This #define produces dynamic linking inline functions for
     bootstrap relocation instead of general-purpose relocation.  */
#define RTLD_BOOTSTRAP
#define RESOLVE_MAP(sym, version, flags) \
  ((*(sym))->st_shndx == SHN_UNDEF ? 0 : &bootstrap_map)
#define RESOLVE(sym, version, flags) \
  ((*(sym))->st_shndx == SHN_UNDEF ? 0 : bootstrap_map.l_addr)
#include "dynamic-link.h"

  if (HP_TIMING_INLINE && HP_TIMING_AVAIL)
#ifdef DONT_USE_BOOTSTRAP_MAP
    HP_TIMING_NOW (start_time);
#else
    HP_TIMING_NOW (info.start_time);
#endif

  /* Partly clean the `bootstrap_map' structure up.  Don't use
     `memset' since it might not be built in or inlined and we cannot
     make function calls at this point.  Use '__builtin_memset' if we
     know it is available.  We do not have to clear the memory if we
     do not have to use the temporary bootstrap_map.  Global variables
     are initialized to zero by default.  */
#ifndef DONT_USE_BOOTSTRAP_MAP
# ifdef HAVE_BUILTIN_MEMSET
  __builtin_memset (bootstrap_map.l_info, '\0', sizeof (bootstrap_map.l_info));
# else
  for (size_t cnt = 0;
       cnt < sizeof (bootstrap_map.l_info) / sizeof (bootstrap_map.l_info[0]);
       ++cnt)
    bootstrap_map.l_info[cnt] = 0;
# endif
#endif

  /* Figure out the run-time load address of the dynamic linker itself.  */
  bootstrap_map.l_addr = elf_machine_load_address ();

  /* Read our own dynamic section and fill in the info array.  */
  bootstrap_map.l_ld = (void *) bootstrap_map.l_addr + elf_machine_dynamic ();
  elf_get_dynamic_info (&bootstrap_map);

#if USE___THREAD
  /* Get the dynamic linker's own program header.  First we need the ELF
     file header.  The `_begin' symbol created by the linker script points
     to it.  When we have something like GOTOFF relocs, we can use a plain
     reference to find the runtime address.  Without that, we have to rely
     on the `l_addr' value, which is not the value we want when prelinked.  */
  dtv_t initdtv[3];
  ElfW(Ehdr) *ehdr
# ifdef DONT_USE_BOOTSTRAP_MAP
    = (ElfW(Ehdr) *) &_begin;
# else
    = (ElfW(Ehdr) *) bootstrap_map.l_addr;
# endif
  ElfW(Phdr) *phdr = (ElfW(Phdr) *) ((void *) ehdr + ehdr->e_phoff);
  size_t cnt = ehdr->e_phnum;	/* PT_TLS is usually the last phdr.  */
  while (cnt-- > 0)
    if (phdr[cnt].p_type == PT_TLS)
      {
	void *tlsblock;
	size_t max_align = MAX (TLS_INIT_TCB_ALIGN, phdr[cnt].p_align);
	char *p;

	bootstrap_map.l_tls_blocksize = phdr[cnt].p_memsz;
	bootstrap_map.l_tls_align = phdr[cnt].p_align;
	assert (bootstrap_map.l_tls_blocksize != 0);
	bootstrap_map.l_tls_initimage_size = phdr[cnt].p_filesz;
	bootstrap_map.l_tls_initimage = (void *) (bootstrap_map.l_addr
						  + phdr[cnt].p_vaddr);

	/* We can now allocate the initial TLS block.  This can happen
	   on the stack.  We'll get the final memory later when we
	   know all about the various objects loaded at startup
	   time.  */
# if TLS_TCB_AT_TP
	tlsblock = alloca (roundup (bootstrap_map.l_tls_blocksize,
				    TLS_INIT_TCB_ALIGN)
			   + TLS_INIT_TCB_SIZE
			   + max_align);
# elif TLS_DTV_AT_TP
	tlsblock = alloca (roundup (TLS_INIT_TCB_SIZE,
				    bootstrap_map.l_tls_align)
			   + bootstrap_map.l_tls_blocksize
			   + max_align);
# else
	/* In case a model with a different layout for the TCB and DTV
	   is defined add another #elif here and in the following #ifs.  */
#  error "Either TLS_TCB_AT_TP or TLS_DTV_AT_TP must be defined"
# endif
	/* Align the TLS block.  */
	tlsblock = (void *) (((uintptr_t) tlsblock + max_align - 1)
			     & ~(max_align - 1));

	/* Initialize the dtv.  [0] is the length, [1] the generation
	   counter.  */
	initdtv[0].counter = 1;
	initdtv[1].counter = 0;

	/* Initialize the TLS block.  */
# if TLS_TCB_AT_TP
	initdtv[2].pointer = tlsblock;
# elif TLS_DTV_AT_TP
	bootstrap_map.l_tls_offset = roundup (TLS_INIT_TCB_SIZE,
					      bootstrap_map.l_tls_align);
	initdtv[2].pointer = (char *) tlsblock + bootstrap_map.l_tls_offset;
# else
#  error "Either TLS_TCB_AT_TP or TLS_DTV_AT_TP must be defined"
# endif
	p = __mempcpy (initdtv[2].pointer, bootstrap_map.l_tls_initimage,
		       bootstrap_map.l_tls_initimage_size);
# ifdef HAVE_BUILTIN_MEMSET
	__builtin_memset (p, '\0', (bootstrap_map.l_tls_blocksize
				    - bootstrap_map.l_tls_initimage_size));
# else
	{
	  size_t remaining = (bootstrap_map.l_tls_blocksize
			      - bootstrap_map.l_tls_initimage_size);
	  while (remaining-- > 0)
	    *p++ = '\0';
	}
#endif

	/* Install the pointer to the dtv.  */

	/* Initialize the thread pointer.  */
# if TLS_TCB_AT_TP
	bootstrap_map.l_tls_offset
	  = roundup (bootstrap_map.l_tls_blocksize, TLS_INIT_TCB_ALIGN);

	INSTALL_DTV ((char *) tlsblock + bootstrap_map.l_tls_offset,
		     initdtv);

	const char *lossage = TLS_INIT_TP ((char *) tlsblock
					   + bootstrap_map.l_tls_offset, 0);
# elif TLS_DTV_AT_TP
	INSTALL_DTV (tlsblock, initdtv);
	const char *lossage = TLS_INIT_TP (tlsblock, 0);
# else
#  error "Either TLS_TCB_AT_TP or TLS_DTV_AT_TP must be defined"
# endif
	if (__builtin_expect (lossage != NULL, 0))
	  _dl_fatal_printf ("cannot set up thread-local storage: %s\n",
			    lossage);

	/* So far this is module number one.  */
	bootstrap_map.l_tls_modid = 1;

	/* There can only be one PT_TLS entry.  */
	break;
      }
#endif	/* USE___THREAD */

#ifdef ELF_MACHINE_BEFORE_RTLD_RELOC
  ELF_MACHINE_BEFORE_RTLD_RELOC (bootstrap_map.l_info);
#endif

  if (bootstrap_map.l_addr || ! bootstrap_map.l_info[VALIDX(DT_GNU_PRELINKED)])
    {
      /* Relocate ourselves so we can do normal function calls and
	 data access using the global offset table.  */

      ELF_DYNAMIC_RELOCATE (&bootstrap_map, 0, 0);
    }

  /* Please note that we don't allow profiling of this object and
     therefore need not test whether we have to allocate the array
     for the relocation results (as done in dl-reloc.c).  */

  /* Now life is sane; we can call functions and access global data.
     Set up to use the operating system facilities, and find out from
     the operating system's program loader where to find the program
     header table in core.  Put the rest of _dl_start into a separate
     function, that way the compiler cannot put accesses to the GOT
     before ELF_DYNAMIC_RELOCATE.  */
  {
#ifdef DONT_USE_BOOTSTRAP_MAP
    ElfW(Addr) entry = _dl_start_final (arg);
#else
    ElfW(Addr) entry = _dl_start_final (arg, &info);
#endif

#ifndef ELF_MACHINE_START_ADDRESS
# define ELF_MACHINE_START_ADDRESS(map, start) (start)
#endif

    return ELF_MACHINE_START_ADDRESS (GL(dl_loaded), entry);
  }
}



/* Now life is peachy; we can do all normal operations.
   On to the real work.  */

/* Some helper functions.  */

/* Arguments to relocate_doit.  */
struct relocate_args
{
  struct link_map *l;
  int lazy;
};

struct map_args
{
  /* Argument to map_doit.  */
  char *str;
  /* Return value of map_doit.  */
  struct link_map *main_map;
};

/* Arguments to version_check_doit.  */
struct version_check_args
{
  int doexit;
  int dotrace;
};

static void
relocate_doit (void *a)
{
  struct relocate_args *args = (struct relocate_args *) a;

  INTUSE(_dl_relocate_object) (args->l, args->l->l_scope, args->lazy, 0);
}

static void
map_doit (void *a)
{
  struct map_args *args = (struct map_args *) a;
  args->main_map = INTUSE(_dl_map_object) (NULL, args->str, 0, lt_library, 0, 0);
}

static void
version_check_doit (void *a)
{
  struct version_check_args *args = (struct version_check_args *) a;
  if (_dl_check_all_versions (GL(dl_loaded), 1, args->dotrace) && args->doexit)
    /* We cannot start the application.  Abort now.  */
    _exit (1);
}


static inline struct link_map *
find_needed (const char *name)
{
  unsigned int n = GL(dl_loaded)->l_searchlist.r_nlist;

  while (n-- > 0)
    if (_dl_name_match_p (name, GL(dl_loaded)->l_searchlist.r_list[n]))
      return GL(dl_loaded)->l_searchlist.r_list[n];

  /* Should never happen.  */
  return NULL;
}

static int
match_version (const char *string, struct link_map *map)
{
  const char *strtab = (const void *) D_PTR (map, l_info[DT_STRTAB]);
  ElfW(Verdef) *def;

#define VERDEFTAG (DT_NUM + DT_THISPROCNUM + DT_VERSIONTAGIDX (DT_VERDEF))
  if (map->l_info[VERDEFTAG] == NULL)
    /* The file has no symbol versioning.  */
    return 0;

  def = (ElfW(Verdef) *) ((char *) map->l_addr
			  + map->l_info[VERDEFTAG]->d_un.d_ptr);
  while (1)
    {
      ElfW(Verdaux) *aux = (ElfW(Verdaux) *) ((char *) def + def->vd_aux);

      /* Compare the version strings.  */
      if (strcmp (string, strtab + aux->vda_name) == 0)
	/* Bingo!  */
	return 1;

      /* If no more definitions we failed to find what we want.  */
      if (def->vd_next == 0)
	break;

      /* Next definition.  */
      def = (ElfW(Verdef) *) ((char *) def + def->vd_next);
    }

  return 0;
}

#ifdef _LIBC_REENTRANT
/* _dl_error_catch_tsd points to this for the single-threaded case.
   It's reset by the thread library for multithreaded programs.  */
void ** __attribute__ ((const))
_dl_initial_error_catch_tsd (void)
{
  static void *data;
  return &data;
}
#endif

static const char *library_path;	/* The library search path.  */
static const char *preloadlist;		/* The list preloaded objects.  */
static int version_info;		/* Nonzero if information about
					   versions has to be printed.  */

static void
dl_main (const ElfW(Phdr) *phdr,
	 ElfW(Word) phnum,
	 ElfW(Addr) *user_entry)
{
  const ElfW(Phdr) *ph;
  enum mode mode;
  struct link_map **preloads;
  unsigned int npreloads;
  size_t file_size;
  char *file;
  bool has_interp = false;
  unsigned int i;
  bool prelinked = false;
  bool rtld_is_main = false;
#ifndef HP_TIMING_NONAVAIL
  hp_timing_t start;
  hp_timing_t stop;
  hp_timing_t diff;
#endif
#ifdef USE_TLS
  void *tcbp;
#endif

#ifdef _LIBC_REENTRANT
  /* Explicit initialization since the reloc would just be more work.  */
  GL(dl_error_catch_tsd) = &_dl_initial_error_catch_tsd;
#endif

  /* Process the environment variable which control the behaviour.  */
  process_envvars (&mode);

  /* Set up a flag which tells we are just starting.  */
  INTUSE(_dl_starting_up) = 1;

  if (*user_entry == (ElfW(Addr)) ENTRY_POINT)
    {
      /* Ho ho.  We are not the program interpreter!  We are the program
	 itself!  This means someone ran ld.so as a command.  Well, that
	 might be convenient to do sometimes.  We support it by
	 interpreting the args like this:

	 ld.so PROGRAM ARGS...

	 The first argument is the name of a file containing an ELF
	 executable we will load and run with the following arguments.
	 To simplify life here, PROGRAM is searched for using the
	 normal rules for shared objects, rather than $PATH or anything
	 like that.  We just load it and use its entry point; we don't
	 pay attention to its PT_INTERP command (we are the interpreter
	 ourselves).  This is an easy way to test a new ld.so before
	 installing it.  */
      rtld_is_main = true;

      /* Note the place where the dynamic linker actually came from.  */
      GL(dl_rtld_map).l_name = rtld_progname;

      while (_dl_argc > 1)
	if (! strcmp (INTUSE(_dl_argv)[1], "--list"))
	  {
	    mode = list;
	    GL(dl_lazy) = -1;	/* This means do no dependency analysis.  */

	    ++_dl_skip_args;
	    --_dl_argc;
	    ++INTUSE(_dl_argv);
	  }
	else if (! strcmp (INTUSE(_dl_argv)[1], "--verify"))
	  {
	    mode = verify;

	    ++_dl_skip_args;
	    --_dl_argc;
	    ++INTUSE(_dl_argv);
	  }
	else if (! strcmp (INTUSE(_dl_argv)[1], "--library-path")
		 && _dl_argc > 2)
	  {
	    library_path = INTUSE(_dl_argv)[2];

	    _dl_skip_args += 2;
	    _dl_argc -= 2;
	    INTUSE(_dl_argv) += 2;
	  }
	else if (! strcmp (INTUSE(_dl_argv)[1], "--inhibit-rpath")
		 && _dl_argc > 2)
	  {
	    GL(dl_inhibit_rpath) = INTUSE(_dl_argv)[2];

	    _dl_skip_args += 2;
	    _dl_argc -= 2;
	    INTUSE(_dl_argv) += 2;
	  }
	else
	  break;

      /* If we have no further argument the program was called incorrectly.
	 Grant the user some education.  */
      if (_dl_argc < 2)
	_dl_fatal_printf ("\
Usage: ld.so [OPTION]... EXECUTABLE-FILE [ARGS-FOR-PROGRAM...]\n\
You have invoked `ld.so', the helper program for shared library executables.\n\
This program usually lives in the file `/lib/ld.so', and special directives\n\
in executable files using ELF shared libraries tell the system's program\n\
loader to load the helper program from this file.  This helper program loads\n\
the shared libraries needed by the program executable, prepares the program\n\
to run, and runs it.  You may invoke this helper program directly from the\n\
command line to load and run an ELF executable file; this is like executing\n\
that file itself, but always uses this helper program from the file you\n\
specified, instead of the helper program file specified in the executable\n\
file you run.  This is mostly of use for maintainers to test new versions\n\
of this helper program; chances are you did not intend to run this program.\n\
\n\
  --list                list all dependencies and how they are resolved\n\
  --verify              verify that given object really is a dynamically linked\n\
                        object we can handle\n\
  --library-path PATH   use given PATH instead of content of the environment\n\
                        variable LD_LIBRARY_PATH\n\
  --inhibit-rpath LIST  ignore RUNPATH and RPATH information in object names\n\
                        in LIST\n");

      ++_dl_skip_args;
      --_dl_argc;
      ++INTUSE(_dl_argv);

      /* Initialize the data structures for the search paths for shared
	 objects.  */
      _dl_init_paths (library_path);

      if (__builtin_expect (mode, normal) == verify)
	{
	  const char *objname;
	  const char *err_str = NULL;
	  struct map_args args;

	  args.str = rtld_progname;
	  (void) INTUSE(_dl_catch_error) (&objname, &err_str, map_doit, &args);
	  if (__builtin_expect (err_str != NULL, 0))
	    /* We don't free the returned string, the programs stops
	       anyway.  */
	    _exit (EXIT_FAILURE);
	}
      else
	{
	  HP_TIMING_NOW (start);
	  INTUSE(_dl_map_object) (NULL, rtld_progname, 0, lt_library, 0, 0);
	  HP_TIMING_NOW (stop);

	  HP_TIMING_DIFF (load_time, start, stop);
	}

      phdr = GL(dl_loaded)->l_phdr;
      phnum = GL(dl_loaded)->l_phnum;
      /* We overwrite here a pointer to a malloc()ed string.  But since
	 the malloc() implementation used at this point is the dummy
	 implementations which has no real free() function it does not
	 makes sense to free the old string first.  */
      GL(dl_loaded)->l_name = (char *) "";
      *user_entry = GL(dl_loaded)->l_entry;
    }
  else
    {
      /* Create a link_map for the executable itself.
	 This will be what dlopen on "" returns.  */
      _dl_new_object ((char *) "", "", lt_executable, NULL);
      if (GL(dl_loaded) == NULL)
	_dl_fatal_printf ("cannot allocate memory for link map\n");
      GL(dl_loaded)->l_phdr = phdr;
      GL(dl_loaded)->l_phnum = phnum;
      GL(dl_loaded)->l_entry = *user_entry;

      /* At this point we are in a bit of trouble.  We would have to
	 fill in the values for l_dev and l_ino.  But in general we
	 do not know where the file is.  We also do not handle AT_EXECFD
	 even if it would be passed up.

	 We leave the values here defined to 0.  This is normally no
	 problem as the program code itself is normally no shared
	 object and therefore cannot be loaded dynamically.  Nothing
	 prevent the use of dynamic binaries and in these situations
	 we might get problems.  We might not be able to find out
	 whether the object is already loaded.  But since there is no
	 easy way out and because the dynamic binary must also not
	 have an SONAME we ignore this program for now.  If it becomes
	 a problem we can force people using SONAMEs.  */

      /* We delay initializing the path structure until we got the dynamic
	 information for the program.  */
    }

  GL(dl_loaded)->l_map_end = 0;
  /* Perhaps the executable has no PT_LOAD header entries at all.  */
  GL(dl_loaded)->l_map_start = ~0;
  /* We opened the file, account for it.  */
  ++GL(dl_loaded)->l_opencount;

  /* Scan the program header table for the dynamic section.  */
  for (ph = phdr; ph < &phdr[phnum]; ++ph)
    switch (ph->p_type)
      {
      case PT_PHDR:
	/* Find out the load address.  */
	GL(dl_loaded)->l_addr = (ElfW(Addr)) phdr - ph->p_vaddr;
	break;
      case PT_DYNAMIC:
	/* This tells us where to find the dynamic section,
	   which tells us everything we need to do.  */
	GL(dl_loaded)->l_ld = (void *) GL(dl_loaded)->l_addr + ph->p_vaddr;
	break;
      case PT_INTERP:
	/* This "interpreter segment" was used by the program loader to
	   find the program interpreter, which is this program itself, the
	   dynamic linker.  We note what name finds us, so that a future
	   dlopen call or DT_NEEDED entry, for something that wants to link
	   against the dynamic linker as a shared library, will know that
	   the shared object is already loaded.  */
	_dl_rtld_libname.name = ((const char *) GL(dl_loaded)->l_addr
				 + ph->p_vaddr);
	/* _dl_rtld_libname.next = NULL;	Already zero.  */
	GL(dl_rtld_map).l_libname = &_dl_rtld_libname;

	/* Ordinarilly, we would get additional names for the loader from
	   our DT_SONAME.  This can't happen if we were actually linked as
	   a static executable (detect this case when we have no DYNAMIC).
	   If so, assume the filename component of the interpreter path to
	   be our SONAME, and add it to our name list.  */
	if (GL(dl_rtld_map).l_ld == NULL)
	  {
	    const char *p = NULL;
	    const char *cp = _dl_rtld_libname.name;

	    /* Find the filename part of the path.  */
	    while (*cp != '\0')
	      if (*cp++ == '/')
		p = cp;

	    if (p != NULL)
	      {
		_dl_rtld_libname2.name = p;
		/* _dl_rtld_libname2.next = NULL;  Already zero.  */
		_dl_rtld_libname.next = &_dl_rtld_libname2;
	      }
	  }

	has_interp = true;
	break;
      case PT_LOAD:
	{
	  ElfW(Addr) mapstart;
	  ElfW(Addr) allocend;

	  /* Remember where the main program starts in memory.  */
	  mapstart = (GL(dl_loaded)->l_addr
		      + (ph->p_vaddr & ~(ph->p_align - 1)));
	  if (GL(dl_loaded)->l_map_start > mapstart)
	    GL(dl_loaded)->l_map_start = mapstart;

	  /* Also where it ends.  */
	  allocend = GL(dl_loaded)->l_addr + ph->p_vaddr + ph->p_memsz;
	  if (GL(dl_loaded)->l_map_end < allocend)
	    GL(dl_loaded)->l_map_end = allocend;
	}
	break;
#ifdef USE_TLS
      case PT_TLS:
	if (ph->p_memsz > 0)
	  {
	    /* Note that in the case the dynamic linker we duplicate work
	       here since we read the PT_TLS entry already in
	       _dl_start_final.  But the result is repeatable so do not
	       check for this special but unimportant case.  */
	    GL(dl_loaded)->l_tls_blocksize = ph->p_memsz;
	    GL(dl_loaded)->l_tls_align = ph->p_align;
	    GL(dl_loaded)->l_tls_initimage_size = ph->p_filesz;
	    GL(dl_loaded)->l_tls_initimage = (void *) ph->p_vaddr;

	    /* This image gets the ID one.  */
	    GL(dl_tls_max_dtv_idx) = GL(dl_loaded)->l_tls_modid = 1;
	  }
	break;
#endif
      }
#ifdef USE_TLS
    /* Adjust the address of the TLS initialization image in case
       the executable is actually an ET_DYN object.  */
    if (GL(dl_loaded)->l_tls_initimage != NULL)
      GL(dl_loaded)->l_tls_initimage
	= (char *) GL(dl_loaded)->l_tls_initimage + GL(dl_loaded)->l_addr;
#endif
  if (! GL(dl_loaded)->l_map_end)
    GL(dl_loaded)->l_map_end = ~0;
  if (! GL(dl_rtld_map).l_libname && GL(dl_rtld_map).l_name)
    {
      /* We were invoked directly, so the program might not have a
	 PT_INTERP.  */
      _dl_rtld_libname.name = GL(dl_rtld_map).l_name;
      /* _dl_rtld_libname.next = NULL; 	Already zero.  */
      GL(dl_rtld_map).l_libname =  &_dl_rtld_libname;
    }
  else
    assert (GL(dl_rtld_map).l_libname); /* How else did we get here?  */

  if (! rtld_is_main)
    {
      /* Extract the contents of the dynamic section for easy access.  */
      elf_get_dynamic_info (GL(dl_loaded));
      if (GL(dl_loaded)->l_info[DT_HASH])
	/* Set up our cache of pointers into the hash table.  */
	_dl_setup_hash (GL(dl_loaded));
    }

  if (__builtin_expect (mode, normal) == verify)
    {
      /* We were called just to verify that this is a dynamic
	 executable using us as the program interpreter.  Exit with an
	 error if we were not able to load the binary or no interpreter
	 is specified (i.e., this is no dynamically linked binary.  */
      if (GL(dl_loaded)->l_ld == NULL)
	_exit (1);

      /* We allow here some platform specific code.  */
#ifdef DISTINGUISH_LIB_VERSIONS
      DISTINGUISH_LIB_VERSIONS;
#endif
      _exit (has_interp ? 0 : 2);
    }

  if (! rtld_is_main)
    /* Initialize the data structures for the search paths for shared
       objects.  */
    _dl_init_paths (library_path);

  /* Put the link_map for ourselves on the chain so it can be found by
     name.  Note that at this point the global chain of link maps contains
     exactly one element, which is pointed to by dl_loaded.  */
  if (! GL(dl_rtld_map).l_name)
    /* If not invoked directly, the dynamic linker shared object file was
       found by the PT_INTERP name.  */
    GL(dl_rtld_map).l_name = (char *) GL(dl_rtld_map).l_libname->name;
  GL(dl_rtld_map).l_type = lt_library;
  GL(dl_loaded)->l_next = &GL(dl_rtld_map);
  GL(dl_rtld_map).l_prev = GL(dl_loaded);
  ++GL(dl_nloaded);

  /* We have two ways to specify objects to preload: via environment
     variable and via the file /etc/ld.so.preload.  The latter can also
     be used when security is enabled.  */
  preloads = NULL;
  npreloads = 0;

  if (__builtin_expect (preloadlist != NULL, 0))
    {
      /* The LD_PRELOAD environment variable gives list of libraries
	 separated by white space or colons that are loaded before the
	 executable's dependencies and prepended to the global scope
	 list.  If the binary is running setuid all elements
	 containing a '/' are ignored since it is insecure.  */
      char *list = strdupa (preloadlist);
      char *p;

      HP_TIMING_NOW (start);

      /* Prevent optimizing strsep.  Speed is not important here.  */
      while ((p = (strsep) (&list, " :")) != NULL)
	if (p[0] != '\0'
	    && (__builtin_expect (! INTUSE(__libc_enable_secure), 1)
		|| strchr (p, '/') == NULL))
	  {
	    struct link_map *new_map = INTUSE(_dl_map_object) (GL(dl_loaded),
							       p, 1,
							       lt_library,
							       0, 0);
	    if (++new_map->l_opencount == 1)
	      /* It is no duplicate.  */
	      ++npreloads;
	  }

      HP_TIMING_NOW (stop);
      HP_TIMING_DIFF (diff, start, stop);
      HP_TIMING_ACCUM_NT (load_time, diff);
    }

  /* Read the contents of the file.  */
  file = _dl_sysdep_read_whole_file ("/etc/ld.so.preload", &file_size,
				     PROT_READ | PROT_WRITE);
  if (__builtin_expect (file != MAP_FAILED, 0))
    {
      /* Parse the file.  It contains names of libraries to be loaded,
	 separated by white spaces or `:'.  It may also contain
	 comments introduced by `#'.  */
      char *problem;
      char *runp;
      size_t rest;

      /* Eliminate comments.  */
      runp = file;
      rest = file_size;
      while (rest > 0)
	{
	  char *comment = memchr (runp, '#', rest);
	  if (comment == NULL)
	    break;

	  rest -= comment - runp;
	  do
	    *comment = ' ';
	  while (--rest > 0 && *++comment != '\n');
	}

      /* We have one problematic case: if we have a name at the end of
	 the file without a trailing terminating characters, we cannot
	 place the \0.  Handle the case separately.  */
      if (file[file_size - 1] != ' ' && file[file_size - 1] != '\t'
	  && file[file_size - 1] != '\n' && file[file_size - 1] != ':')
	{
	  problem = &file[file_size];
	  while (problem > file && problem[-1] != ' ' && problem[-1] != '\t'
		 && problem[-1] != '\n' && problem[-1] != ':')
	    --problem;

	  if (problem > file)
	    problem[-1] = '\0';
	}
      else
	{
	  problem = NULL;
	  file[file_size - 1] = '\0';
	}

      HP_TIMING_NOW (start);

      if (file != problem)
	{
	  char *p;
	  runp = file;
	  while ((p = strsep (&runp, ": \t\n")) != NULL)
	    if (p[0] != '\0')
	      {
		struct link_map *new_map = INTUSE(_dl_map_object) (GL(dl_loaded),
								   p, 1,
								   lt_library,
								   0, 0);
		if (++new_map->l_opencount == 1)
		  /* It is no duplicate.  */
		  ++npreloads;
	      }
	}

      if (problem != NULL)
	{
	  char *p = strndupa (problem, file_size - (problem - file));
	  struct link_map *new_map = INTUSE(_dl_map_object) (GL(dl_loaded), p,
							     1, lt_library,
							     0, 0);
	  if (++new_map->l_opencount == 1)
	    /* It is no duplicate.  */
	    ++npreloads;
	}

      HP_TIMING_NOW (stop);
      HP_TIMING_DIFF (diff, start, stop);
      HP_TIMING_ACCUM_NT (load_time, diff);

      /* We don't need the file anymore.  */
      __munmap (file, file_size);
    }

  if (__builtin_expect (npreloads, 0) != 0)
    {
      /* Set up PRELOADS with a vector of the preloaded libraries.  */
      struct link_map *l;
      preloads = __alloca (npreloads * sizeof preloads[0]);
      l = GL(dl_rtld_map).l_next; /* End of the chain before preloads.  */
      i = 0;
      do
	{
	  preloads[i++] = l;
	  l = l->l_next;
	} while (l);
      assert (i == npreloads);
    }

  /* Load all the libraries specified by DT_NEEDED entries.  If LD_PRELOAD
     specified some libraries to load, these are inserted before the actual
     dependencies in the executable's searchlist for symbol resolution.  */
  HP_TIMING_NOW (start);
  INTUSE(_dl_map_object_deps) (GL(dl_loaded), preloads, npreloads,
			       mode == trace, 0);
  HP_TIMING_NOW (stop);
  HP_TIMING_DIFF (diff, start, stop);
  HP_TIMING_ACCUM_NT (load_time, diff);

  /* Mark all objects as being in the global scope and set the open
     counter.  */
  for (i = GL(dl_loaded)->l_searchlist.r_nlist; i > 0; )
    {
      --i;
      GL(dl_loaded)->l_searchlist.r_list[i]->l_global = 1;
      ++GL(dl_loaded)->l_searchlist.r_list[i]->l_opencount;
    }

#ifndef MAP_ANON
  /* We are done mapping things, so close the zero-fill descriptor.  */
  __close (_dl_zerofd);
  _dl_zerofd = -1;
#endif

  /* Remove _dl_rtld_map from the chain.  */
  GL(dl_rtld_map).l_prev->l_next = GL(dl_rtld_map).l_next;
  if (GL(dl_rtld_map).l_next)
    GL(dl_rtld_map).l_next->l_prev = GL(dl_rtld_map).l_prev;

  if (__builtin_expect (GL(dl_rtld_map).l_opencount > 1, 1))
    {
      /* Some DT_NEEDED entry referred to the interpreter object itself, so
	 put it back in the list of visible objects.  We insert it into the
	 chain in symbol search order because gdb uses the chain's order as
	 its symbol search order.  */
      i = 1;
      while (GL(dl_loaded)->l_searchlist.r_list[i] != &GL(dl_rtld_map))
	++i;
      GL(dl_rtld_map).l_prev = GL(dl_loaded)->l_searchlist.r_list[i - 1];
      if (__builtin_expect (mode, normal) == normal)
	GL(dl_rtld_map).l_next = (i + 1 < GL(dl_loaded)->l_searchlist.r_nlist
				  ? GL(dl_loaded)->l_searchlist.r_list[i + 1]
				  : NULL);
      else
	/* In trace mode there might be an invisible object (which we
	   could not find) after the previous one in the search list.
	   In this case it doesn't matter much where we put the
	   interpreter object, so we just initialize the list pointer so
	   that the assertion below holds.  */
	GL(dl_rtld_map).l_next = GL(dl_rtld_map).l_prev->l_next;

      assert (GL(dl_rtld_map).l_prev->l_next == GL(dl_rtld_map).l_next);
      GL(dl_rtld_map).l_prev->l_next = &GL(dl_rtld_map);
      if (GL(dl_rtld_map).l_next != NULL)
	{
	  assert (GL(dl_rtld_map).l_next->l_prev == GL(dl_rtld_map).l_prev);
	  GL(dl_rtld_map).l_next->l_prev = &GL(dl_rtld_map);
	}
    }

  /* Now let us see whether all libraries are available in the
     versions we need.  */
  {
    struct version_check_args args;
    args.doexit = mode == normal;
    args.dotrace = mode == trace;
    _dl_receive_error (print_missing_version, version_check_doit, &args);
  }

#ifdef USE_TLS
  /* Now it is time to determine the layout of the static TLS block
     and allocate it for the initial thread.  Note that we always
     allocate the static block, we never defer it even if no
     DF_STATIC_TLS bit is set.  The reason is that we know glibc will
     use the static model.  First add the dynamic linker to the list
     if it also uses TLS.  */
  if (GL(dl_rtld_map).l_tls_blocksize != 0)
    /* Assign a module ID.  */
    GL(dl_rtld_map).l_tls_modid = _dl_next_tls_modid ();

# ifndef TLS_INIT_TP_EXPENSIVE
#  define TLS_INIT_TP_EXPENSIVE 0
# endif

  /* We do not initialize any of the TLS functionality unless any of the
     initial modules uses TLS.  This makes dynamic loading of modules with
     TLS impossible, but to support it requires either eagerly doing setup
     now or lazily doing it later.  Doing it now makes us incompatible with
     an old kernel that can't perform TLS_INIT_TP, even if no TLS is ever
     used.  Trying to do it lazily is too hairy to try when there could be
     multiple threads (from a non-TLS-using libpthread).  */
  if (GL(dl_tls_max_dtv_idx) > 0 || !TLS_INIT_TP_EXPENSIVE)
    {
      struct link_map *l;
      size_t nelem;
      struct dtv_slotinfo *slotinfo;

      /* Number of elements in the static TLS block.  */
      GL(dl_tls_static_nelem) = GL(dl_tls_max_dtv_idx);

      /* Allocate the array which contains the information about the
	 dtv slots.  We allocate a few entries more than needed to
	 avoid the need for reallocation.  */
      nelem = GL(dl_tls_max_dtv_idx) + 1 + TLS_SLOTINFO_SURPLUS;

      /* Allocate.  */
      GL(dl_tls_dtv_slotinfo_list) = (struct dtv_slotinfo_list *)
	malloc (sizeof (struct dtv_slotinfo_list)
		+ nelem * sizeof (struct dtv_slotinfo));
      /* No need to check the return value.  If memory allocation failed
	 the program would have been terminated.  */

      slotinfo = memset (GL(dl_tls_dtv_slotinfo_list)->slotinfo, '\0',
			 nelem * sizeof (struct dtv_slotinfo));
      GL(dl_tls_dtv_slotinfo_list)->len = nelem;
      GL(dl_tls_dtv_slotinfo_list)->next = NULL;

      /* Fill in the information from the loaded modules.  */
      for (l = GL(dl_loaded), i = 0; l != NULL; l = l->l_next)
	if (l->l_tls_blocksize != 0)
	  /* This is a module with TLS data.  Store the map reference.
	     The generation counter is zero.  */
	  slotinfo[++i].map = l;
      assert (i == GL(dl_tls_max_dtv_idx));

      /* Compute the TLS offsets for the various blocks.  */
      _dl_determine_tlsoffset ();

      /* Construct the static TLS block and the dtv for the initial
	 thread.  For some platforms this will include allocating memory
	 for the thread descriptor.  The memory for the TLS block will
	 never be freed.  It should be allocated accordingly.  The dtv
	 array can be changed if dynamic loading requires it.  */
      tcbp = _dl_allocate_tls_storage ();
      if (tcbp == NULL)
	_dl_fatal_printf ("\
cannot allocate TLS data structures for initial thread");

      /* Store for detection of the special case by __tls_get_addr
	 so it knows not to pass this dtv to the normal realloc.  */
      GL(dl_initial_dtv) = GET_DTV (tcbp);
    }
#endif

  if (__builtin_expect (mode, normal) != normal)
    {
      /* We were run just to list the shared libraries.  It is
	 important that we do this before real relocation, because the
	 functions we call below for output may no longer work properly
	 after relocation.  */
      if (! GL(dl_loaded)->l_info[DT_NEEDED])
	_dl_printf ("\tstatically linked\n");
      else
	{
	  struct link_map *l;

	  if (GL(dl_debug_mask) & DL_DEBUG_PRELINK)
	    {
	      struct r_scope_elem *scope = &GL(dl_loaded)->l_searchlist;

	      for (i = 0; i < scope->r_nlist; i++)
		{
		  l = scope->r_list [i];
		  if (l->l_faked)
		    {
		      _dl_printf ("\t%s => not found\n", l->l_libname->name);
		      continue;
		    }
		  if (_dl_name_match_p (GL(dl_trace_prelink), l))
		    GL(dl_trace_prelink_map) = l;
		  _dl_printf ("\t%s => %s (0x%0*Zx, 0x%0*Zx)",
			      l->l_libname->name[0] ? l->l_libname->name
			      : rtld_progname ?: "<main program>",
			      l->l_name[0] ? l->l_name
			      : rtld_progname ?: "<main program>",
			      (int) sizeof l->l_map_start * 2,
			      l->l_map_start,
			      (int) sizeof l->l_addr * 2,
			      l->l_addr);
#ifdef USE_TLS
		  if (l->l_tls_modid)
		    _dl_printf (" TLS(0x%Zx, 0x%0*Zx)\n", l->l_tls_modid,
				(int) sizeof l->l_tls_offset * 2,
				l->l_tls_offset);
		  else
#endif
		    _dl_printf ("\n");
		}
	    }
	  else
	    {
	      for (l = GL(dl_loaded)->l_next; l; l = l->l_next)
		if (l->l_faked)
		  /* The library was not found.  */
		  _dl_printf ("\t%s => not found\n", l->l_libname->name);
		else
		  _dl_printf ("\t%s => %s (0x%0*Zx)\n", l->l_libname->name,
			      l->l_name, (int) sizeof l->l_map_start * 2,
			      l->l_map_start);
	    }
	}

      if (__builtin_expect (mode, trace) != trace)
	for (i = 1; i < (unsigned int) _dl_argc; ++i)
	  {
	    const ElfW(Sym) *ref = NULL;
	    ElfW(Addr) loadbase;
	    lookup_t result;

	    result = INTUSE(_dl_lookup_symbol) (INTUSE(_dl_argv)[i],
						GL(dl_loaded),
						&ref, GL(dl_loaded)->l_scope,
						ELF_RTYPE_CLASS_PLT, 1);

	    loadbase = LOOKUP_VALUE_ADDRESS (result);

	    _dl_printf ("%s found at 0x%0*Zd in object at 0x%0*Zd\n",
			INTUSE(_dl_argv)[i],
			(int) sizeof ref->st_value * 2, ref->st_value,
			(int) sizeof loadbase * 2, loadbase);
	  }
      else
	{
	  /* If LD_WARN is set warn about undefined symbols.  */
	  if (GL(dl_lazy) >= 0 && GL(dl_verbose))
	    {
	      /* We have to do symbol dependency testing.  */
	      struct relocate_args args;
	      struct link_map *l;

	      args.lazy = GL(dl_lazy);

	      l = GL(dl_loaded);
	      while (l->l_next)
		l = l->l_next;
	      do
		{
		  if (l != &GL(dl_rtld_map) && ! l->l_faked)
		    {
		      args.l = l;
		      _dl_receive_error (print_unresolved, relocate_doit,
					 &args);
		    }
		  l = l->l_prev;
		} while (l);

	      if ((GL(dl_debug_mask) & DL_DEBUG_PRELINK)
		  && GL(dl_rtld_map).l_opencount > 1)
		INTUSE(_dl_relocate_object) (&GL(dl_rtld_map),
					     GL(dl_loaded)->l_scope, 0, 0);
	    }

#define VERNEEDTAG (DT_NUM + DT_THISPROCNUM + DT_VERSIONTAGIDX (DT_VERNEED))
	  if (version_info)
	    {
	      /* Print more information.  This means here, print information
		 about the versions needed.  */
	      int first = 1;
	      struct link_map *map = GL(dl_loaded);

	      for (map = GL(dl_loaded); map != NULL; map = map->l_next)
		{
		  const char *strtab;
		  ElfW(Dyn) *dyn = map->l_info[VERNEEDTAG];
		  ElfW(Verneed) *ent;

		  if (dyn == NULL)
		    continue;

		  strtab = (const void *) D_PTR (map, l_info[DT_STRTAB]);
		  ent = (ElfW(Verneed) *) (map->l_addr + dyn->d_un.d_ptr);

		  if (first)
		    {
		      _dl_printf ("\n\tVersion information:\n");
		      first = 0;
		    }

		  _dl_printf ("\t%s:\n",
			      map->l_name[0] ? map->l_name : rtld_progname);

		  while (1)
		    {
		      ElfW(Vernaux) *aux;
		      struct link_map *needed;

		      needed = find_needed (strtab + ent->vn_file);
		      aux = (ElfW(Vernaux) *) ((char *) ent + ent->vn_aux);

		      while (1)
			{
			  const char *fname = NULL;

			  if (needed != NULL
			      && match_version (strtab + aux->vna_name,
						needed))
			    fname = needed->l_name;

			  _dl_printf ("\t\t%s (%s) %s=> %s\n",
				      strtab + ent->vn_file,
				      strtab + aux->vna_name,
				      aux->vna_flags & VER_FLG_WEAK
				      ? "[WEAK] " : "",
				      fname ?: "not found");

			  if (aux->vna_next == 0)
			    /* No more symbols.  */
			    break;

			  /* Next symbol.  */
			  aux = (ElfW(Vernaux) *) ((char *) aux
						   + aux->vna_next);
			}

		      if (ent->vn_next == 0)
			/* No more dependencies.  */
			break;

		      /* Next dependency.  */
		      ent = (ElfW(Verneed) *) ((char *) ent + ent->vn_next);
		    }
		}
	    }
	}

      _exit (0);
    }

  if (GL(dl_loaded)->l_info [ADDRIDX (DT_GNU_LIBLIST)]
      && ! __builtin_expect (GL(dl_profile) != NULL, 0))
    {
      ElfW(Lib) *liblist, *liblistend;
      struct link_map **r_list, **r_listend, *l;
      const char *strtab = (const void *) D_PTR (GL(dl_loaded),
						 l_info[DT_STRTAB]);

      assert (GL(dl_loaded)->l_info [VALIDX (DT_GNU_LIBLISTSZ)] != NULL);
      liblist = (ElfW(Lib) *)
		GL(dl_loaded)->l_info [ADDRIDX (DT_GNU_LIBLIST)]->d_un.d_ptr;
      liblistend = (ElfW(Lib) *)
		   ((char *) liblist
		    + GL(dl_loaded)->l_info [VALIDX (DT_GNU_LIBLISTSZ)]->d_un.d_val);
      r_list = GL(dl_loaded)->l_searchlist.r_list;
      r_listend = r_list + GL(dl_loaded)->l_searchlist.r_nlist;

      for (; r_list < r_listend && liblist < liblistend; r_list++)
	{
	  l = *r_list;

	  if (l == GL(dl_loaded))
	    continue;

	  /* If the library is not mapped where it should, fail.  */
	  if (l->l_addr)
	    break;

	  /* Next, check if checksum matches.  */
	  if (l->l_info [VALIDX(DT_CHECKSUM)] == NULL
	      || l->l_info [VALIDX(DT_CHECKSUM)]->d_un.d_val
		 != liblist->l_checksum)
	    break;

	  if (l->l_info [VALIDX(DT_GNU_PRELINKED)] == NULL
	      || l->l_info [VALIDX(DT_GNU_PRELINKED)]->d_un.d_val
		 != liblist->l_time_stamp)
	    break;

	  if (! _dl_name_match_p (strtab + liblist->l_name, l))
	    break;

	  ++liblist;
	}


      if (r_list == r_listend && liblist == liblistend)
	prelinked = true;

      if (__builtin_expect (GL(dl_debug_mask) & DL_DEBUG_LIBS, 0))
	_dl_printf ("\nprelink checking: %s\n", prelinked ? "ok" : "failed");
    }

  if (prelinked)
    {
      struct link_map *l;

      if (GL(dl_loaded)->l_info [ADDRIDX (DT_GNU_CONFLICT)] != NULL)
	{
	  ElfW(Rela) *conflict, *conflictend;
#ifndef HP_TIMING_NONAVAIL
	  hp_timing_t start;
	  hp_timing_t stop;
#endif

	  HP_TIMING_NOW (start);
	  assert (GL(dl_loaded)->l_info [VALIDX (DT_GNU_CONFLICTSZ)] != NULL);
	  conflict = (ElfW(Rela) *)
	    GL(dl_loaded)->l_info [ADDRIDX (DT_GNU_CONFLICT)]->d_un.d_ptr;
	  conflictend = (ElfW(Rela) *)
	    ((char *) conflict
	     + GL(dl_loaded)->l_info [VALIDX (DT_GNU_CONFLICTSZ)]->d_un.d_val);
	  _dl_resolve_conflicts (GL(dl_loaded), conflict, conflictend);
	  HP_TIMING_NOW (stop);
	  HP_TIMING_DIFF (relocate_time, start, stop);
	}


      /* Mark all the objects so we know they have been already relocated.  */
      for (l = GL(dl_loaded); l != NULL; l = l->l_next)
	l->l_relocated = 1;

      _dl_sysdep_start_cleanup ();
    }
  else
    {
      /* Now we have all the objects loaded.  Relocate them all except for
	 the dynamic linker itself.  We do this in reverse order so that copy
	 relocs of earlier objects overwrite the data written by later
	 objects.  We do not re-relocate the dynamic linker itself in this
	 loop because that could result in the GOT entries for functions we
	 call being changed, and that would break us.  It is safe to relocate
	 the dynamic linker out of order because it has no copy relocs (we
	 know that because it is self-contained).  */

      struct link_map *l;
      int consider_profiling = GL(dl_profile) != NULL;
#ifndef HP_TIMING_NONAVAIL
      hp_timing_t start;
      hp_timing_t stop;
      hp_timing_t add;
#endif

      /* If we are profiling we also must do lazy reloaction.  */
      GL(dl_lazy) |= consider_profiling;

      l = GL(dl_loaded);
      while (l->l_next)
	l = l->l_next;

      HP_TIMING_NOW (start);
      do
	{
	  /* While we are at it, help the memory handling a bit.  We have to
	     mark some data structures as allocated with the fake malloc()
	     implementation in ld.so.  */
	  struct libname_list *lnp = l->l_libname->next;

	  while (__builtin_expect (lnp != NULL, 0))
	    {
	      lnp->dont_free = 1;
	      lnp = lnp->next;
	    }

	  if (l != &GL(dl_rtld_map))
	    INTUSE(_dl_relocate_object) (l, l->l_scope, GL(dl_lazy),
					 consider_profiling);

	  l = l->l_prev;
	}
      while (l);
      HP_TIMING_NOW (stop);

      HP_TIMING_DIFF (relocate_time, start, stop);

      /* Do any necessary cleanups for the startup OS interface code.
	 We do these now so that no calls are made after rtld re-relocation
	 which might be resolved to different functions than we expect.
	 We cannot do this before relocating the other objects because
	 _dl_relocate_object might need to call `mprotect' for DT_TEXTREL.  */
      _dl_sysdep_start_cleanup ();

      /* Now enable profiling if needed.  Like the previous call,
	 this has to go here because the calls it makes should use the
	 rtld versions of the functions (particularly calloc()), but it
	 needs to have _dl_profile_map set up by the relocator.  */
      if (__builtin_expect (GL(dl_profile_map) != NULL, 0))
	/* We must prepare the profiling.  */
	INTUSE(_dl_start_profile) (GL(dl_profile_map), GL(dl_profile_output));

      if (GL(dl_rtld_map).l_opencount > 1)
	{
	  /* There was an explicit ref to the dynamic linker as a shared lib.
	     Re-relocate ourselves with user-controlled symbol definitions.  */
	  HP_TIMING_NOW (start);
	  INTUSE(_dl_relocate_object) (&GL(dl_rtld_map), GL(dl_loaded)->l_scope,
				       0, 0);
	  HP_TIMING_NOW (stop);
	  HP_TIMING_DIFF (add, start, stop);
	  HP_TIMING_ACCUM_NT (relocate_time, add);
	}
    }

  /* Now set up the variable which helps the assembler startup code.  */
  GL(dl_main_searchlist) = &GL(dl_loaded)->l_searchlist;
  GL(dl_global_scope)[0] = &GL(dl_loaded)->l_searchlist;

  /* Save the information about the original global scope list since
     we need it in the memory handling later.  */
  GL(dl_initial_searchlist) = *GL(dl_main_searchlist);

#ifndef NONTLS_INIT_TP
# define NONTLS_INIT_TP do { } while (0)
#endif

#ifdef USE_TLS
  if (GL(dl_tls_max_dtv_idx) > 0 || USE___THREAD || !TLS_INIT_TP_EXPENSIVE)
    {
      /* Now that we have completed relocation, the initializer data
	 for the TLS blocks has its final values and we can copy them
	 into the main thread's TLS area, which we allocated above.  */
      _dl_allocate_tls_init (tcbp);

      /* And finally install it for the main thread.  If ld.so itself uses
	 TLS we know the thread pointer was initialized earlier.  */
      const char *lossage = TLS_INIT_TP (tcbp, USE___THREAD);
      if (__builtin_expect (lossage != NULL, 0))
	_dl_fatal_printf ("cannot set up thread-local storage: %s\n", lossage);
    }
  else
#endif
    NONTLS_INIT_TP;

  {
    /* Initialize _r_debug.  */
    struct r_debug *r = _dl_debug_initialize (GL(dl_rtld_map).l_addr);
    struct link_map *l;

    l = GL(dl_loaded);

#ifdef ELF_MACHINE_DEBUG_SETUP

    /* Some machines (e.g. MIPS) don't use DT_DEBUG in this way.  */

    ELF_MACHINE_DEBUG_SETUP (l, r);
    ELF_MACHINE_DEBUG_SETUP (&GL(dl_rtld_map), r);

#else

    if (l->l_info[DT_DEBUG] != NULL)
      /* There is a DT_DEBUG entry in the dynamic section.  Fill it in
	 with the run-time address of the r_debug structure  */
      l->l_info[DT_DEBUG]->d_un.d_ptr = (ElfW(Addr)) r;

    /* Fill in the pointer in the dynamic linker's own dynamic section, in
       case you run gdb on the dynamic linker directly.  */
    if (GL(dl_rtld_map).l_info[DT_DEBUG] != NULL)
      GL(dl_rtld_map).l_info[DT_DEBUG]->d_un.d_ptr = (ElfW(Addr)) r;

#endif

    /* Notify the debugger that all objects are now mapped in.  */
    r->r_state = RT_ADD;
    INTUSE(_dl_debug_state) ();
  }

#ifndef MAP_COPY
  /* We must munmap() the cache file.  */
  INTUSE(_dl_unload_cache) ();
#endif

  /* Once we return, _dl_sysdep_start will invoke
     the DT_INIT functions and then *USER_ENTRY.  */
}

/* This is a little helper function for resolving symbols while
   tracing the binary.  */
static void
print_unresolved (int errcode __attribute__ ((unused)), const char *objname,
		  const char *errstring)
{
  if (objname[0] == '\0')
    objname = rtld_progname ?: "<main program>";
  _dl_error_printf ("%s	(%s)\n", errstring, objname);
}

/* This is a little helper function for resolving symbols while
   tracing the binary.  */
static void
print_missing_version (int errcode __attribute__ ((unused)),
		       const char *objname, const char *errstring)
{
  _dl_error_printf ("%s: %s: %s\n", rtld_progname ?: "<program name unknown>",
		    objname, errstring);
}

/* Nonzero if any of the debugging options is enabled.  */
static int any_debug;

/* Process the string given as the parameter which explains which debugging
   options are enabled.  */
static void
process_dl_debug (const char *dl_debug)
{
  /* When adding new entries make sure that the maximal length of a name
     is correctly handled in the LD_DEBUG_HELP code below.  */
  static const struct
  {
    unsigned char len;
    const char name[10];
    const char helptext[41];
    unsigned short int mask;
  } debopts[] =
    {
#define LEN_AND_STR(str) sizeof (str) - 1, str
      { LEN_AND_STR ("libs"), "display library search paths",
	DL_DEBUG_LIBS | DL_DEBUG_IMPCALLS },
      { LEN_AND_STR ("reloc"), "display relocation processing",
	DL_DEBUG_RELOC | DL_DEBUG_IMPCALLS },
      { LEN_AND_STR ("files"), "display progress for input file",
	DL_DEBUG_FILES | DL_DEBUG_IMPCALLS },
      { LEN_AND_STR ("symbols"), "display symbol table processing",
	DL_DEBUG_SYMBOLS | DL_DEBUG_IMPCALLS },
      { LEN_AND_STR ("bindings"), "display information about symbol binding",
	DL_DEBUG_BINDINGS | DL_DEBUG_IMPCALLS },
      { LEN_AND_STR ("versions"), "display version dependencies",
	DL_DEBUG_VERSIONS | DL_DEBUG_IMPCALLS },
      { LEN_AND_STR ("all"), "all previous options combined",
	DL_DEBUG_LIBS | DL_DEBUG_RELOC | DL_DEBUG_FILES | DL_DEBUG_SYMBOLS
	| DL_DEBUG_BINDINGS | DL_DEBUG_VERSIONS | DL_DEBUG_IMPCALLS },
      { LEN_AND_STR ("statistics"), "display relocation statistics",
	DL_DEBUG_STATISTICS },
      { LEN_AND_STR ("help"), "display this help message and exit",
	DL_DEBUG_HELP },
    };
#define ndebopts (sizeof (debopts) / sizeof (debopts[0]))

  /* Skip separating white spaces and commas.  */
  while (*dl_debug != '\0')
    {
      if (*dl_debug != ' ' && *dl_debug != ',' && *dl_debug != ':')
	{
	  size_t cnt;
	  size_t len = 1;

	  while (dl_debug[len] != '\0' && dl_debug[len] != ' '
		 && dl_debug[len] != ',' && dl_debug[len] != ':')
	    ++len;

	  for (cnt = 0; cnt < ndebopts; ++cnt)
	    if (debopts[cnt].len == len
		&& memcmp (dl_debug, debopts[cnt].name, len) == 0)
	      {
		GL(dl_debug_mask) |= debopts[cnt].mask;
		any_debug = 1;
		break;
	      }

	  if (cnt == ndebopts)
	    {
	      /* Display a warning and skip everything until next
		 separator.  */
	      char *copy = strndupa (dl_debug, len);
	      _dl_error_printf ("\
warning: debug option `%s' unknown; try LD_DEBUG=help\n", copy);
	    }

	  dl_debug += len;
	  continue;
	}

      ++dl_debug;
    }

  if (GL(dl_debug_mask) & DL_DEBUG_HELP)
    {
      size_t cnt;

      _dl_printf ("\
Valid options for the LD_DEBUG environment variable are:\n\n");

      for (cnt = 0; cnt < ndebopts; ++cnt)
	_dl_printf ("  %.*s%s%s\n", debopts[cnt].len, debopts[cnt].name,
		    "         " + debopts[cnt].len - 3,
		    debopts[cnt].helptext);

      _dl_printf ("\n\
To direct the debugging output into a file instead of standard output\n\
a filename can be specified using the LD_DEBUG_OUTPUT environment variable.\n");
      _exit (0);
    }
}

/* Process all environments variables the dynamic linker must recognize.
   Since all of them start with `LD_' we are a bit smarter while finding
   all the entries.  */
extern char **_environ attribute_hidden;


static void
process_envvars (enum mode *modep)
{
  char **runp = _environ;
  char *envline;
  enum mode mode = normal;
  char *debug_output = NULL;

  /* This is the default place for profiling data file.  */
  GL(dl_profile_output)
    = &"/var/tmp\0/var/profile"[INTUSE(__libc_enable_secure) ? 9 : 0];

  while ((envline = _dl_next_ld_env_entry (&runp)) != NULL)
    {
      size_t len = 0;

      while (envline[len] != '\0' && envline[len] != '=')
	++len;

      if (envline[len] != '=')
	/* This is a "LD_" variable at the end of the string without
	   a '=' character.  Ignore it since otherwise we will access
	   invalid memory below.  */
	continue;

      switch (len)
	{
	case 4:
	  /* Warning level, verbose or not.  */
	  if (memcmp (envline, "WARN", 4) == 0)
	    GL(dl_verbose) = envline[5] != '\0';
	  break;

	case 5:
	  /* Debugging of the dynamic linker?  */
	  if (memcmp (envline, "DEBUG", 5) == 0)
	    process_dl_debug (&envline[6]);
	  break;

	case 7:
	  /* Print information about versions.  */
	  if (memcmp (envline, "VERBOSE", 7) == 0)
	    {
	      version_info = envline[8] != '\0';
	      break;
	    }

	  /* List of objects to be preloaded.  */
	  if (memcmp (envline, "PRELOAD", 7) == 0)
	    {
	      preloadlist = &envline[8];
	      break;
	    }

	  /* Which shared object shall be profiled.  */
	  if (memcmp (envline, "PROFILE", 7) == 0 && envline[8] != '\0')
	    GL(dl_profile) = &envline[8];
	  break;

	case 8:
	  /* Do we bind early?  */
	  if (memcmp (envline, "BIND_NOW", 8) == 0)
	    {
	      GL(dl_lazy) = envline[9] == '\0';
	      break;
	    }
	  if (memcmp (envline, "BIND_NOT", 8) == 0)
	    GL(dl_bind_not) = envline[9] != '\0';
	  break;

	case 9:
	  /* Test whether we want to see the content of the auxiliary
	     array passed up from the kernel.  */
	  if (memcmp (envline, "SHOW_AUXV", 9) == 0)
	    _dl_show_auxv ();
	  break;

	case 10:
	  /* Mask for the important hardware capabilities.  */
	  if (memcmp (envline, "HWCAP_MASK", 10) == 0)
	    GL(dl_hwcap_mask) = __strtoul_internal (&envline[11], NULL, 0, 0);
	  break;

	case 11:
	  /* Path where the binary is found.  */
	  if (!INTUSE(__libc_enable_secure)
	      && memcmp (envline, "ORIGIN_PATH", 11) == 0)
	    GL(dl_origin_path) = &envline[12];
	  break;

	case 12:
	  /* The library search path.  */
	  if (memcmp (envline, "LIBRARY_PATH", 12) == 0)
	    {
	      library_path = &envline[13];
	      break;
	    }

	  /* Where to place the profiling data file.  */
	  if (memcmp (envline, "DEBUG_OUTPUT", 12) == 0)
	    {
	      debug_output = &envline[13];
	      break;
	    }

	  if (memcmp (envline, "DYNAMIC_WEAK", 12) == 0)
	    GL(dl_dynamic_weak) = 1;
	  break;

	case 14:
	  /* Where to place the profiling data file.  */
	  if (!INTUSE(__libc_enable_secure)
	      && memcmp (envline, "PROFILE_OUTPUT", 14) == 0
	      && envline[15] != '\0')
	    GL(dl_profile_output) = &envline[15];
	  break;

	case 16:
	  /* The mode of the dynamic linker can be set.  */
	  if (memcmp (envline, "TRACE_PRELINKING", 16) == 0)
	    {
	      mode = trace;
	      GL(dl_verbose) = 1;
	      GL(dl_debug_mask) |= DL_DEBUG_PRELINK;
	      GL(dl_trace_prelink) = &envline[17];
	    }
	  break;

	case 20:
	  /* The mode of the dynamic linker can be set.  */
	  if (memcmp (envline, "TRACE_LOADED_OBJECTS", 20) == 0)
	    mode = trace;
	  break;

	  /* We might have some extra environment variable to handle.  This
	     is tricky due to the pre-processing of the length of the name
	     in the switch statement here.  The code here assumes that added
	     environment variables have a different length.  */
#ifdef EXTRA_LD_ENVVARS
	  EXTRA_LD_ENVVARS
#endif
	}
    }

  /* The caller wants this information.  */
  *modep = mode;

  /* Extra security for SUID binaries.  Remove all dangerous environment
     variables.  */
  if (__builtin_expect (INTUSE(__libc_enable_secure), 0))
    {
      static const char unsecure_envvars[] =
#ifdef EXTRA_UNSECURE_ENVVARS
	EXTRA_UNSECURE_ENVVARS
#endif
	UNSECURE_ENVVARS;
      const char *nextp;

      nextp = unsecure_envvars;
      do
	{
	  unsetenv (nextp);
	  /* We could use rawmemchr but this need not be fast.  */
	  nextp = (char *) (strchr) (nextp, '\0') + 1;
	}
      while (*nextp != '\0');

      if (__access ("/etc/suid-debug", F_OK) != 0)
	unsetenv ("MALLOC_CHECK_");
    }
  /* If we have to run the dynamic linker in debugging mode and the
     LD_DEBUG_OUTPUT environment variable is given, we write the debug
     messages to this file.  */
  else if (any_debug && debug_output != NULL)
    {
#ifdef O_NOFOLLOW
      const int flags = O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW;
#else
      const int flags = O_WRONLY | O_APPEND | O_CREAT;
#endif
      size_t name_len = strlen (debug_output);
      char buf[name_len + 12];
      char *startp;

      buf[name_len + 11] = '\0';
      startp = _itoa (__getpid (), &buf[name_len + 11], 10, 0);
      *--startp = '.';
      startp = memcpy (startp - name_len, debug_output, name_len);

      GL(dl_debug_fd) = __open (startp, flags, DEFFILEMODE);
      if (GL(dl_debug_fd) == -1)
	/* We use standard output if opening the file failed.  */
	GL(dl_debug_fd) = STDOUT_FILENO;
    }
}


/* Print the various times we collected.  */
static void
print_statistics (void)
{
#ifndef HP_TIMING_NONAVAIL
  char buf[200];
  char *cp;
  char *wp;

  /* Total time rtld used.  */
  if (HP_TIMING_AVAIL)
    {
      HP_TIMING_PRINT (buf, sizeof (buf), rtld_total_time);
      INTUSE(_dl_debug_printf) ("\nruntime linker statistics:\n"
				"  total startup time in dynamic loader: %s\n",
				buf);
    }

  /* Print relocation statistics.  */
  if (HP_TIMING_AVAIL)
    {
      char pbuf[30];
      HP_TIMING_PRINT (buf, sizeof (buf), relocate_time);
      cp = _itoa ((1000ULL * relocate_time) / rtld_total_time,
		  pbuf + sizeof (pbuf), 10, 0);
      wp = pbuf;
      switch (pbuf + sizeof (pbuf) - cp)
	{
	case 3:
	  *wp++ = *cp++;
	case 2:
	  *wp++ = *cp++;
	case 1:
	  *wp++ = '.';
	  *wp++ = *cp++;
	}
      *wp = '\0';
      INTUSE(_dl_debug_printf) ("\
            time needed for relocation: %s (%s%%)\n",
				buf, pbuf);
    }
#endif
  INTUSE(_dl_debug_printf) ("                 number of relocations: %lu\n",
			    GL(dl_num_relocations));
  INTUSE(_dl_debug_printf) ("      number of relocations from cache: %lu\n",
			    GL(dl_num_cache_relocations));

#ifndef HP_TIMING_NONAVAIL
  /* Time spend while loading the object and the dependencies.  */
  if (HP_TIMING_AVAIL)
    {
      char pbuf[30];
      HP_TIMING_PRINT (buf, sizeof (buf), load_time);
      cp = _itoa ((1000ULL * load_time) / rtld_total_time,
		  pbuf + sizeof (pbuf), 10, 0);
      wp = pbuf;
      switch (pbuf + sizeof (pbuf) - cp)
	{
	case 3:
	  *wp++ = *cp++;
	case 2:
	  *wp++ = *cp++;
	case 1:
	  *wp++ = '.';
	  *wp++ = *cp++;
	}
      *wp = '\0';
      INTUSE(_dl_debug_printf) ("\
           time needed to load objects: %s (%s%%)\n",
				buf, pbuf);
    }
#endif
}
