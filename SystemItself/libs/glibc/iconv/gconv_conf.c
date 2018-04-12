/* Handle configuration data.
   Copyright (C) 1997, 1998, 1999 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1997.

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
#include <limits.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

#include <gconv_int.h>


/* This is the default path where we look for module lists.  */
static const char default_gconv_path[] = GCONV_PATH;

/* Name of the file containing the module information in the directories
   along the path.  */
static const char gconv_conf_filename[] = "gconv-modules";

/* Filename extension for the modules.  */
#ifndef MODULE_EXT
# define MODULE_EXT ".so"
#endif
static const char gconv_module_ext[] = MODULE_EXT;

/* We have a few builtin transformations.  */
static struct gconv_module builtin_modules[] =
{
#define BUILTIN_TRANSFORMATION(From, ConstPfx, ConstLen, To, Cost, Name, \
			       Fct, Init, End, MinF, MaxF, MinT, MaxT) \
  {									      \
    from_pattern: From,							      \
    from_constpfx: ConstPfx,						      \
    from_constpfx_len: ConstLen,					      \
    from_regex: NULL,							      \
    to_string: To,							      \
    cost_hi: Cost,							      \
    cost_lo: INT_MAX,							      \
    module_name: Name							      \
  },
#define BUILTIN_ALIAS(From, To)

#include "gconv_builtin.h"
};

#undef BUILTIN_TRANSFORMATION
#undef BUILTIN_ALIAS

static const char *builtin_aliases[] =
{
#define BUILTIN_TRANSFORMATION(From, ConstPfx, ConstLen, To, Cost, Name, \
			       Fct, Init, End, MinF, MaxF, MinT, MaxT)
#define BUILTIN_ALIAS(From, To) From " " To,

#include "gconv_builtin.h"
};

#ifdef USE_IN_LIBIO
# include <libio/libioP.h>
# define __getdelim(line, len, c, fp) _IO_getdelim (line, len, c, fp)
#endif


/* Test whether there is already a matching module known.  */
static int
internal_function
detect_conflict (const char *alias, size_t alias_len)
{
  struct gconv_module *node = __gconv_modules_db;

  while (node != NULL)
    {
      int cmpres = strncmp (alias, node->from_constpfx,
			    MIN (alias_len, node->from_constpfx_len));

      if (cmpres == 0)
	{
	  struct gconv_module *runp;

	  if (alias_len < node->from_constpfx_len)
	    /* Cannot possibly match.  */
	    return 0;

	  /* This means the prefix and the alias are identical.  If
	     there is now a simple extry or a regular expression
	     matching this name we have found a conflict.  If there is
	     no conflict with the elements in the `same' list there
	     cannot be a conflict.  */
	  runp = node;
	  do
	    {
	      if (runp->from_pattern == NULL)
		{
		  /* This is a simple entry and therefore we have a
		     conflict if the strings are really the same.  */
		  if (alias_len == node->from_constpfx_len)
		    return 1;
		}
	      else
		{
		  /* Compile the regular expression if necessary.  */
		  if (runp->from_regex == NULL)
		    {
		      if (__regcomp (&runp->from_regex_mem,
				     runp->from_pattern,
				     REG_EXTENDED | REG_ICASE) != 0)
			/* Something is wrong.  Remember this.  */
			runp->from_regex = (regex_t *) -1L;
		      else
			runp->from_regex = &runp->from_regex_mem;
		    }

		  if (runp->from_regex != (regex_t *) -1L)
		    {
		      regmatch_t match[1];

		      /* Try to match the regular expression.  */
		      if (__regexec (runp->from_regex, alias, 1, match, 0) == 0
			  && match[0].rm_so == 0
			  && alias[match[0].rm_eo] == '\0')
			/* They match, therefore it is a conflict.  */
			return 1;
		    }
		}

	      runp = runp->same;
	    }
	  while (runp != NULL);

	  if (alias_len == node->from_constpfx_len)
	      return 0;

	  node = node->matching;
	}
      else if (cmpres < 0)
	node = node->left;
      else
	node = node->right;
    }

  return node != NULL;
}


/* Add new alias.  */
static inline void
add_alias (char *rp, void *modules)
{
  /* We now expect two more string.  The strings are normalized
     (converted to UPPER case) and strored in the alias database.  */
  struct gconv_alias *new_alias;
  char *from, *to, *wp;

  while (isspace (*rp))
    ++rp;
  from = wp = rp;
  while (*rp != '\0' && !isspace (*rp))
    *wp++ = toupper (*rp++);
  if (*rp == '\0')
    /* There is no `to' string on the line.  Ignore it.  */
    return;
  *wp++ = '\0';
  to = ++rp;
  while (isspace (*rp))
    ++rp;
  while (*rp != '\0' && !isspace (*rp))
    *wp++ = toupper (*rp++);
  if (to == wp)
    /* No `to' string, ignore the line.  */
    return;
  *wp++ = '\0';

  /* Test whether this alias conflicts with any available module.  */
  if (detect_conflict (from, to - from - 1))
    /* It does conflict, don't add the alias.  */
    return;

  new_alias = (struct gconv_alias *)
    malloc (sizeof (struct gconv_alias) + (wp - from));
  if (new_alias != NULL)
    {
      void **inserted;

      new_alias->fromname = memcpy ((char *) new_alias
				    + sizeof (struct gconv_alias),
				    from, wp - from);
      new_alias->toname = new_alias->fromname + (to - from);

      inserted = (void **) __tsearch (new_alias, &__gconv_alias_db,
				      __gconv_alias_compare);
      if (inserted == NULL || *inserted != new_alias)
	/* Something went wrong, free this entry.  */
	free (new_alias);
    }
}


/* Insert a data structure for a new module in the search tree.  */
static inline void
internal_function
insert_module (struct gconv_module *newp)
{
  struct gconv_module **rootp = &__gconv_modules_db;

  while (*rootp != NULL)
    {
      struct gconv_module *root = *rootp;
      size_t minlen = MIN (newp->from_constpfx_len, root->from_constpfx_len);
      int cmpres;

      cmpres = strncmp (newp->from_constpfx, root->from_constpfx, minlen);
      if (cmpres == 0)
	{
	  /* This can mean two things: the prefix is entirely the same or
	     it matches only for the minimum length of both strings.  */
	  if (newp->from_constpfx_len == root->from_constpfx_len)
	    {
	      /* Both prefixes are identical.  Insert the string at the
		 end of the `same' list if it is not already there.  */
	      const char *from_pattern = (newp->from_pattern
					  ?: newp->from_constpfx);

	      while (strcmp (from_pattern,
			     root->from_pattern ?: root->from_constpfx) != 0
		     || strcmp (newp->to_string, root->to_string) != 0)
		{
		  rootp = &root->same;
		  root = *rootp;
		  if (root == NULL)
		    break;
		}

	      if (root != NULL)
		/* This is a no new conversion.  */
		return;

	      break;
	    }

	  /* The new element either has a prefix which is itself a
	     prefix for the prefix of the current node or vice verse.
	     In the first case we insert the node right here.  Otherwise
	     we have to descent further.  */
	  if (newp->from_constpfx_len < root->from_constpfx_len)
	    {
	      newp->matching = root;
	      break;
	    }

	  rootp = &root->matching;
	}
      else if (cmpres < 0)
	rootp = &root->left;
      else
	rootp = &root->right;
    }

  /* Plug in the new node here.  */
  *rootp = newp;
}


/* Add new module.  */
static inline void
internal_function
add_module (char *rp, const char *directory, size_t dir_len, void **modules,
	    size_t *nmodules, int modcounter)
{
  /* We expect now
     1. `from' name
     2. `to' name
     3. filename of the module
     4. an optional cost value
  */
  struct gconv_module *new_module;
  char *from, *to, *module, *wp;
  size_t const_len;
  int from_is_regex;
  int need_ext;
  int cost_hi;

  while (isspace (*rp))
    ++rp;
  from = rp;
  from_is_regex = 0;
  while (*rp != '\0' && !isspace (*rp))
    {
      if (!isalnum (*rp) && *rp != '-' && *rp != '/' && *rp != '.'
	  && *rp != '_' && *rp != '(' && *rp != ')')
	from_is_regex = 1;
      *rp = toupper (*rp);
      ++rp;
    }
  if (*rp == '\0')
    return;
  *rp++ = '\0';
  to = wp = rp;
  while (isspace (*rp))
    ++rp;
  while (*rp != '\0' && !isspace (*rp))
    *wp++ = toupper (*rp++);
  if (*rp == '\0')
    return;
  *wp++ = '\0';
  do
    ++rp;
  while (isspace (*rp));
  module = wp;
  while (*rp != '\0' && !isspace (*rp))
    *wp++ = *rp++;
  if (*rp == '\0')
    {
      /* There is no cost, use one by default.  */
      *wp++ = '\0';
      cost_hi = 1;
    }
  else
    {
      /* There might be a cost value.  */
      char *endp;

      *wp++ = '\0';
      cost_hi = strtol (rp, &endp, 10);
      if (rp == endp || cost_hi < 1)
	/* No useful information.  */
	cost_hi = 1;
    }

  if (module[0] == '\0')
    /* No module name given.  */
    return;
  if (module[0] == '/')
    dir_len = 0;
  else
    /* Increment by one for the slash.  */
    ++dir_len;

  /* See whether we must add the ending.  */
  need_ext = 0;
  if (wp - module < sizeof (gconv_module_ext)
      || memcmp (wp - sizeof (gconv_module_ext), gconv_module_ext,
		 sizeof (gconv_module_ext)) != 0)
    /* We must add the module extension.  */
    need_ext = sizeof (gconv_module_ext) - 1;

  /* We've collected all the information, now create an entry.  */

  if (from_is_regex)
    {
      const_len = 0;
      while (isalnum (from[const_len]) || from[const_len] == '-'
	     || from[const_len] == '/' || from[const_len] == '.'
	     || from[const_len] == '_')
	++const_len;
    }
  else
    const_len = to - from - 1;

  new_module = (struct gconv_module *) calloc (1,
					       sizeof (struct gconv_module)
					       + (wp - from)
					       + dir_len + need_ext);
  if (new_module != NULL)
    {
      char *tmp;

      new_module->from_constpfx = memcpy ((char *) new_module
					  + sizeof (struct gconv_module),
					  from, to - from);
      if (from_is_regex)
	new_module->from_pattern = new_module->from_constpfx;

      new_module->from_constpfx_len = const_len;

      new_module->to_string = memcpy ((char *) new_module->from_constpfx
				      + (to - from), to, module - to);

      new_module->cost_hi = cost_hi;
      new_module->cost_lo = modcounter;

      new_module->module_name = (char *) new_module->to_string + (module - to);

      if (dir_len == 0)
	tmp = (char *) new_module->module_name;
      else
	{
	  tmp = __mempcpy ((char *) new_module->module_name,
			   directory, dir_len - 1);
	  *tmp++ = '/';
	}

      tmp = __mempcpy (tmp, module, wp - module);

      if (need_ext)
	memcpy (tmp - 1, gconv_module_ext, sizeof (gconv_module_ext));

      /* See whether we have already an alias with this name defined.
	 We do allow regular expressions matching this any alias since
	 this expression can also match other names and we test for aliases
	 before testing for modules.  */
      if (! from_is_regex)
	{
	  struct gconv_alias fake_alias;

	  fake_alias.fromname = new_module->from_constpfx;

	  if (__tfind (&fake_alias, &__gconv_alias_db, __gconv_alias_compare)
	      != NULL)
	    {
	      /* This module duplicates an alias.  */
	      free (new_module);
	      return;
	    }
	}

      /* Now insert the new module data structure in our search tree.  */
      insert_module (new_module);
    }
}


/* Read the next configuration file.  */
static void
internal_function
read_conf_file (const char *filename, const char *directory, size_t dir_len,
		void **modules, size_t *nmodules)
{
  FILE *fp = fopen (filename, "r");
  char *line = NULL;
  size_t line_len = 0;
  int modcounter = 0;

  /* Don't complain if a file is not present or readable, simply silently
     ignore it.  */
  if (fp == NULL)
    return;

  /* Process the known entries of the file.  Comments start with `#' and
     end with the end of the line.  Empty lines are ignored.  */
  while (!feof_unlocked (fp))
    {
      char *rp, *endp, *word;
      ssize_t n = __getdelim (&line, &line_len, '\n', fp);
      if (n < 0)
	/* An error occurred.  */
	break;

      rp = line;
      /* Terminate the line (excluding comments or newline) by an NUL byte
	 to simplify the following code.  */
      endp = strchr (rp, '#');
      if (endp != NULL)
	*endp = '\0';
      else
	if (rp[n - 1] == '\n')
	  rp[n - 1] = '\0';

      while (isspace (*rp))
	++rp;

      /* If this is an empty line go on with the next one.  */
      if (rp == endp)
	continue;

      word = rp;
      while (*rp != '\0' && !isspace (*rp))
	++rp;

      if (rp - word == sizeof ("alias") - 1
	  && memcmp (word, "alias", sizeof ("alias") - 1) == 0)
	add_alias (rp, *modules);
      else if (rp - word == sizeof ("module") - 1
	       && memcmp (word, "module", sizeof ("module") - 1) == 0)
	add_module (rp, directory, dir_len, modules, nmodules, modcounter++);
      /* else */
	/* Otherwise ignore the line.  */
    }

  if (line != NULL)
    free (line);
  fclose (fp);
}


/* Read all configuration files found in the user-specified and the default
   path.  */
void
__gconv_read_conf (void)
{
  const char *user_path = __secure_getenv ("GCONV_PATH");
  char *gconv_path, *elem;
  void *modules = NULL;
  size_t nmodules = 0;
  int save_errno = errno;
  size_t cnt;

  if (user_path == NULL)
    /* No user-defined path.  Make a modifiable copy of the default path.  */
    gconv_path = strdupa (default_gconv_path);
  else
    {
      /* Append the default path to the user-defined path.  */
      size_t user_len = strlen (user_path);

      gconv_path = alloca (user_len + 1 + sizeof (default_gconv_path));
      __mempcpy (__mempcpy (__mempcpy (gconv_path, user_path, user_len),
			    ":", 1),
		 default_gconv_path, sizeof (default_gconv_path));
    }

  elem = __strtok_r (gconv_path, ":", &gconv_path);
  while (elem != NULL)
    {
#ifndef MAXPATHLEN
      /* We define a reasonable limit.  */
# define MAXPATHLEN 4096
#endif
      char real_elem[MAXPATHLEN];

      if (__realpath (elem, real_elem) != NULL)
	{
	  size_t elem_len = strlen (real_elem);
	  char *filename;

	  filename = alloca (elem_len + 1 + sizeof (gconv_conf_filename));
	  __mempcpy (__mempcpy (__mempcpy (filename, real_elem, elem_len),
				"/", 1),
		     gconv_conf_filename, sizeof (gconv_conf_filename));

	  /* Read the next configuration file.  */
	  read_conf_file (filename, real_elem, elem_len, &modules, &nmodules);
	}

      /* Get next element in the path.  */
      elem = __strtok_r (NULL, ":", &gconv_path);
    }

  /* Add the internal modules.  */
  for (cnt = 0; cnt < sizeof (builtin_modules) / sizeof (builtin_modules[0]);
       ++cnt)
    {
      if (builtin_modules[cnt].from_pattern == NULL)
	{
	  struct gconv_alias fake_alias;

	  fake_alias.fromname = builtin_modules[cnt].from_constpfx;

	  if (__tfind (&fake_alias, &__gconv_alias_db, __gconv_alias_compare)
	      != NULL)
	    /* It'll conflict so don't add it.  */
	    continue;
	}

      insert_module (&builtin_modules[cnt]);
    }

  /* Add aliases for builtin conversions.  */
  cnt = sizeof (builtin_aliases) / sizeof (builtin_aliases[0]);
  while (cnt > 0)
    {
      char *copy = strdupa (builtin_aliases[--cnt]);
      add_alias (copy, modules);
    }

  /* Restore the error number.  */
  __set_errno (save_errno);
}
