/* Copyright (C) 1991, 92, 95, 96, 97, 98, 99 Free Software Foundation, Inc.
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

#include <alloca.h>
#include <argz.h>
#include <errno.h>
#include <bits/libc-lock.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "localeinfo.h"

/* For each category declare two external variables (with weak references):
     extern const struct locale_data *_nl_current_CATEGORY;
   This points to the current locale's in-core data for CATEGORY.
     extern const struct locale_data _nl_C_CATEGORY;
   This contains the built-in "C"/"POSIX" locale's data for CATEGORY.
   Both are weak references; if &_nl_current_CATEGORY is zero,
   then nothing is using the locale data.  */
#define DEFINE_CATEGORY(category, category_name, items, a, b, c, d) \
extern struct locale_data *_nl_current_##category;			      \
extern struct locale_data _nl_C_##category;
#include "categories.def"
#undef	DEFINE_CATEGORY

/* Array indexed by category of pointers to _nl_current_CATEGORY slots.  */
struct locale_data * *const _nl_current[] =
  {
#define DEFINE_CATEGORY(category, category_name, items, a, b, c, d) \
    [category] = &_nl_current_##category,
#include "categories.def"
#undef	DEFINE_CATEGORY
    /* We need this additional element to simplify the code.  It must
       simply be != NULL.  */
    [LC_ALL] = (struct locale_data **) ~0ul
  };

/* Array indexed by category of pointers to _nl_C_CATEGORY slots.
   Elements are zero for categories whose data is never used.  */
struct locale_data *const _nl_C[] =
  {
#define DEFINE_CATEGORY(category, category_name, items, a, b, c, d) \
    [category] = &_nl_C_##category,
#include "categories.def"
#undef	DEFINE_CATEGORY
  };


/* Define an array of category names (also the environment variable names),
   indexed by integral category.

   We have entries of fixed width (16 for now) do avoid an array of
   pointers.  Update the size of the outer array if new, longer locale
   names are introduced.  */
const char _nl_category_names[][16] =
  {
#define DEFINE_CATEGORY(category, category_name, items, a, b, c, d) \
    [category] = category_name,
#include "categories.def"
#undef	DEFINE_CATEGORY
    [LC_ALL] = "LC_ALL"
  };
/* An array of their lengths, for convenience.  */
const size_t _nl_category_name_sizes[] =
  {
#define DEFINE_CATEGORY(category, category_name, items, a, b, c, d) \
    [category] = sizeof (category_name) - 1,
#include "categories.def"
#undef	DEFINE_CATEGORY
    [LC_ALL] = sizeof ("LC_ALL") - 1
  };


/* Declare the postload functions used below.  */
#undef	NO_POSTLOAD
#define NO_POSTLOAD _nl_postload_ctype /* Harmless thing known to exist.  */
#define DEFINE_CATEGORY(category, category_name, items, postload, b, c, d) \
extern void postload (void);
#include "categories.def"
#undef	DEFINE_CATEGORY
#undef	NO_POSTLOAD

/* Define an array indexed by category of postload functions to call after
   loading and installing that category's data.  */
static void (*const _nl_category_postload[]) (void) =
  {
#define DEFINE_CATEGORY(category, category_name, items, postload, b, c, d) \
    [category] = postload,
#include "categories.def"
#undef	DEFINE_CATEGORY
  };


/* Name of current locale for each individual category.
   Each is malloc'd unless it is nl_C_name.  */
static const char *_nl_current_names[] =
  {
#define DEFINE_CATEGORY(category, category_name, items, a, b, c, d) \
    [category] = _nl_C_name,
#include "categories.def"
#undef	DEFINE_CATEGORY
    [LC_ALL] = _nl_C_name		/* For LC_ALL.  */
  };


/* Lock for protecting global data.  */
__libc_lock_define_initialized (, __libc_setlocale_lock)


/* Use this when we come along an error.  */
#define ERROR_RETURN							      \
  do {									      \
    __set_errno (EINVAL);						      \
    return NULL;							      \
  } while (0)


/* Construct a new composite name.  */
static inline char *
new_composite_name (int category, const char *newnames[LC_ALL])
{
  size_t last_len = 0;
  size_t cumlen = 0;
  int i;
  char *new, *p;
  int same = 1;

  for (i = 0; i < LC_ALL; ++i)
    {
      const char *name = (category == LC_ALL ? newnames[i] :
			  category == i ? newnames[0] :
			  _nl_current_names[i]);
      last_len = strlen (name);
      cumlen += _nl_category_name_sizes[i] + 1 + last_len + 1;
      if (same && strcmp (name, newnames[0]) != 0)
	same = 0;
    }

  if (same)
    {
      /* All the categories use the same name.  */
      if (strcmp (newnames[0], _nl_C_name) == 0
	  || strcmp (newnames[0], _nl_POSIX_name) == 0)
	return (char *) _nl_C_name;

      new = malloc (last_len + 1);

      return new == NULL ? NULL : memcpy (new, newnames[0], last_len + 1);
    }

  new = malloc (cumlen);
  if (new == NULL)
    return NULL;
  p = new;
  for (i = 0; i < LC_ALL; ++i)
    {
      /* Add "CATEGORY=NAME;" to the string.  */
      const char *name = (category == LC_ALL ? newnames[i] :
			  category == i ? newnames[0] :
			  _nl_current_names[i]);
      p = __stpcpy (p, _nl_category_names[i]);
      *p++ = '=';
      p = __stpcpy (p, name);
      *p++ = ';';
    }
  p[-1] = '\0';		/* Clobber the last ';'.  */
  return new;
}


/* Put NAME in _nl_current_names.  */
static inline void
setname (int category, const char *name)
{
  if (_nl_current_names[category] == name)
    return;

  if (category == LC_ALL && _nl_current_names[category] != _nl_C_name)
    free ((char *) _nl_current_names[category]);

  _nl_current_names[category] = name;
}


/* Put DATA in *_nl_current[CATEGORY].  */
static inline void
setdata (int category, struct locale_data *data)
{
  if (_nl_current[category] != NULL)
    {
      *_nl_current[category] = data;
      if (_nl_category_postload[category])
	(*_nl_category_postload[category]) ();
    }
}


char *
setlocale (int category, const char *locale)
{
  char *locale_path;
  size_t locale_path_len;
  const char *locpath_var;
  char *composite;

  /* Sanity check for CATEGORY argument.  */
  if (category < 0 || category > LC_ALL)
    ERROR_RETURN;

  /* Does user want name of current locale?  */
  if (locale == NULL)
    return (char *) _nl_current_names[category];

  if (strcmp (locale, _nl_current_names[category]) == 0)
    /* Changing to the same thing.  */
    return (char *) _nl_current_names[category];

  /* We perhaps really have to load some data.  So we determine the
     path in which to look for the data now.  The environment variable
     `LOCPATH' must only be used when the binary has no SUID or SGID
     bit set.  */
  locale_path = NULL;
  locale_path_len = 0;

  locpath_var = __secure_getenv ("LOCPATH");
  if (locpath_var != NULL && locpath_var[0] != '\0')
    if (__argz_create_sep (locpath_var, ':',
			   &locale_path, &locale_path_len) != 0)
      return NULL;

  if (__argz_add_sep (&locale_path, &locale_path_len, LOCALE_PATH, ':') != 0)
    return NULL;

  if (category == LC_ALL)
    {
      /* The user wants to set all categories.  The desired locales
	 for the individual categories can be selected by using a
	 composite locale name.  This is a semi-colon separated list
	 of entries of the form `CATEGORY=VALUE'.  */
      const char *newnames[LC_ALL];
      struct locale_data *newdata[LC_ALL];

      /* Set all name pointers to the argument name.  */
      for (category = 0; category < LC_ALL; ++category)
	newnames[category] = (char *) locale;

      if (strchr (locale, ';') != NULL)
	{
	  /* This is a composite name.  Make a copy and split it up.  */
	  char *np = strdupa (locale);
	  char *cp;
	  int cnt;

	  while ((cp = strchr (np, '=')) != NULL)
	    {
	      for (cnt = 0; cnt < LC_ALL; ++cnt)
		if ((size_t) (cp - np) == _nl_category_name_sizes[cnt]
		    && memcmp (np, _nl_category_names[cnt], cp - np) == 0)
		  break;

	      if (cnt == LC_ALL)
		/* Bogus category name.  */
		ERROR_RETURN;

	      /* Found the category this clause sets.  */
	      newnames[cnt] = ++cp;
	      cp = strchr (cp, ';');
	      if (cp != NULL)
		{
		  /* Examine the next clause.  */
		  *cp = '\0';
		  np = cp + 1;
		}
	      else
		/* This was the last clause.  We are done.  */
		break;
	    }

	  for (cnt = 0; cnt < LC_ALL; ++cnt)
	    if (newnames[cnt] == locale)
	      /* The composite name did not specify all categories.  */
	      ERROR_RETURN;
	}

      /* Protect global data.  */
      __libc_lock_lock (__libc_setlocale_lock);

      /* Load the new data for each category.  */
      while (category-- > 0)
	{
	  newdata[category] = _nl_find_locale (locale_path, locale_path_len,
					       category,
					       &newnames[category]);

	  if (newdata[category] == NULL)
	    break;

	  /* We must not simply free a global locale since we have no
	     control over the usage.  So we mark it as un-deletable.  */
	  if (newdata[category]->usage_count != UNDELETABLE)
	    newdata[category]->usage_count = UNDELETABLE;
	}

      /* Create new composite name.  */
      if (category >= 0
	  || (composite = new_composite_name (LC_ALL, newnames)) == NULL)
	/* Loading this part of the locale failed.  Abort the
	   composite load.  */
	composite = NULL;
      else
	{
	  /* Now we have loaded all the new data.  Put it in place.  */
	  for (category = 0; category < LC_ALL; ++category)
	    {
	      setdata (category, newdata[category]);
	      setname (category, newnames[category]);
	    }
	  setname (LC_ALL, composite);
	}

      /* Critical section left.  */
      __libc_lock_unlock (__libc_setlocale_lock);

      /* Free the resources (the locale path variable.  */
      free (locale_path);

      return composite;
    }
  else
    {
      struct locale_data *newdata = NULL;
      const char *newname[1] = { locale };

      /* Protect global data.  */
      __libc_lock_lock (__libc_setlocale_lock);

      if (_nl_current[category] != NULL)
	{
	  /* Only actually load the data if anything will use it.  */
	  newdata = _nl_find_locale (locale_path, locale_path_len, category,
				     &newname[0]);
	  if (newdata == NULL)
	    goto abort_single;

	  /* We must not simply free a global locale since we have no
	     control over the usage.  So we mark it as un-deletable.

	     Note: do not remove the `if', it's necessary to copy with
	     the builtin locale data.  */
	  if (newdata->usage_count != UNDELETABLE)
	    newdata->usage_count = UNDELETABLE;
	}

      /* Create new composite name.  */
      composite = new_composite_name (category, newname);
      if (composite == NULL)
	{
	  /* Say that we don't have any data loaded.  */
	abort_single:
	  newname[0] = NULL;
	}
      else
	{
	  if (_nl_current[category] != NULL)
	    setdata (category, newdata);

	  setname (category, newname[0]);
	  setname (LC_ALL, composite);
	}

      /* Critical section left.  */
      __libc_lock_unlock (__libc_setlocale_lock);

      /* Free the resources (the locale path variable.  */
      free (locale_path);

      return (char *) newname[0];
    }
}


static void __attribute__ ((unused))
free_mem (void)
{
  int category;

  for (category = 0; category < LC_ALL; ++category)
    {
      struct locale_data *here = *_nl_current[category];

      /* If this category is already "C" don't do anything.  */
      if (here == _nl_C[category])
	continue;

      /* We have to be prepared that sometime later me still might
	 need the locale information.  */
      setdata (category, _nl_C[category]);
      setname (category, _nl_C_name);

      _nl_unload_locale (here);
    }

  setname (LC_ALL, _nl_C_name);
}
text_set_element (__libc_subfreeres, free_mem);
