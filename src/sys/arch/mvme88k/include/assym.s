#ifndef __GENASSYM_INCLUDED
#define __GENASSYM_INCLUDED 1

#ifdef ASSEMBLER
#define NEWLINE \\ 
#endif
#define	P_FORW 0
#define	P_BACK 4
#define	P_VMSPACE 32
#define	P_ADDR 240
#define	P_PRIORITY 208
#define	P_STAT 44
#define	P_WCHAN 96
#define	SRUN 2
#define	VM_PMAP 132
#define	V_INTR 12
#define	UPAGES 3
#define	PGSHIFT 12
#define	U_PROF 824
#define	U_PROFSCALE 836
#define	PCB_ONFAULT 328
#define	SIZEOF_PCB 332
#define	SYS_exit 1
#define	SYS_execve 59
#define	SYS_sigreturn 103
#define EF_R0 0
#define EF_R31 31
#define EF_FPSR 32
#define EF_FPCR 33
#define EF_EPSR 34
#define EF_SXIP 35
#define EF_SFIP 37
#define EF_SNIP 36
#define EF_SSBR 38
#define EF_DMT0 39
#define EF_DMD0 40
#define EF_DMA0 41
#define EF_DMT1 42
#define EF_DMD1 43
#define EF_DMA1 44
#define EF_DMT2 45
#define EF_DMD2 46
#define EF_DMA2 47
#define EF_FPECR 48
#define EF_FPHS1 49
#define EF_FPLS1 50
#define EF_FPHS2 51
#define EF_FPLS2 52
#define EF_FPPT 53
#define EF_FPRH 54
#define EF_FPRL 55
#define EF_FPIT 56
#define EF_VECTOR 57
#define EF_MASK 58
#define EF_MODE 59
#define EF_RET 60
#define EF_NREGS 62
#define SIZEOF_EF 248

#endif /* __GENASSYM_INCLUDED */
