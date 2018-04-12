/* This file defines USE___THREAD to 1 or 0 to cut down on the #if mess.  */

#include_next <tls.h>

#if USE_TLS && HAVE___THREAD \
    && (!defined NOT_IN_libc || defined IS_IN_libpthread)

# define USE___THREAD 1

#else

# define USE___THREAD 0

#endif