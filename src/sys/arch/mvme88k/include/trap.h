/*	$OpenBSD: trap.h,v 1.13 2001/12/13 08:55:51 smurph Exp $ */
/* 
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
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
 * Trap codes 
 */
#ifndef __MACHINE_TRAP_H__
#define __MACHINE_TRAP_H__

/*
 * Trap type values
 */

#define T_RESADFLT	0	/* reserved addressing fault */
#define T_PRIVINFLT	1	/* privileged instruction fault */
#define T_RESOPFLT	2	/* reserved operand fault */

/* End of known constants */

#define T_INSTFLT	3	/* instruction access exception */
#define T_DATAFLT	4	/* data access exception */
#define T_MISALGNFLT	5	/* misaligned access exception */
#define T_ILLFLT	6	/* unimplemented opcode exception */
#define T_BNDFLT	7	/* bounds check violation exception */
#define T_ZERODIV	8	/* illegal divide exception */
#define T_OVFFLT	9	/* integer overflow exception */
#define T_ERRORFLT	10	/* error exception */
#define T_FPEPFLT	11	/* floating point precise exception */
#define T_FPEIFLT	12	/* floating point imprecise exception */
#define T_ASTFLT	13	/* software trap */
#if	DDB
#define	T_KDB_ENTRY	14	/* force entry to kernel debugger */
#define T_KDB_BREAK	15	/* break point hit */
#define T_KDB_TRACE	16	/* trace */
#endif /* DDB */
#define T_UNKNOWNFLT	17	/* unknown exception */
#define T_SIGTRAP	18	/* generate SIGTRAP */
#define T_SIGSYS	19	/* generate SIGSYS */
#define T_STEPBPT	20	/* special breakpoint for single step */
#define T_USERBPT	21	/* user set breakpoint (for debugger) */
#define T_SYSCALL	22	/* Syscall */
#define T_NON_MASK	23	/* MVME197 Non-Maskable Interrupt */
#if	DDB
#define	T_KDB_WATCH	24	/* watchpoint hit */
#endif /* DDB */
#define T_197_READ	25	/* MVME197 Data Read Miss (Software Table Searches) */
#define T_197_WRITE	26	/* MVME197 Data Write Miss (Software Table Searches) */
#define T_197_INST	27	/* MVME197 Inst ATC Miss (Software Table Searches) */
#define T_INT		28	/* interrupt exception */
#define T_USER		29	/* user mode fault */

#ifndef _LOCORE
void panictrap(int type, struct m88100_saved_state *frame);
void test_trap(struct m88100_saved_state *frame);
void error_fault(struct m88100_saved_state *frame);
void error_reset(struct m88100_saved_state *frame);
unsigned ss_get_value(struct proc *p, unsigned addr, int size);
int ss_put_value(struct proc *p, unsigned addr, unsigned value, int size);
unsigned ss_branch_taken(unsigned inst, unsigned pc, 
			 unsigned (*func)(unsigned int, struct trapframe *),
			 struct trapframe *func_data);  /* 'opaque' */
unsigned ss_getreg_val(unsigned regno, struct trapframe *tf);
int ss_inst_branch(unsigned ins);
int ss_inst_delayed(unsigned ins);
unsigned ss_next_instr_address(struct proc *p, unsigned pc, unsigned delay_slot);
int cpu_singlestep(register struct proc *p);

#ifdef M88100
void m88100_trap __P((unsigned, struct m88100_saved_state *));
void m88100_syscall __P((register_t, struct m88100_saved_state *));
#endif /* M88100 */

#ifdef M88110
void m88110_trap __P((unsigned, struct m88100_saved_state *));
void m88110_syscall __P((register_t, struct m88100_saved_state *));
#endif /* M88110 */

/* machine dependant trap and syscall macros */
#define trap(type, frame)	(*md.interrupt_func)(type, frame)
#define syscall(code, frame)	(*md.syscall_func)(code, frame)

#endif /* _LOCORE */

#endif /* __MACHINE_TRAP_H__ */

