/* Call the termination functions of loaded shared objects.
   Copyright (C) 1995,96,98,99,2000,2001,2002 Free Software Foundation, Inc.
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

#include <alloca.h>
#include <assert.h>
#include <string.h>
#include <ldsodefs.h>


/* Type of the constructor functions.  */
typedef void (*fini_t) (void);


void
internal_function
_dl_fini (void)
{
  /* Lots of fun ahead.  We have to call the destructors for all still
     loaded objects.  The problem is that the ELF specification now
     demands that dependencies between the modules are taken into account.
     I.e., the destructor for a module is called before the ones for any
     of its dependencies.

     To make things more complicated, we cannot simply use the reverse
     order of the constructors.  Since the user might have loaded objects
     using `dlopen' there are possibly several other modules with its
     dependencies to be taken into account.  Therefore we have to start
     determining the order of the modules once again from the beginning.  */
  unsigned int i;
  struct link_map *l;
  struct link_map **maps;

  /* XXX Could it be (in static binaries) that there is no object loaded?  */
  assert (GL(dl_nloaded) > 0);

  /* Now we can allocate an array to hold all the pointers and copy
     the pointers in.  */
  maps = (struct link_map **) alloca (GL(dl_nloaded)
				      * sizeof (struct link_map *));
  for (l = GL(dl_loaded), i = 0; l != NULL; l = l->l_next)
    {
      assert (i < GL(dl_nloaded));

      maps[i++] = l;

      /* Bump l_opencount of all objects so that they are not dlclose()ed
	 from underneath us.  */
      ++l->l_opencount;
    }
  assert (i == GL(dl_nloaded));

  /* Now we have to do the sorting.  */
  for (l = GL(dl_loaded)->l_next; l != NULL; l = l->l_next)
    {
      unsigned int j;
      unsigned int k;

      /* Find the place in the `maps' array.  */
      for (j = 1; maps[j] != l; ++j)
	;

      /* Find all object for which the current one is a dependency and
	 move the found object (if necessary) in front.  */
      for (k = j + 1; k < GL(dl_nloaded); ++k)
	{
	  struct link_map **runp;

	  runp = maps[k]->l_initfini;
	  if (runp != NULL)
	    {
	      while (*runp != NULL)
		if (*runp == l)
		  {
		    struct link_map *here = maps[k];

		    /* Move it now.  */
		    memmove (&maps[j] + 1,
			     &maps[j],
			     (k - j) * sizeof (struct link_map *));
		    maps[j++] = here;

		    break;
		  }
		else
		  ++runp;
	    }

	  if (__builtin_expect (maps[k]->l_reldeps != NULL, 0))
	    {
	      unsigned int m = maps[k]->l_reldepsact;
	      struct link_map **relmaps = maps[k]->l_reldeps;

	      while (m-- > 0)
		{
		  if (relmaps[m] == l)
		    {
		      struct link_map *here = maps[k];

		      /* Move it now.  */
		      memmove (&maps[j] + 1,
			       &maps[j],
			       (k - j) * sizeof (struct link_map *));
		      maps[j] = here;

		      break;
		    }

		}
	    }
	}
    }

  /* `maps' now contains the objects in the right order.  Now call the
     destructors.  We have to process this array from the front.  */
  for (i = 0; i < GL(dl_nloaded); ++i)
    {
      l = maps[i];

      if (l->l_init_called)
	{
	  /* Make sure nothing happens if we are called twice.  */
	  l->l_init_called = 0;

	  /* Don't call the destructors for objects we are not supposed to.  */
	  if (l->l_name[0] == '\0' && l->l_type == lt_executable)
	    continue;

	  /* Is there a destructor function?  */
	  if (l->l_info[DT_FINI_ARRAY] == NULL && l->l_info[DT_FINI] == NULL)
	    continue;

	  /* When debugging print a message first.  */
	  if (__builtin_expect (GL(dl_debug_mask) & DL_DEBUG_IMPCALLS, 0))
	    INTUSE(_dl_debug_printf) ("\ncalling fini: %s\n\n",
				      l->l_name[0]
				      ? l->l_name : rtld_progname);

	  /* First see whether an array is given.  */
	  if (l->l_info[DT_FINI_ARRAY] != NULL)
	    {
	      ElfW(Addr) *array =
		(ElfW(Addr) *) (l->l_addr
				+ l->l_info[DT_FINI_ARRAY]->d_un.d_ptr);
	      unsigned int i = (l->l_info[DT_FINI_ARRAYSZ]->d_un.d_val
				/ sizeof (ElfW(Addr)));
	      while (i-- > 0)
		((fini_t) array[i]) ();
	    }

	  /* Next try the old-style destructor.  */
	  if (l->l_info[DT_FINI] != NULL)
	    ((fini_t) DL_DT_FINI_ADDRESS (l, l->l_addr + l->l_info[DT_FINI]->d_un.d_ptr)) ();
	}

      /* Correct the previous increment.  */
      --l->l_opencount;
    }

  if (__builtin_expect (GL(dl_debug_mask) & DL_DEBUG_STATISTICS, 0))
    {
      INTUSE(_dl_debug_printf) ("\nruntime linker statistics:\n");
      INTUSE(_dl_debug_printf) ("\
           final number of relocations: %lu\n",
				GL(dl_num_relocations));
      INTUSE(_dl_debug_printf) ("\
final number of relocations from cache: %lu\n",
				GL(dl_num_cache_relocations));
    }
}
