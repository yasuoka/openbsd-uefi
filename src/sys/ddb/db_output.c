/*	$OpenBSD: db_output.c,v 1.10 1997/05/29 03:00:21 mickey Exp $	*/
/*	$NetBSD: db_output.c,v 1.13 1996/04/01 17:27:14 christos Exp $	*/

/* 
 * Mach Operating System
 * Copyright (c) 1993,1992,1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/*
 * Printf and character output for debugger.
 */
#include <sys/param.h>
#include <sys/proc.h>

#include <machine/stdarg.h>

#include <dev/cons.h>

#include <machine/db_machdep.h>

#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_interface.h>
#include <ddb/db_sym.h>
#include <ddb/db_var.h>
#include <ddb/db_extern.h>

#include <lib/libkern/libkern.h>

/*
 *	Character output - tracks position in line.
 *	To do this correctly, we should know how wide
 *	the output device is - then we could zero
 *	the line position when the output device wraps
 *	around to the start of the next line.
 *
 *	Instead, we count the number of spaces printed
 *	since the last printing character so that we
 *	don't print trailing spaces.  This avoids most
 *	of the wraparounds.
 */

#ifndef	DB_MAX_LINE
#define	DB_MAX_LINE		24	/* maximum line */
#define DB_MAX_WIDTH		80	/* maximum width */
#endif	DB_MAX_LINE

#define DB_MIN_MAX_WIDTH	20	/* minimum max width */
#define DB_MIN_MAX_LINE		3	/* minimum max line */
#define CTRL(c)			((c) & 0xff)

int	db_output_position = 0;		/* output column */
int	db_output_line = 0;		/* output line number */
int	db_last_non_space = 0;		/* last non-space character */
int	db_tab_stop_width = 8;		/* how wide are tab stops? */
#define	NEXT_TAB(i) \
	((((i) + db_tab_stop_width) / db_tab_stop_width) * db_tab_stop_width)
int	db_max_line = DB_MAX_LINE;	/* output max lines */
int	db_max_width = DB_MAX_WIDTH;	/* output line width */
int	db_radix = 16;			/* output numbers radix */

#ifdef DDB
static void db_more __P((void));
#endif
static char *db_ksprintn __P((u_long, int, int *));
static void db_printf_guts __P((const char *, va_list));

/*
 * Force pending whitespace.
 */
void
db_force_whitespace()
{
	register int last_print, next_tab;

	last_print = db_last_non_space;
	while (last_print < db_output_position) {
	    next_tab = NEXT_TAB(last_print);
	    if (next_tab <= db_output_position) {
		while (last_print < next_tab) { /* DON'T send a tab!!! */
			cnputc(' ');
			last_print++;
		}
	    }
	    else {
		cnputc(' ');
		last_print++;
	    }
	}
	db_last_non_space = db_output_position;
}

#ifdef DDB
static void
db_more()
{
	register  char *p;
	int quit_output = 0;

	for (p = "--db_more--"; *p; p++)
	    cnputc(*p);
	switch(cngetc()) {
	case ' ':
	    db_output_line = 0;
	    break;
	case 'q':
	case CTRL('c'):
	    db_output_line = 0;
	    quit_output = 1;
	    break;
	default:
	    db_output_line--;
	    break;
	}
	p = "\b\b\b\b\b\b\b\b\b\b\b           \b\b\b\b\b\b\b\b\b\b\b";
	while (*p)
	    cnputc(*p++);
	if (quit_output) {
	    db_error(0);
	    /* NOTREACHED */
	}
}
#endif

/*
 * Output character.  Buffer whitespace.
 */
void
db_putchar(c)
	int	c;		/* character to output */
{
#ifdef DDB
	if (db_max_line >= DB_MIN_MAX_LINE && db_output_line >= db_max_line-1)
	    db_more();
#endif
	if (c > ' ' && c <= '~') {
	    /*
	     * Printing character.
	     * If we have spaces to print, print them first.
	     * Use tabs if possible.
	     */
	    db_force_whitespace();
	    cnputc(c);
	    db_output_position++;
	    if (db_max_width >= DB_MIN_MAX_WIDTH
		&& db_output_position >= db_max_width-1) {
		/* auto new line */
		cnputc('\n');
		db_output_position = 0;
		db_last_non_space = 0;
		db_output_line++;
	    }
	    db_last_non_space = db_output_position;
	}
	else if (c == '\n') {
	    /* Return */
	    cnputc(c);
	    db_output_position = 0;
	    db_last_non_space = 0;
	    db_output_line++;
#ifdef DDB
	    db_check_interrupt();
#endif
	}
	else if (c == '\t') {
	    /* assume tabs every 8 positions */
	    db_output_position = NEXT_TAB(db_output_position);
	}
	else if (c == ' ') {
	    /* space */
	    db_output_position++;
	}
	else if (c == '\007') {
	    /* bell */
	    cnputc(c);
	}
	/* other characters are assumed non-printing */
}

/*
 * Return output position
 */
int
db_print_position()
{
	return (db_output_position);
}

/*
 * Printing
 */

/*VARARGS1*/
int
#if __STDC__
db_printf(const char *fmt, ...)
#else
db_printf(fmt, va_alist)
	const char *fmt;
	va_dcl
#endif
{
	va_list	listp;
	va_start(listp, fmt);
	db_printf_guts (fmt, listp);
	va_end(listp);
	return 0;
}

/* alternate name */

/*VARARGS1*/
int
#if __STDC__
kdbprintf(const char *fmt, ...)
#else
kdbprintf(fmt, va_alist)
	char *fmt;
	va_dcl
#endif
{
	va_list	listp;
	va_start(listp, fmt);
	db_printf_guts (fmt, listp);
	va_end(listp);
	return 0;
}

/*
 * End line if too long.
 */
void
db_end_line()
{
	if (db_output_position >= db_max_width)
	    db_printf("\n");
}

/*
 * Put a number (base <= 16) in a buffer in reverse order; return an
 * optional length and a pointer to the NULL terminated (preceded?)
 * buffer.
 */
static char *
db_ksprintn(ul, base, lenp)
	register u_long ul;
	register int base, *lenp;
{					/* A long in base 8, plus NULL. */
	static char buf[sizeof(long) * NBBY / 3 + 2];
	register char *p;

	p = buf;
	do {
		*++p = "0123456789abcdef"[ul % base];
	} while (ul /= base);
	if (lenp)
		*lenp = p - buf;
	return (p);
}

static void
db_printf_guts(fmt, ap)
	register const char *fmt;
	va_list ap;
{
	register char *p;
	register int ch, n;
	u_long ul;
	int base, lflag, tmp, width;
	char padc;
	int ladjust;
	int sharpflag;
	int neg;

	for (;;) {
		padc = ' ';
		width = 0;
		while ((ch = *(u_char *)fmt++) != '%') {
			if (ch == '\0')
				return;
			db_putchar(ch);
		}
		lflag = 0;
		ladjust = 0;
		sharpflag = 0;
		neg = 0;
reswitch:	switch (ch = *(u_char *)fmt++) {
		case '0':
			padc = '0';
			goto reswitch;
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			for (width = 0;; ++fmt) {
				width = width * 10 + ch - '0';
				ch = *fmt;
				if (ch < '0' || ch > '9')
					break;
			}
			goto reswitch;
		case 'l':
			lflag = 1;
			goto reswitch;
		case '-':
			ladjust = 1;
			goto reswitch;
		case '#':
			sharpflag = 1;
			goto reswitch;
		case 'b':
			ul = va_arg(ap, int);
			p = va_arg(ap, char *);
			for (p = db_ksprintn(ul, *p++, NULL);
			     (ch = *p--) !='\0';)
				db_putchar(ch);

			if (!ul)
				break;

			for (tmp = 0; (n = *p++) != '\0';) {
				if (ul & (1 << (n - 1))) {
					db_putchar(tmp ? ',' : '<');
					for (; (n = *p) > ' '; ++p)
						db_putchar(n);
					tmp = 1;
				} else
					for (; *p > ' '; ++p);
			}
			if (tmp)
				db_putchar('>');
			break;
		case '*':
			width = va_arg (ap, int);
			if (width < 0) {
				ladjust = !ladjust;
				width = -width;
			}
			goto reswitch;
		case ':':
			p = va_arg(ap, char *);
			db_printf_guts (p, va_arg(ap, va_list));
			break;
		case 'c':
			db_putchar(va_arg(ap, int));
			break;
		case 's':
			p = va_arg(ap, char *);
			width -= strlen (p);
			if (!ladjust && width > 0)
				while (width--)
					db_putchar (padc);
			while ((ch = *p++) != '\0')
				db_putchar(ch);
			if (ladjust && width > 0)
				while (width--)
					db_putchar (padc);
			break;
		case 'r':
			ul = lflag ? va_arg(ap, u_long) : va_arg(ap, u_int);
			if ((long)ul < 0) {
				neg = 1;
				ul = -(long)ul;
			}
			base = db_radix;
			if (base < 8 || base > 16)
				base = 10;
			goto number;
		case 'n':
			ul = lflag ? va_arg(ap, u_long) : va_arg(ap, u_int);
			base = db_radix;
			if (base < 8 || base > 16)
				base = 10;
			goto number;
		case 'd':
			ul = lflag ? va_arg(ap, long) : va_arg(ap, int);
			if ((long)ul < 0) {
				neg = 1;
				ul = -(long)ul;
			}
			base = 10;
			goto number;
		case 'o':
			ul = lflag ? va_arg(ap, u_long) : va_arg(ap, u_int);
			base = 8;
			goto number;
		case 'p':
			db_putchar ('0');
			db_putchar ('x');
			ul = (u_long) va_arg(ap, void *);
			base = 16;
			goto number;
		case 'u':
			ul = lflag ? va_arg(ap, u_long) : va_arg(ap, u_int);
			base = 10;
			goto number;
		case 'z':
			ul = lflag ? va_arg(ap, u_long) : va_arg(ap, u_int);
			if ((long)ul < 0) {
				neg = 1;
				ul = -(long)ul;
			}
			base = 16;
			goto number;
		case 'x':
			ul = lflag ? va_arg(ap, u_long) : va_arg(ap, u_int);
			base = 16;
number:			p = (char *)db_ksprintn(ul, base, &tmp);
			if (sharpflag && ul != 0) {
				if (base == 8)
					tmp++;
				else if (base == 16)
					tmp += 2;
			}
			if (neg)
				tmp++;

			if (!ladjust && width && (width -= tmp) > 0)
				while (width--)
					db_putchar(padc);
			if (neg)
				db_putchar ('-');
			if (sharpflag && ul != 0) {
				if (base == 8) {
					db_putchar ('0');
				} else if (base == 16) {
					db_putchar ('0');
					db_putchar ('x');
				}
			}
			if (ladjust && width && (width -= tmp) > 0)
				while (width--)
					db_putchar(padc);

			while ((ch = *p--) != '\0')
				db_putchar(ch);
			break;
		default:
			db_putchar('%');
			if (lflag)
				db_putchar('l');
			/* FALLTHROUGH */
		case '%':
			db_putchar(ch);
		}
	}
}
