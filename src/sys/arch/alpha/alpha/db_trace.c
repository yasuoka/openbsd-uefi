/*	$OpenBSD: db_trace.c,v 1.2 1997/07/08 20:33:40 niklas Exp $	*/

/*
 * Copyright (c) 1997 Niklas Hallqvist.  All rights reserverd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Niklas Hallqvist.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_access.h>
#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>

/*
 * XXX This is not yet functional on optimized code.
 * However it might be useful for you if you turn off debugging.
 */
void
db_stack_trace_cmd(addr, have_addr, count, modif)
	db_expr_t       addr;
	int             have_addr;
	db_expr_t       count;
	char            *modif;
{
	u_long		*frame;
	int		i;
	db_addr_t	pc;

	if (count == -1)
		count = 65535;

	if (!have_addr)
		frame = (u_long *)ddb_regs.tf_regs[FRAME_SP];
	else
		frame = (u_long *)addr;

	while (count--) {
		db_printf("frame %p: \n", frame);
		pc = frame[0];
		db_printf("%lx(", pc);

		/*
		 * Print first possible arguments... like 6 of 'em.
		 */
		for (i = 0; i < 6; i++)
			db_printf("%lx, ", frame[2+i]);
		db_printf(") at ");
		db_printsym(pc, DB_STGY_PROC);
		db_printf("\n");

		frame = (u_long *)frame[1];
	}
}
