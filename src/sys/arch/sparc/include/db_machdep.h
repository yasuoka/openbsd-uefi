/*	$OpenBSD: db_machdep.h,v 1.4 1997/08/08 08:26:13 downsj Exp $	*/
/*	$NetBSD: db_machdep.h,v 1.9 1997/06/26 01:27:00 thorpej Exp $ */

/*
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
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

#ifndef	_SPARC_DB_MACHDEP_H_
#define	_SPARC_DB_MACHDEP_H_

/*
 * Machine-dependent defines for new kernel debugger.
 */


#include <vm/vm.h>
#include <machine/frame.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/reg.h>

/* end of mangling */

typedef	vm_offset_t	db_addr_t;	/* address - unsigned */
typedef	long		db_expr_t;	/* expression - signed */

typedef struct {
	struct trapframe ddb_tf;
	struct frame	 ddb_fr;
} db_regs_t;

db_regs_t		ddb_regs;	/* register state */
#define	DDB_REGS	(&ddb_regs)
#define	DDB_TF		(&ddb_regs.ddb_tf)
#define	DDB_FR		(&ddb_regs.ddb_fr)

#if defined(lint)
#define	PC_REGS(regs)	((regs)->ddb_tf.tf_pc)
#else
#define	PC_REGS(regs)	((db_addr_t)(regs)->ddb_tf.tf_pc)
#endif

#define	BKPT_INST	0x91d02001	/* breakpoint instruction */
#define	BKPT_SIZE	(4)		/* size of breakpoint inst */
#define	BKPT_SET(inst)	(BKPT_INST)

#define	db_clear_single_step(regs)	(void) (0)
#define	db_set_single_step(regs)	(void) (0)

#define	IS_BREAKPOINT_TRAP(type, code)	((type) == T_BREAKPOINT)
#define IS_WATCHPOINT_TRAP(type, code)	(0)

#define	inst_trap_return(ins)	((ins)&0)
#define	inst_return(ins)	((ins)&0)
#define	inst_call(ins)		((ins)&0)
#define inst_load(ins)		0
#define inst_store(ins)		0

#define DB_MACHINE_COMMANDS

void db_machine_init __P((void));
int kdb_trap __P((int, struct trapframe *));

/*
 * We use a.out symbols in DDB.
 */
#define	DB_AOUT_SYMBOLS

#endif	/* _SPARC_DB_MACHDEP_H_ */
