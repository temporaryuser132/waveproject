#ifndef _RESOLV_H_

#define RES_SET_H_ERRNO(r,x)			\
  do						\
    {						\
      (r)->res_h_errno = x;			\
      __set_h_errno(x);				\
    }						\
  while (0)

#include <resolv/resolv.h>

#ifdef _RESOLV_H_

# ifdef _LIBC_REENTRANT
#  include <tls.h>
#  if USE___THREAD
#   undef _res
#   ifndef NOT_IN_libc
#    define _res __libc_res
#   endif
extern __thread struct __res_state _res attribute_tls_model_ie;
#  endif
# else
#  ifndef __BIND_NOSTATIC
#   undef _res
extern struct __res_state _res;
#  endif
# endif

/* Now define the internal interfaces.  */
extern int __res_vinit (res_state, int);
extern void _sethtent (int);
extern void _endhtent (void);
extern struct hostent *_gethtent (void);
extern struct hostent *_gethtbyname (const char *__name);
extern struct hostent *_gethtbyname2 (const char *__name, int __af);
struct hostent *_gethtbyaddr (const char *addr, size_t __len, int __af);
extern u_int32_t _getlong (const u_char *__src);
extern u_int16_t _getshort (const u_char *__src);
extern void res_pquery (const res_state __statp, const u_char *__msg,
			int __len, FILE *__file);
extern void res_send_setqhook (res_send_qhook __hook);
extern void res_send_setrhook (res_send_rhook __hook);
extern int res_ourserver_p (const res_state __statp,
			    const struct sockaddr_in6 *__inp);
libc_hidden_proto (__res_ninit)
libc_hidden_proto (__res_nclose)
libc_hidden_proto (__res_randomid)
libc_hidden_proto (__res_state)

int __libc_res_nquery (res_state, const char *, int, int, u_char *, int,
		       u_char **);
int __libc_res_nsearch (res_state, const char *, int, int, u_char *, int,
			u_char **);
int __libc_res_nsend (res_state, const u_char *, int, u_char *, int,
		      u_char **)
  attribute_hidden;

#endif

#endif
