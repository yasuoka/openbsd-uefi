/*	$OpenBSD: va-m88k.h,v 1.10 2003/08/01 07:44:05 miod Exp $	*/

/* This file has local changes by MOTOROLA
Thu Sep  9 09:06:29 CDT 1993 Dale Rahn (drahn@pacific)
	* Due to C-Front's usage of __alignof__ builtin the
	usage of it must be changed to have an object of that type
	as the argument not just the type.
 */
/* GNU C varargs support for the Motorola 88100  */

/* Define __gnuc_va_list.  */

#ifndef __GNUC_VA_LIST
#define __GNUC_VA_LIST

typedef struct __va_list_tag {
	int  __va_arg;		/* argument number */
	int *__va_stk;		/* start of args passed on stack */
	int *__va_reg;		/* start of args passed in regs */
} __va_list[1], __gnuc_va_list[1];

#endif /* not __GNUC_VA_LIST */

/* If this is for internal libc use, don't define anything but
   __gnuc_va_list.  */
#if defined (_STDARG_H) || defined (_VARARGS_H)

#define __va_start_common(AP,FAKE) \
__extension__ ({							\
   (AP) = (struct __va_list_tag *)__builtin_alloca(sizeof(__gnuc_va_list)); \
  __builtin_memcpy ((AP), __builtin_saveregs (), sizeof(__gnuc_va_list)); \
  })

#ifdef _STDARG_H /* stdarg.h support */

/* Calling __builtin_next_arg gives the proper error message if LASTARG is
   not indeed the last argument.  */
#define va_start(AP,LASTARG) \
  (__builtin_next_arg (LASTARG), __va_start_common (AP, 0))

#else /* varargs.h support */

#define va_start(AP) __va_start_common (AP, 1)
#define va_alist __va_1st_arg
#define va_dcl register int va_alist; ...

#endif /* _STDARG_H */

#define __va_reg_p(TYPE) \
  (__builtin_classify_type(*(TYPE *)0) < 12 \
   ? sizeof(TYPE) <= 8 : sizeof(TYPE) == 4 && __alignof__(*(TYPE *)0) == 4)

#define	__va_size(TYPE) ((sizeof(TYPE) + 3) >> 2)

/* We cast to void * and then to TYPE * because this avoids
   a warning about increasing the alignment requirement.  */
#define va_arg(AP,TYPE)							   \
  ( (AP)->__va_arg = (((AP)->__va_arg + (1 << (__alignof__(*(TYPE *)0) >> 3)) - 1) \
		     & ~((1 << (__alignof__(*(TYPE *)0) >> 3)) - 1))	   \
    + __va_size(TYPE),							   \
    *((TYPE *) (void *) ((__va_reg_p(TYPE)				   \
			  && (AP)->__va_arg < 8 + __va_size(TYPE)	   \
			  ? (AP)->__va_reg : (AP)->__va_stk)		   \
			 + ((AP)->__va_arg - __va_size(TYPE)))))

#define va_end(AP)

/* Copy __gnuc_va_list into another variable of this type.  */
#define __va_copy(dest, src) \
__extension__ ({ \
	(dest) =  \
	   (struct __va_list_tag *)__builtin_alloca(sizeof(__gnuc_va_list)); \
	*(dest) = *(src);\
  })

#if !defined(_ANSI_SOURCE) && \
    (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) || \
	defined(_ISOC99_SOURCE) || (__STDC_VERSION__ - 0) >= 199901L)
#define va_copy(dest, src) __va_copy(dest, src)
#endif

#endif /* defined (_STDARG_H) || defined (_VARARGS_H) */
