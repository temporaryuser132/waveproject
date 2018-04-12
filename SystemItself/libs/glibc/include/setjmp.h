#ifndef _SETJMP_H
#include <setjmp/setjmp.h>

/* Now define the internal interfaces.  */

/* Internal machine-dependent function to restore context sans signal mask.  */
extern void __longjmp __P ((__jmp_buf __env, int __val))
     __attribute__ ((__noreturn__));

/* Internal function to possibly save the current mask of blocked signals
   in ENV, and always set the flag saying whether or not it was saved.
   This is used by the machine-dependent definition of `__sigsetjmp'.
   Always returns zero, for convenience.  */
extern int __sigjmp_save __P ((jmp_buf __env, int __savemask));
#endif
