/*	$OpenBSD: db_machdep.c,v 1.26 2010/01/16 23:28:10 miod Exp $ */

/*
 * Copyright (c) 1998-2003 Opsycon AB (www.opsycon.se)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <dev/cons.h>

#include <machine/autoconf.h>
#include <machine/db_machdep.h>
#include <machine/cpu.h>
#include <machine/mips_opcode.h>
#include <machine/pte.h>
#include <machine/frame.h>
#include <machine/regnum.h>

#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#include <ddb/db_access.h>
#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_variables.h>
#include <ddb/db_interface.h>

#define MIPS_JR_RA        0x03e00008      /* instruction code for jr ra */

extern void trapDump(char *);
u_long MipsEmulateBranch(db_regs_t *, int, int, u_int);
void  stacktrace_subr(db_regs_t *, int, int (*)(const char*, ...));

uint32_t kdbpeek(vaddr_t);
uint64_t kdbpeekd(vaddr_t);
uint16_t kdbpeekw(vaddr_t);
uint8_t  kdbpeekb(vaddr_t);
void  kdbpoke(vaddr_t, uint32_t);
void  kdbpoked(vaddr_t, uint64_t);
void  kdbpokew(vaddr_t, uint16_t);
void  kdbpokeb(vaddr_t, uint8_t);
int   kdb_trap(int, struct trap_frame *);

void db_trap_trace_cmd(db_expr_t, int, db_expr_t, char *);
void db_dump_tlb_cmd(db_expr_t, int, db_expr_t, char *);

int   db_active = 0;
db_regs_t ddb_regs;

struct db_variable db_regs[] = {
    { "at",  (long *)&ddb_regs.ast,     FCN_NULL },
    { "v0",  (long *)&ddb_regs.v0,      FCN_NULL },
    { "v1",  (long *)&ddb_regs.v1,      FCN_NULL },
    { "a0",  (long *)&ddb_regs.a0,      FCN_NULL },
    { "a1",  (long *)&ddb_regs.a1,      FCN_NULL },
    { "a2",  (long *)&ddb_regs.a2,      FCN_NULL },
    { "a3",  (long *)&ddb_regs.a3,      FCN_NULL },
    { "a4",  (long *)&ddb_regs.t0,      FCN_NULL },
    { "a5",  (long *)&ddb_regs.t1,      FCN_NULL },
    { "a6",  (long *)&ddb_regs.t2,      FCN_NULL },
    { "a7",  (long *)&ddb_regs.t3,      FCN_NULL },
    { "t0",  (long *)&ddb_regs.t4,      FCN_NULL },
    { "t1",  (long *)&ddb_regs.t5,      FCN_NULL },
    { "t2",  (long *)&ddb_regs.t6,      FCN_NULL },
    { "t3",  (long *)&ddb_regs.t7,      FCN_NULL },
    { "s0",  (long *)&ddb_regs.s0,      FCN_NULL },
    { "s1",  (long *)&ddb_regs.s1,      FCN_NULL },
    { "s2",  (long *)&ddb_regs.s2,      FCN_NULL },
    { "s3",  (long *)&ddb_regs.s3,      FCN_NULL },
    { "s4",  (long *)&ddb_regs.s4,      FCN_NULL },
    { "s5",  (long *)&ddb_regs.s5,      FCN_NULL },
    { "s6",  (long *)&ddb_regs.s6,      FCN_NULL },
    { "s7",  (long *)&ddb_regs.s7,      FCN_NULL },
    { "t8",  (long *)&ddb_regs.t8,      FCN_NULL },
    { "t9",  (long *)&ddb_regs.t9,      FCN_NULL },
    { "k0",  (long *)&ddb_regs.k0,      FCN_NULL },
    { "k1",  (long *)&ddb_regs.k1,      FCN_NULL },
    { "gp",  (long *)&ddb_regs.gp,      FCN_NULL },
    { "sp",  (long *)&ddb_regs.sp,      FCN_NULL },
    { "s8",  (long *)&ddb_regs.s8,      FCN_NULL },
    { "ra",  (long *)&ddb_regs.ra,      FCN_NULL },
    { "sr",  (long *)&ddb_regs.sr,      FCN_NULL },
    { "lo",  (long *)&ddb_regs.mullo,   FCN_NULL },
    { "hi",  (long *)&ddb_regs.mulhi,   FCN_NULL },
    { "bad", (long *)&ddb_regs.badvaddr,FCN_NULL },
    { "cs",  (long *)&ddb_regs.cause,   FCN_NULL },
    { "pc",  (long *)&ddb_regs.pc,      FCN_NULL },
};
struct db_variable *db_eregs = db_regs + sizeof(db_regs)/sizeof(db_regs[0]);

extern label_t  *db_recover;

int
kdb_trap(type, fp)
	int type;
	struct trap_frame *fp;
{
	switch(type) {
	case T_BREAK:		/* breakpoint */
		if (db_get_value((fp)->pc, sizeof(int), FALSE) == BREAK_SOVER) {
			(fp)->pc += BKPT_SIZE;
		}
		break;
	case -1:
		break;
	default:
#if 0
		if (!db_panic)
			return (0);
#endif
		if (db_recover != 0) {
			db_error("Caught exception in ddb.\n");
			/*NOTREACHED*/
		}
		printf("stopped on non ddb fault\n");
	}

	bcopy((void *)fp, (void *)&ddb_regs, NUMSAVEREGS * sizeof(register_t));

	db_active++;
	cnpollc(TRUE);
	db_trap(type, 0);
	cnpollc(FALSE);
	db_active--;

	bcopy((void *)&ddb_regs, (void *)fp, NUMSAVEREGS * sizeof(register_t));
	return(TRUE);
}

void
db_read_bytes(addr, size, data)
	vaddr_t addr;
	size_t      size;
	char       *data;
{
	while (size >= sizeof(uint32_t)) {
		*((uint32_t *)data)++ = kdbpeek(addr);
		addr += sizeof(uint32_t);
		size -= sizeof(uint32_t);
	}

	if (size >= sizeof(uint16_t)) {
		*((uint16_t *)data)++ = kdbpeekw(addr);
		addr += sizeof(uint16_t);
		size -= sizeof(uint16_t);
	}

	if (size)
		*(uint8_t *)data = kdbpeekb(addr);
}

void
db_write_bytes(addr, size, data)
	vaddr_t addr;
	size_t      size;
	char       *data;
{
	vaddr_t ptr = addr;
	size_t len = size;

	while (len >= sizeof(uint32_t)) {
		kdbpoke(ptr, *((uint32_t *)data)++);
		ptr += sizeof(uint32_t);
		len -= sizeof(uint32_t);
	}

	if (len >= sizeof(uint16_t)) {
		kdbpokew(ptr, *((uint16_t *)data)++);
		ptr += sizeof(uint16_t);
		len -= sizeof(uint16_t);
	}

	if (len)
		kdbpokeb(ptr, *(uint8_t *)data);

	if (addr < VM_MAXUSER_ADDRESS) {
		struct cpu_info *ci = curcpu();

		/* XXX we don't know where this page is mapped... */
		Mips_HitSyncDCache(ci, addr, PHYS_TO_XKPHYS(addr, CCA_CACHED),
		    size);
		Mips_InvalidateICache(ci, PHYS_TO_CKSEG0(addr & 0xffff), size);
	}
}

void
db_stack_trace_print(addr, have_addr, count, modif, pr)
	db_expr_t	addr;
	boolean_t	have_addr;
	db_expr_t	count;
	char		*modif;
	int		(*pr)(const char *, ...);
{
	struct trap_frame *regs = &ddb_regs;

	if (have_addr) {
		(*pr)("mips trace requires a trap frame... giving up\n");
		return;
	}

	stacktrace_subr(regs, count, pr);
}

/*
 *	To do a single step ddb needs to know the next address
 *	that we will get to. It means that we need to find out
 *	both the address for a branch taken and for not taken, NOT! :-)
 *	MipsEmulateBranch will do the job to find out _exactly_ which
 *	address we will end up at so the 'dual bp' method is not
 *	required.
 */
db_addr_t
next_instr_address(db_addr_t pc, boolean_t bd)
{
	db_addr_t next;

	next = MipsEmulateBranch(&ddb_regs, pc, 0, 0);
	return(next);
}


/*
 *	Decode instruction and figure out type.
 */
int
db_inst_type(ins)
	int	ins;
{
	InstFmt	inst;
	int	ityp = 0;

	inst.word = ins;
	switch ((int)inst.JType.op) {
	case OP_SPECIAL:
		switch ((int)inst.RType.func) {
		case OP_JR:
			ityp = IT_BRANCH;
			break;
		case OP_JALR:
		case OP_SYSCALL:
			ityp = IT_CALL;
			break;
		}
		break;

	case OP_BCOND:
		switch ((int)inst.IType.rt) {
		case OP_BLTZ:
		case OP_BLTZL:
		case OP_BGEZ:
		case OP_BGEZL:
			ityp = IT_BRANCH;
			break;

		case OP_BLTZAL:
		case OP_BLTZALL:
		case OP_BGEZAL:
		case OP_BGEZALL:
			ityp = IT_CALL;
			break;
		}
		break;

	case OP_JAL:
		ityp = IT_CALL;
		break;

	case OP_J:
	case OP_BEQ:
	case OP_BEQL:
	case OP_BNE:
	case OP_BNEL:
	case OP_BLEZ:
	case OP_BLEZL:
	case OP_BGTZ:
	case OP_BGTZL:
		ityp = IT_BRANCH;
		break;

	case OP_COP1:
		switch (inst.RType.rs) {
		case OP_BCx:
		case OP_BCy:
			ityp = IT_BRANCH;
			break;
		}
		break;

	case OP_LB:
	case OP_LH:
	case OP_LW:
	case OP_LD:
	case OP_LBU:
	case OP_LHU:
	case OP_LWU:
	case OP_LWC1:
		ityp = IT_LOAD;
		break;

	case OP_SB:
	case OP_SH:
	case OP_SW:
	case OP_SD:
	case OP_SWC1:
		ityp = IT_STORE;
		break;
	}
	return (ityp);
}

/*
 *  MIPS machine dependent DDB commands.
 */

/*
 *  Do a trap traceback.
 */
void
db_trap_trace_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *m)
{
	trapDump("ddb trap trace");
}

/*
 *	Dump TLB contents.
 */
void
db_dump_tlb_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *m)
{
	int tlbno, last, check, pid;
	struct tlb_entry tlb, tlbp;
	struct cpu_info *ci = curcpu();
char *attr[] = {
	"WTNA", "WTA ", "UCBL", "CWB ", "RES ", "RES ", "UCNB", "BPAS"
};

	pid = -1;

	if (m[0] == 'p') {
		if (have_addr && addr < 256) {
			pid = addr;
			tlbno = 0;
			count = ci->ci_hw.tlbsize;
		}
	} else if (m[0] == 'c') {
		last = ci->ci_hw.tlbsize;
		for (tlbno = 0; tlbno < last; tlbno++) {
			tlb_read(tlbno, &tlb);
			for (check = tlbno + 1; check < last; check++) {
				tlb_read(check, &tlbp);
if ((tlbp.tlb_hi == tlb.tlb_hi && (tlb.tlb_lo0 & PG_V || tlb.tlb_lo1 & PG_V)) ||
(pfn_to_pad(tlb.tlb_lo0) == pfn_to_pad(tlbp.tlb_lo0) && tlb.tlb_lo0 & PG_V) ||
(pfn_to_pad(tlb.tlb_lo1) == pfn_to_pad(tlbp.tlb_lo1) && tlb.tlb_lo1 & PG_V)) {
					printf("MATCH:\n");
					db_dump_tlb_cmd(tlbno, 1, 1, "");
					db_dump_tlb_cmd(check, 1, 1, "");
				}
			}
		}
		return;
	} else {
		if (have_addr && addr < ci->ci_hw.tlbsize) {
			tlbno = addr;
		} else {
			tlbno = 0;
			count = ci->ci_hw.tlbsize;
		}
	}
	last = tlbno + count;

	for (; tlbno < ci->ci_hw.tlbsize && tlbno < last; tlbno++) {
		tlb_read(tlbno, &tlb);

		if (pid >= 0 && (tlb.tlb_hi & 0xff) != pid)
			continue;

		if (tlb.tlb_lo0 & PG_V || tlb.tlb_lo1 & PG_V) {
			printf("%2d v=%16llx", tlbno, tlb.tlb_hi & ~0xffL);
			printf("/%02x ", tlb.tlb_hi & 0xff);

			if (tlb.tlb_lo0 & PG_V) {
				printf("%16llx ", pfn_to_pad(tlb.tlb_lo0));
				printf("%c", tlb.tlb_lo0 & PG_M ? 'M' : ' ');
				printf("%c", tlb.tlb_lo0 & PG_G ? 'G' : ' ');
				printf("%s ", attr[(tlb.tlb_lo0 >> 3) & 7]);
			} else {
				printf("invalid             ");
			}

			if (tlb.tlb_lo1 & PG_V) {
				printf("%16llx ", pfn_to_pad(tlb.tlb_lo1));
				printf("%c", tlb.tlb_lo1 & PG_M ? 'M' : ' ');
				printf("%c", tlb.tlb_lo1 & PG_G ? 'G' : ' ');
				printf("%s ", attr[(tlb.tlb_lo1 >> 3) & 7]);
			} else {
				printf("invalid             ");
			}
			printf(" sz=%x", tlb.tlb_mask);
		}
		else if (pid < 0) {
			printf("%2d v=invalid    ", tlbno);
		}
		printf("\n");
	}
}


struct db_command mips_db_command_table[] = {
	{ "tlb",	db_dump_tlb_cmd,	0,	NULL },
	{ "trap",	db_trap_trace_cmd,	0,	NULL },
	{ NULL,		NULL,			0,	NULL }
};

void
db_machine_init()
{
extern char *ssym;
	db_machine_commands_install(mips_db_command_table);
	if (ssym != NULL) {
		ddb_init();	/* Init symbols */
	}
}
