/*	$OpenBSD: db_trap.c,v 1.10 2001/11/06 19:53:18 miod Exp $	*/
/*	$NetBSD: db_trap.c,v 1.9 1996/02/05 01:57:18 christos Exp $	*/

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
 *
 * 	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

/*
 * Trap entry point to kernel debugger.
 */
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/db_machdep.h>

#include <ddb/db_run.h>
#include <ddb/db_command.h>
#include <ddb/db_break.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>

void
db_trap(type, code)
	int	type, code;
{
	boolean_t	bkpt;
	boolean_t	watchpt;

	bkpt = IS_BREAKPOINT_TRAP(type, code);
	watchpt = IS_WATCHPOINT_TRAP(type, code);

	if (db_stop_at_pc(DDB_REGS, &bkpt)) {
		if (db_inst_count) {
			db_printf("After %d instructions ", db_inst_count);
			db_printf("(%d loads, %d stores),\n", db_load_count,
			    db_store_count);
		}
		if (bkpt)
			db_printf("Breakpoint at\t");
		else if (watchpt)
			db_printf("Watchpoint at\t");
		else
			db_printf("Stopped at\t");
		db_dot = PC_REGS(DDB_REGS);
		db_print_loc_and_inst(db_dot);

		if (panicstr != NULL) {
			if (db_print_position() != 0)
				db_printf("\n");
			db_printf("RUN AT LEAST 'trace' AND 'ps' AND INCLUDE "
			    "OUTPUT WHEN REPORTING THIS PANIC!\n");
			db_printf("DO NOT EVEN BOTHER REPORTING THIS WITHOUT "
			    "INCLUDING THAT INFORMATION!\n");
		}

		db_command_loop();
	}

	db_restart_at_pc(DDB_REGS, watchpt);
}
