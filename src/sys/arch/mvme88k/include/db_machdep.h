/*	$OpenBSD: db_machdep.h,v 1.21 2002/03/14 03:15:57 millert Exp $ */
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */
/*
 * HISTORY
 */

/*
 * Machine-dependent defined for the new kernel debugger
 */

#ifndef  _M88K_DB_MACHDEP_H_
#define  _M88K_DB_MACHDEP_H_

/* trap numbers used by ddb */
#define	DDB_ENTRY_BKPT_NO	130
#define	DDB_ENTRY_TRACE_NO	131
#define DDB_ENTRY_TRAP_NO	132

#ifndef	_LOCORE

#include <machine/pcb.h>	/* m88100_saved_state */
#include <machine/psl.h>
#include <machine/trap.h>

#include <uvm/uvm_param.h>

/* 
 * This is a hack so that mc88100 can use software single step
 * and mc88110 can use the wonderful hardware single step 
 * feature. XXX smurph
 */
#define INTERNAL_SSTEP		/* Use local Single Step routines */

#define BKPT_SIZE	(4)	/* number of bytes in bkpt inst. */
#define BKPT_INST	(0xF000D000 | DDB_ENTRY_BKPT_NO) /* tb0, 0,r0, vector 130 */
#define BKPT_SET(inst)	(BKPT_INST)

/* Entry trap for the debugger - used for inline assembly breaks*/
#define ENTRY_ASM       	"tb0 0, r0, 132"

typedef	vm_offset_t		db_addr_t;
typedef	int			db_expr_t;
typedef	struct m88100_saved_state db_regs_t;
extern db_regs_t	ddb_regs;	/* register state */
#define	DDB_REGS	(&ddb_regs)

/*
 * the low two bits of sxip, snip, sfip have valid bits
 * in them that need to masked to get the correct addresses
 */

#define m88k_pc(regs) \
({ \
    int ret; \
 \
    if (cputyp == CPU_88110) \
	ret = regs->exip & ~3; \
    else if (regs->sxip & 2) /* is valid */ \
	ret = regs->sxip & ~3; \
    else if (regs->snip & 2) \
	ret = regs->snip & ~3; \
    else if (regs->sfip & 2) \
	ret = regs->sfip & ~3; \
    /* we are in trouble - none of the program counters is valid */ \
    ret;  \
})

/*
 * This is an actual function due to the fact that the sxip
 * or snip could be nooped out due to a jmp or rte
 */
#define PC_REGS(regs) cputyp == CPU_88110 ? (regs->exip & ~3) :\
	((regs->sxip & 2) ?  regs->sxip & ~3 : \
	(regs->snip & 2 ? regs->snip & ~3 : regs->sfip & ~3))
#define l_PC_REGS(regs) cputyp == CPU_88110 ? (regs->exip & ~3) :\
	((regs->sxip & 2) ?  regs->sxip : \
	(regs->snip & 2 ? regs->snip : regs->sfip ))

#define pC_REGS(regs) cputyp == CPU_88110 ? (regs->exip & ~3) :\
	(regs->sxip & 2) ? regs->sxip : (regs->snip & 2 ? \
				regs->snip : regs->sfip)
extern int db_noisy;
#define NOISY(x) if (db_noisy) x
#define NOISY2(x) if (db_noisy >= 2) x
#define NOISY3(x) if (db_noisy >= 3) x

extern int quiet_db_read_bytes;

/* These versions are not constantly doing SPL */
/*#define	cnmaygetc	db_getc*/
/*#define	cngetc		db_getc*/
/*#define	cnputc		db_putc*/

unsigned inst_load(unsigned);
unsigned inst_store(unsigned);
boolean_t inst_branch(unsigned);
db_addr_t next_instr_address(db_addr_t, unsigned);
db_addr_t branch_taken(u_int, db_addr_t, db_expr_t (*)(db_regs_t *, int),
		       db_regs_t *);
int ddb_break_trap(int type, db_regs_t *eframe);
int ddb_entry_trap(int level, db_regs_t *eframe);

/* breakpoint/watchpoint foo */
#define IS_BREAKPOINT_TRAP(type,code) ((type)==T_KDB_BREAK)
#if defined(T_WATCHPOINT)
#define IS_WATCHPOINT_TRAP(type,code) ((type)==T_KDB_WATCH)
#else
#define IS_WATCHPOINT_TRAP(type,code) 0
#endif /* T_WATCHPOINT */

/* we don't want coff support */
#define DB_NO_COFF 1

#ifdef INTERNAL_SSTEP
extern register_t getreg_val(db_regs_t *, int);
void db_set_single_step(register db_regs_t *);
void db_clear_single_step(register db_regs_t *);
#else
/* need software single step */
#define SOFTWARE_SSTEP 1 /* we need this for mc88100 */
#endif 

/*
 * Debugger can get to any address space
 */

#define DB_ACCESS_LEVEL DB_ACCESS_ANY

#define DB_VALID_KERN_ADDR(addr) (!badaddr((void *)(addr), 1))
#define DB_VALID_ADDRESS(addr,user) \
  (user ? db_check_user_addr(addr) : DB_VALID_KERN_ADDR(addr))

/* instruction type checking - others are implemented in db_sstep.c */

#define inst_trap_return(ins)  ((ins) == 0xf400fc00U)

/* don't need to load symbols */
#define DB_SYMBOLS_PRELOADED 1

/* machine specific commands have been added to ddb */
#define DB_MACHINE_COMMANDS 1
/* inst_return(ins) - is the instruction a function call return.
 * Not mutually exclusive with inst_branch. Should be a jmp r1. */
#define inst_return(I) (((I)&0xfffffbffU) == 0xf400c001U ? TRUE : FALSE)

#ifdef __GNUC__
/*
 * inst_call - function call predicate: is the instruction a function call.
 * Could be either bsr or jsr
 */
#define inst_call(I) ({ unsigned i = (I); \
	((((i) & 0xf8000000U) == 0xc8000000U || /*bsr*/ \
          ((i) & 0xfffffbe0U) == 0xf400c800U)   /*jsr*/ \
	? TRUE : FALSE) \
;})

/*
 * This routine should return true for instructions that result in unconditonal
 * transfers of the flow of control. (Unconditional Jumps, subroutine calls,
 * subroutine returns, etc).
 *
 *  Trap and return from trap  should not  be listed here.
 */
#define inst_unconditional_flow_transfer(I) ({ unsigned i = (I); \
    ((((i) & 0xf0000000U) == 0xc0000000U || /* br, bsr */ \
      ((i) & 0xfffff3e0U) == 0xf400c000U)   /* jmp, jsr */ \
     ? TRUE: FALSE) \
;})

/* Return true if the instruction has a delay slot.  */
#define db_branch_is_delayed(I)	inst_delayed(I)

#endif	/* __GNUC__ */

#define	db_printf_enter	db_printing

int m88k_print_instruction(unsigned iadr, long inst);

#endif	/* _LOCORE */

#endif	/* _M88K_DB_MACHDEP_H_ */
