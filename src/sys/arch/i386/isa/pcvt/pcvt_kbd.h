/*	$OpenBSD: pcvt_kbd.h,v 1.3 1996/05/07 07:22:31 deraadt Exp $	*/

/*
 * Copyright (c) 1992, 1995 Hellmuth Michaelis and Joerg Wunsch.
 *
 * Copyright (c) 1992, 1993 Brian Dunford-Shore and Holger Veit.
 *
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
 *
 * This code is derived from software contributed to 386BSD by
 * Holger Veit.
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
 *	This product includes software developed by Hellmuth Michaelis,
 *	Brian Dunford-Shore and Joerg Wunsch.
 * 4. The name authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * @(#)pcvt_kbd.h, 3.32, Last Edit-Date: [Tue Oct  3 11:19:48 1995]
 *
 */

/*---------------------------------------------------------------------------*
 *
 *	pcvt_kbd.h	VT220 Driver Keyboard Interface Header
 *	------------------------------------------------------
 *	-hm	split off from pcvt_kbd.c
 *	-hm	patch from Lon Willett to fix mapping of Control-R scancode
 *	-hm	---------------- Release 3.30 -----------------------
 *	-hm	---------------- Release 3.32 -----------------------
 *
 *---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*
 *	this is one sub-entry for the table. the type can be either
 *	"pointer to a string" or "pointer to a function"
 *---------------------------------------------------------------------------*/
typedef struct
{
	u_char subtype;			/* subtype, string or function */
#ifdef PCVT_ALT_ENH
	u_short str_leng;		/* if string, stringlength */
#endif
	union what
	{
		u_char *string;		/* ptr to string, null terminated */
		void (*func)(void);	/* ptr to function */
	} what;
} entry;

/*---------------------------------------------------------------------------*
 *	this is the "outer" table
 *---------------------------------------------------------------------------*/
typedef struct
{
	u_short	type;			/* type of key */
	u_short	ovlindex;		/* -hv- index into overload table */
	entry 	unshift;		/* normal default codes/funcs */
	entry	shift;			/* shifted default codes/funcs */
	entry 	ctrl;			/* control default codes/funcs */
#ifdef PCVT_ALT_ENH
	entry 	alt;			/* normal default codes/funcs */
	entry	alt_shift;		/* shifted default codes/funcs */
	entry 	alt_ctrl;		/* control default codes/funcs */
	entry 	alt_ctrl_shift;		/* normal default codes/funcs */
#endif
} Keycap_def;

#define IDX0		0	/* default indexvalue into ovl table */

#define STR		KBD_SUBT_STR	/* subtype = ptr to string */
#define FNC		KBD_SUBT_FNC	/* subtype = ptr to function */

#define CODE_SIZE	5

/*---------------------------------------------------------------------------*
 * the overlaytable table is a static fixed size scratchpad where all the
 * overloaded definitions are stored.
 * an entry consists of a short (holding the new type attribute) and
 * four entries for a new keydefinition.
 *---------------------------------------------------------------------------*/

#define OVLTBL_SIZE	64		/* 64 keys can be overloaded */

#define Ovl_tbl struct kbd_ovlkey

static Ovl_tbl *ovltbl;			/* the table itself */

static ovlinitflag = 0;			/* the init flag for the table */

/*
 * key codes >= 128 denote "virtual" shift/control
 * They are resolved before any keymapping is handled
 */

#if PCVT_SCANSET == 2
static u_char scantokey[] = {
/*      -0- -1- -2- -3- -4- -5- -6- -7-    This layout is valid for US only */
/*00*/   0,120,  0,116,114,112,113,123,  /* ??  F9  ??  F5  F3  F1  F2  F12 */
/*08*/   0,121,119,117,115, 16,  1,  0,  /* ??  F10 F8  F6  F4  TAB `   ??  */
/*10*/   0, 60, 44,  0, 58, 17,  2,  0,  /* ??  ALl SHl ??  CTl Q   1   ??  */
/*18*/   0,  0, 46, 32, 31, 18,  3,  0,  /* ??  Z   S   A   W   2   ??  ??  */
/*20*/   0, 48, 47, 33, 19,  5,  4,  0,  /* ??  C   X   D   E   4   3   ??  */
/*28*/   0, 61, 49, 34, 21, 20,  6,  0,  /* ??  SP  V   F   T   R   5   ??  */
/*30*/   0, 51, 50, 36, 35, 22,  7,  0,  /* ??  N   B   H   G   Y   6   ??  */
/*38*/   0,  0, 52, 37, 23,  8,  9,  0,  /* ??  ??  M   J   U   7   8   ??  */
/*40*/   0, 53, 38, 24, 25, 11, 10,  0,  /* ??  ,   K   I   O   0   9   ??  */
/*48*/   0, 54, 55, 39, 40, 26, 12,  0,  /* ??  .   /   L   ;   P   -   ??  */
/*50*/   0,  0, 41,  0, 27, 13,  0,  0,  /* ??  ??  "   ??  [   =   ??  ??  */
/*58*/  30, 57, 43, 28,  0, 29,  0,  0,  /* CAP SHr ENT ]   ??  \   ??  ??  */
/*60*/   0, 45,  0,  0,  0,  0, 15,  0,  /* ??  NL1 ??  ??  ??  ??  BS  ??  */
/*68*/   0, 93,  0, 92, 91,  0,  0,  0,  /* ??  KP1 ??  KP4 KP7 ??  ??  ??  */
/*70*/  99,104, 98, 97,102, 96,110, 90,  /* KP0 KP. KP2 KP5 KP6 KP8 ESC NUM */
/*78*/ 122,106,103,105,100,101,125,  0,  /* F11 KP+ KP3 KP- KP* KP9 LOC ??  */
/*80*/   0,  0,  0,118,127               /* ??  ??  ??  F7 SyRQ */
};

static u_char extscantokey[] = {
/*      -0- -1- -2- -3- -4- -5- -6- -7-    This layout is valid for US only */
/*00*/   0,120,  0,116,114,112,113,123,  /* ??  F9  ??  F5  F3  F1  F2  F12 */
/*08*/   0,121,119,117,115, 16,  1,  0,  /* ??  F10 F8  F6  F4  TAB `   ??  */
/*10*/   0, 62,128,  0, 64, 17,  2,  0,  /* ??  ALr vSh ??  CTr Q   1   ??  */
/*18*/   0,  0, 46, 32, 31, 18,  3,  0,  /* ??  Z   S   A   W   2   ??  ??  */
/*20*/   0, 48, 47, 33, 19,  5,  4,  0,  /* ??  C   X   D   E   4   3   ??  */
/*28*/   0, 61, 49, 34, 21, 20,  6,  0,  /* ??  SP  V   F   T   R   5   ??  */
/*30*/   0, 51, 50, 36, 35, 22,  7,  0,  /* ??  N   B   H   G   Y   6   ??  */
/*38*/   0,  0, 52, 37, 23,  8,  9,  0,  /* ??  ??  M   J   U   7   8   ??  */
/*40*/   0, 53, 38, 24, 25, 11, 10,  0,  /* ??  ,   K   I   O   0   9   ??  */
/*48*/   0, 54, 95, 39, 40, 26, 12,  0,  /* ??  .   KP/ L   ;   P   -   ??  */
/*50*/   0,  0, 41,  0, 27, 13,  0,  0,  /* ??  ??  "   ??  [   =   ??  ??  */
/*58*/  30, 57,108, 28,  0, 29,  0,  0,  /* CAP  SHr KPE ]   ??  \  ??  ??  */
/*60*/   0, 45,  0,  0,  0,  0, 15,  0,  /* ??  NL1 ??  ??  ??  ??  BS  ??  */
/*68*/   0, 81,  0, 79, 80,  0,  0,  0,  /* ??  END ??  LA  HOM ??  ??  ??  */
/*70*/  75, 76, 84, 97, 89, 83,110, 90,  /* INS DEL DA  KP5 RA  UA  ESC NUM */
/*78*/ 122,106, 86,105,124, 85,126,  0,  /* F11 KP+ PD  KP- PSc PU  Brk ??  */
/*80*/   0,  0,  0,118,127               /* ??  ??  ??  F7 SysRq */
};

#else	/* PCVT_SCANSET != 2 */

static u_char scantokey[] = {
/*       -0- -1- -2- -3- -4- -5- -6- -7-    This layout is valid for US only */
/*00*/    0,110,  2,  3,  4,  5,  6,  7,  /* ??  ESC 1   2   3   4   5   6   */
/*08*/    8,  9, 10, 11, 12, 13, 15, 16,  /* 7   8   9   0   -   =   BS  TAB */
/*10*/   17, 18, 19, 20, 21, 22, 23, 24,  /* Q   W   E   R   T   Y   U   I   */
/*18*/   25, 26, 27, 28, 43, 58, 31, 32,  /* O   P   [   ]   ENT CTl A   S   */
/*20*/   33, 34, 35, 36, 37, 38, 39, 40,  /* D   F   G   H   J   K   L   ;   */
/*28*/   41,  1, 44, 29, 46, 47, 48, 49,  /* '   `   SHl \   Z   X   C   V   */
/*30*/   50, 51, 52, 53, 54, 55, 57,100,  /* B   N   M   ,   .   /   SHr KP* */
/*38*/   60, 61, 30,112,113,114,115,116,  /* ALl SP  CAP F1  F2  F3  F4  F5  */
/*40*/  117,118,119,120,121, 90,125, 91,  /* F6  F7  F8  F9  F10 NUM LOC KP7 */
/*48*/   96,101,105, 92, 97,102,106, 93,  /* KP8 KP9 KP- KP4 KP5 KP6 KP+ KP1 */
/*50*/   98,103, 99,104,127,  0, 45,122,  /* KP2 KP3 KP0 KP. SyRq??  NL1 F11 */
/*58*/  123                               /* F12 */
};

static u_char extscantokey[] = {
/*       -0- -1- -2- -3- -4- -5- -6- -7-    This layout is valid for US only */
/*00*/    0,110,  2,  3,  4,  5,  6,  7,  /* ??  ESC 1   2   3   4   5   6   */
/*08*/    8,  9, 10, 11, 12, 13, 15, 16,  /* 7   8   9   0   -   =   BS  TAB */
/*10*/   17, 18, 19, 20, 21, 22, 23, 24,  /* Q   W   E   R   T   Y   U   I   */
/*18*/   25, 26, 27, 28,108, 64, 31, 32,  /* O   P   [   ]   KPE CTr A   S   */
/*20*/   33, 34, 35, 36, 37, 38, 39, 40,  /* D   F   G   H   J   K   L   ;   */
/*28*/   41,  1,128, 29, 46, 47, 48, 49,  /* '   `   vSh \   Z   X   C   V   */
/*30*/   50, 51, 52, 53, 54, 95, 57,124,  /* B   N   M   ,   .   KP/ SHr KP* */
/*38*/   62, 61, 30,112,113,114,115,116,  /* ALr SP  CAP F1  F2  F3  F4  F5  */
/*40*/  117,118,119,120,121, 90,126, 80,  /* F6  F7  F8  F9  F10 NUM Brk HOM */
/*48*/   83, 85,105, 79, 97, 89,106, 81,  /* UA  PU  KP- LA  KP5 RA  KP+ END */
/*50*/   84, 86, 75, 76,  0,  0, 45,122,  /* DA  PD  INS DEL ??  ??  NL1 F11 */
/*58*/  123,                              /* F12 */
};
#endif	/* PCVT_SCANSET == 2 */

static Keycap_def	key2ascii[] =
{

#ifdef PCVT_ALT_ENH

#define C (u_char *)
#define U (u_short)
#define V (void *)
#define S STR
#define F FNC
#define I IDX0

#define DFAULT  {S, 0,  { "" }}

/* DONT EVER OVERLOAD KEY 0, THIS IS A KEY THAT MUSTN'T EXIST */

/*      type   index  unshift            shift              ctrl               alt                alt_shift          alt_ctrl           alt_ctrl_shift     */
/*      -------------------------------------------------------------------------------------------------------------------------------------------------- */
/*  0*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*  1*/ { KBD_ASCII, I, {S,1, { "`" }},       {S,1, { "~" }},       {S,1, { "`" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*  2*/ { KBD_ASCII, I, {S,1, { "1" }},       {S,1, { "!" }},       {S,1, { "1" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*  3*/ { KBD_ASCII, I, {S,1, { "2" }},       {S,1, { "@" }},       {S,1, { "\000" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*  4*/ { KBD_ASCII, I, {S,1, { "3" }},       {S,1, { "#" }},       {S,1, { "3" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*  5*/ { KBD_ASCII, I, {S,1, { "4" }},       {S,1, { "$" }},       {S,1, { "4" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*  6*/ { KBD_ASCII, I, {S,1, { "5" }},       {S,1, { "%" }},       {S,1, { "5" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*  7*/ { KBD_ASCII, I, {S,1, { "6" }},       {S,1, { "^" }},       {S,1, { "\036" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*  8*/ { KBD_ASCII, I, {S,1, { "7" }},       {S,1, { "&" }},       {S,1, { "7" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*  9*/ { KBD_ASCII, I, {S,1, { "8" }},       {S,1, { "*" }},       {S,1, { "8" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 10*/ { KBD_ASCII, I, {S,1, { "9" }},       {S,1, { "(" }},       {S,1, { "9" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 11*/ { KBD_ASCII, I, {S,1, { "0" }},       {S,1, { ")" }},       {S,1, { "0" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 12*/ { KBD_ASCII, I, {S,1, { "-" }},       {S,1, { "_" }},       {S,1, { "\037" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 13*/ { KBD_ASCII, I, {S,1, { "=" }},       {S,1, { "+" }},       {S,1, { "=" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 14*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 15*/ { KBD_ASCII, I, {S,1, { "\177" }},    {S,1, { "\010" }},    {S,1, { "\177" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 16*/ { KBD_ASCII, I, {S,1, { "\t" }},      {S,1, { "\t" }},      {S,1, { "\t" }},      DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 17*/ { KBD_ASCII, I, {S,1, { "q" }},       {S,1, { "Q" }},       {S,1, { "\021" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 18*/ { KBD_ASCII, I, {S,1, { "w" }},       {S,1, { "W" }},       {S,1, { "\027" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 19*/ { KBD_ASCII, I, {S,1, { "e" }},       {S,1, { "E" }},       {S,1, { "\005" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 20*/ { KBD_ASCII, I, {S,1, { "r" }},       {S,1, { "R" }},       {S,1, { "\022" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 21*/ { KBD_ASCII, I, {S,1, { "t" }},       {S,1, { "T" }},       {S,1, { "\024" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 22*/ { KBD_ASCII, I, {S,1, { "y" }},       {S,1, { "Y" }},       {S,1, { "\031" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 23*/ { KBD_ASCII, I, {S,1, { "u" }},       {S,1, { "U" }},       {S,1, { "\025" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 24*/ { KBD_ASCII, I, {S,1, { "i" }},       {S,1, { "I" }},       {S,1, { "\011" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 25*/ { KBD_ASCII, I, {S,1, { "o" }},       {S,1, { "O" }},       {S,1, { "\017" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 26*/ { KBD_ASCII, I, {S,1, { "p" }},       {S,1, { "P" }},       {S,1, { "\020" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 27*/ { KBD_ASCII, I, {S,1, { "[" }},       {S,1, { "{" }},       {S,1, { "\033" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 28*/ { KBD_ASCII, I, {S,1, { "]" }},       {S,1, { "}" }},       {S,1, { "\035" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 29*/ { KBD_ASCII, I, {S,1, { "\\" }},      {S,1, { "|" }},       {S,1, { "\034" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 30*/ { KBD_CAPS,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 31*/ { KBD_ASCII, I, {S,1, { "a" }},       {S,1, { "A" }},       {S,1, { "\001" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 32*/ { KBD_ASCII, I, {S,1, { "s" }},       {S,1, { "S" }},       {S,1, { "\023" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 33*/ { KBD_ASCII, I, {S,1, { "d" }},       {S,1, { "D" }},       {S,1, { "\004" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 34*/ { KBD_ASCII, I, {S,1, { "f" }},       {S,1, { "F" }},       {S,1, { "\006" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 35*/ { KBD_ASCII, I, {S,1, { "g" }},       {S,1, { "G" }},       {S,1, { "\007" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 36*/ { KBD_ASCII, I, {S,1, { "h" }},       {S,1, { "H" }},       {S,1, { "\010" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 37*/ { KBD_ASCII, I, {S,1, { "j" }},       {S,1, { "J" }},       {S,1, { "\n" }},      DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 38*/ { KBD_ASCII, I, {S,1, { "k" }},       {S,1, { "K" }},       {S,1, { "\013" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 39*/ { KBD_ASCII, I, {S,1, { "l" }},       {S,1, { "L" }},       {S,1, { "\014" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 40*/ { KBD_ASCII, I, {S,1, { ";" }},       {S,1, { ":" }},       {S,1, { ";" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 41*/ { KBD_ASCII, I, {S,1, { "'" }},       {S,1, { "\" }"},      {S,1, { "'" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 42*/ { KBD_ASCII, I, {S,1, { "\\" }},      {S,1, { "|" }},       {S,1, { "\034" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 43*/ { KBD_RETURN,I, {S,1, { "\r" }},      {S,1, { "\r" }},      {S,1, { "\r" }},      DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 44*/ { KBD_SHIFT, I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 45*/ { KBD_ASCII, I, {S,1, { "<" }},       {S,1, { ">" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 46*/ { KBD_ASCII, I, {S,1, { "z" }},       {S,1, { "Z" }},       {S,1, { "\032" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 47*/ { KBD_ASCII, I, {S,1, { "x" }},       {S,1, { "X" }},       {S,1, { "\030" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 48*/ { KBD_ASCII, I, {S,1, { "c" }},       {S,1, { "C" }},       {S,1, { "\003" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 49*/ { KBD_ASCII, I, {S,1, { "v" }},       {S,1, { "V" }},       {S,1, { "\026" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 50*/ { KBD_ASCII, I, {S,1, { "b" }},       {S,1, { "B" }},       {S,1, { "\002" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 51*/ { KBD_ASCII, I, {S,1, { "n" }},       {S,1, { "N" }},       {S,1, { "\016" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 52*/ { KBD_ASCII, I, {S,1, { "m" }},       {S,1, { "M" }},       {S,1, { "\r" }},      DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 53*/ { KBD_ASCII, I, {S,1, { "," }},       {S,1, { "<" }},       {S,1, { "," }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 54*/ { KBD_ASCII, I, {S,1, { "." }},       {S,1, { ">" }},       {S,1, { "." }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 55*/ { KBD_ASCII, I, {S,1, { "/" }},       {S,1, { "?" }},       {S,1, { "/" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 56*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 57*/ { KBD_SHIFT, I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 58*/ { KBD_CTL,   I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 59*/ { KBD_ASCII, I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 60*/ { KBD_META,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
#if !PCVT_NULLCHARS
/* 61*/ { KBD_ASCII, I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
#else
/* 61*/ { KBD_ASCII, I, DFAULT,            DFAULT,            {S,1, { "\000" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
#endif /* PCVT_NULLCHARS */
/* 62*/ { KBD_META,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 63*/ { KBD_ASCII, I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 64*/ { KBD_CTL,   I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 65*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 66*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 67*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 68*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 69*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 70*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 71*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 72*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 73*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 74*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 75*/ { KBD_FUNC,  I, {S,4, { "\033[2~" }}, {S,4, { "\033[2~" }}, {S,4, { "\033[2~" }}, DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 76*/ { KBD_FUNC,  I, {S,4, { "\033[3~" }}, {S,4, { "\033[3~" }}, {S,4, { "\033[3~" }}, DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 77*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 78*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 79*/ { KBD_CURSOR,I, {S,4, { "\033[D" }},  {S,4, { "\033OD" }},  {S,4, { "\033[D" }},  DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 80*/ { KBD_FUNC,  I, {S,4, { "\033[1~" }}, {S,4, { "\033[1~" }}, {S,4, { "\033[1~" }}, DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 81*/ { KBD_FUNC,  I, {S,4, { "\033[4~" }}, {S,4, { "\033[4~" }}, {S,4, { "\033[4~" }}, DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 82*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 83*/ { KBD_CURSOR,I, {S,4, { "\033[A" }},  {S,4, { "\033OA" }},  {S,4, { "\033[A" }},  DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 84*/ { KBD_CURSOR,I, {S,4, { "\033[B" }},  {S,4, { "\033OB" }},  {S,4, { "\033[B" }},  DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 85*/ { KBD_FUNC,  I, {S,4, { "\033[5~" }}, {S,4, { "\033[5~" }}, {S,4, { "\033[5~" }}, DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 86*/ { KBD_FUNC,  I, {S,4, { "\033[6~" }}, {S,4, { "\033[6~" }}, {S,4, { "\033[6~" }}, DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 87*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 88*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 89*/ { KBD_CURSOR,I, {S,3, { "\033[C" }},  {S,3, { "\033OC" }},  {S,3, { "\033[C" }},  DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 90*/ { KBD_NUM,   I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 91*/ { KBD_KP,    I, {S,1, { "7" }},       {S,2, { "\033Ow" }},  {S,1, { "7" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 92*/ { KBD_KP,    I, {S,1, { "4" }},       {S,2, { "\033Ot" }},  {S,1, { "4" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 93*/ { KBD_KP,    I, {S,1, { "1" }},       {S,2, { "\033Oq" }},  {S,1, { "1" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 94*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 95*/ { KBD_KP,    I, {S,1, { "/" }},       {S,1, { "/" }},       {S,1, { "/" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 96*/ { KBD_KP,    I, {S,1, { "8" }},       {S,2, { "\033Ox" }},  {S,1, { "8" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 97*/ { KBD_KP,    I, {S,1, { "5" }},       {S,2, { "\033Ou" }},  {S,1, { "5" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 98*/ { KBD_KP,    I, {S,1, { "2" }},       {S,2, { "\033Or" }},  {S,1, { "2" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/* 99*/ { KBD_KP,    I, {S,1, { "0" }},       {S,2, { "\033Op" }},  {S,1, { "0" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*100*/ { KBD_KP,    I, {S,1, { "*" }},       {S,1, { "*" }},       {S,1, { "*" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*101*/ { KBD_KP,    I, {S,1, { "9" }},       {S,2, { "\033Oy" }},  {S,1, { "9" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*102*/ { KBD_KP,    I, {S,1, { "6" }},       {S,2, { "\033Ov" }},  {S,1, { "6" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*103*/ { KBD_KP,    I, {S,1, { "3" }},       {S,2, { "\033Os" }},  {S,1, { "3" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*104*/ { KBD_KP,    I, {S,1, { "." }},       {S,2, { "\033On" }},  {S,1, { "." }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*105*/ { KBD_KP,    I, {S,1, { "-" }},       {S,2, { "\033Om" }},  {S,1, { "-" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*106*/ { KBD_KP,    I, {S,1, { "+" }},       {S,1, { "+" }},       {S,1, { "+" }},       DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*107*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*108*/ { KBD_RETURN,I, {S,1, { "\r" }},      {S,2, { "\033OM" }},  {S,1, { "\r" }},      DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*109*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*110*/ { KBD_ASCII, I, {S,1, { "\033" }},    {S,2, { "\033" }},    {S,1, { "\033" }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*111*/ { KBD_NONE,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*112*/ { KBD_FUNC,  I, {F,0, { C fkey1 }},     {F,0, { C sfkey1 }},    {F,0, { C cfkey1 }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*113*/ { KBD_FUNC,  I, {F,0, { C fkey2 }},     {F,0, { C sfkey2 }},    {F,0, { C cfkey2 }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*114*/ { KBD_FUNC,  I, {F,0, { C fkey3 }},     {F,0, { C sfkey3 }},    {F,0, { C cfkey3 }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*115*/ { KBD_FUNC,  I, {F,0, { C fkey4 }},     {F,0, { C sfkey4 }},    {F,0, { C cfkey4 }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*116*/ { KBD_FUNC,  I, {F,0, { C fkey5 }},     {F,0, { C sfkey5 }},    {F,0, { C cfkey5 }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*117*/ { KBD_FUNC,  I, {F,0, { C fkey6 }},     {F,0, { C sfkey6 }},    {F,0, { C cfkey6 }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*118*/ { KBD_FUNC,  I, {F,0, { C fkey7 }},     {F,0, { C sfkey7 }},    {F,0, { C cfkey7 }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*119*/ { KBD_FUNC,  I, {F,0, { C fkey8 }},     {F,0, { C sfkey8 }},    {F,0, { C cfkey8 }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*120*/ { KBD_FUNC,  I, {F,0, { C fkey9 }},     {F,0, { C sfkey9 }},    {F,0, { C cfkey9 }},    DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*121*/ { KBD_FUNC,  I, {F,0, { C fkey10 }},    {F,0, { C sfkey10 }},   {F,0, { C cfkey10 }},   DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*122*/ { KBD_FUNC,  I, {F,0, { C fkey11 }},    {F,0, { C sfkey11 }},   {F,0, { C cfkey11 }},   DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*123*/ { KBD_FUNC,  I, {F,0, { C fkey12 }},    {F,0, { C sfkey12 }},   {F,0, { C cfkey12 }},   DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*124*/ { KBD_KP,    I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*125*/ { KBD_SCROLL,I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*126*/ { KBD_BREAK, I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },
/*127*/ { KBD_FUNC,  I, DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT,            DFAULT },

#undef C
#undef U
#undef V
#undef S
#undef F
#undef I
#undef DFLT
};

#else /* PCVT_ALT_ENH */

/* define some shorthands to make the table (almost) fit into 80 columns */
#define C (u_char *)
#define V (void *)
#define S STR
#define F FNC
#define I IDX0

/* DONT EVER OVERLOAD KEY 0, THIS IS A KEY THAT MUSTN'T EXIST */

/*      type   index   unshift        shift           ctrl         */
/*      ---------------------------------------------------------- */
/*  0*/ { KBD_NONE,  I, {S, { "df" }},    {S, { "" }},      {S, { "" }} },
/*  1*/ { KBD_ASCII, I, {S, { "`" }},     {S, { "~" }},     {S, { "`" }} },
/*  2*/ { KBD_ASCII, I, {S, { "1" }},     {S, { "!" }},     {S, { "1" }} },
/*  3*/ { KBD_ASCII, I, {S, { "2" }},     {S, { "@" }},     {S, { "\000" }} },
/*  4*/ { KBD_ASCII, I, {S, { "3" }},     {S, { "#" }},     {S, { "3" }} },
/*  5*/ { KBD_ASCII, I, {S, { "4" }},     {S, { "$" }},     {S, { "4" }} },
/*  6*/ { KBD_ASCII, I, {S, { "5" }},     {S, { "%" }},     {S, { "5" }} },
/*  7*/ { KBD_ASCII, I, {S, { "6" }},     {S, { "^" }},     {S, { "\036" }} },
/*  8*/ { KBD_ASCII, I, {S, { "7" }},     {S, { "&" }},     {S, { "7" }} },
/*  9*/ { KBD_ASCII, I, {S, { "8" }},     {S, { "*" }},     {S, { "9" }} },
/* 10*/ { KBD_ASCII, I, {S, { "9" }},     {S, { "(" }},     {S, { "9" }} },
/* 11*/ { KBD_ASCII, I, {S, { "0" }},     {S, { ")" }},     {S, { "0" }} },
/* 12*/ { KBD_ASCII, I, {S, { "-" }},     {S, { "_" }},     {S, { "\037" }} },
/* 13*/ { KBD_ASCII, I, {S, { "=" }},     {S, { "+" }},     {S, { "=" }} },
/* 14*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 15*/ { KBD_ASCII, I, {S, { "\177" }},  {S, { "\010" }},  {S, { "\177" }} }, /* BS */
/* 16*/ { KBD_ASCII, I, {S, { "\t" }},    {S, { "\t" }},    {S, { "\t" }} },   /* TAB */
/* 17*/ { KBD_ASCII, I, {S, { "q" }},     {S, { "Q" }},     {S, { "\021" }} },
/* 18*/ { KBD_ASCII, I, {S, { "w" }},     {S, { "W" }},     {S, { "\027" }} },
/* 19*/ { KBD_ASCII, I, {S, { "e" }},     {S, { "E" }},     {S, { "\005" }} },
/* 20*/ { KBD_ASCII, I, {S, { "r" }},     {S, { "R" }},     {S, { "\022" }} },
/* 21*/ { KBD_ASCII, I, {S, { "t" }},     {S, { "T" }},     {S, { "\024" }} },
/* 22*/ { KBD_ASCII, I, {S, { "y" }},     {S, { "Y" }},     {S, { "\031" }} },
/* 23*/ { KBD_ASCII, I, {S, { "u" }},     {S, { "U" }},     {S, { "\025" }} },
/* 24*/ { KBD_ASCII, I, {S, { "i" }},     {S, { "I" }},     {S, { "\011" }} },
/* 25*/ { KBD_ASCII, I, {S, { "o" }},     {S, { "O" }},     {S, { "\017" }} },
/* 26*/ { KBD_ASCII, I, {S, { "p" }},     {S, { "P" }},     {S, { "\020" }} },
/* 27*/ { KBD_ASCII, I, {S, { "[" }},     {S, { "{" }},     {S, { "\033" }} },
/* 28*/ { KBD_ASCII, I, {S, { "]" }},     {S, { "}" }},     {S, { "\035" }} },
/* 29*/ { KBD_ASCII, I, {S, { "\\" }},    {S, { "|" }},     {S, { "\034" }} },
/* 30*/ { KBD_CAPS,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 31*/ { KBD_ASCII, I, {S, { "a" }},     {S, { "A" }},     {S, { "\001" }} },
/* 32*/ { KBD_ASCII, I, {S, { "s" }},     {S, { "S" }},     {S, { "\023" }} },
/* 33*/ { KBD_ASCII, I, {S, { "d" }},     {S, { "D" }},     {S, { "\004" }} },
/* 34*/ { KBD_ASCII, I, {S, { "f" }},     {S, { "F" }},     {S, { "\006" }} },
/* 35*/ { KBD_ASCII, I, {S, { "g" }},     {S, { "G" }},     {S, { "\007" }} },
/* 36*/ { KBD_ASCII, I, {S, { "h" }},     {S, { "H" }},     {S, { "\010" }} },
/* 37*/ { KBD_ASCII, I, {S, { "j" }},     {S, { "J" }},     {S, { "\n" }} },
/* 38*/ { KBD_ASCII, I, {S, { "k" }},     {S, { "K" }},     {S, { "\013" }} },
/* 39*/ { KBD_ASCII, I, {S, { "l" }},     {S, { "L" }},     {S, { "\014" }} },
/* 40*/ { KBD_ASCII, I, {S, { ";" }},     {S, { ":" }},     {S, { ";" }} },
/* 41*/ { KBD_ASCII, I, {S, { "'" }},     {S, { "\"" }},    {S, { "'" }} },
/* 42*/ { KBD_ASCII, I, {S, { "\\" }},    {S, { "|" }},     {S, { "\034" }} }, /* special */
/* 43*/ { KBD_RETURN,I, {S, { "\r" }},    {S, { "\r" }},    {S, { "\r" }} },    /* RETURN */
/* 44*/ { KBD_SHIFT, I, {S, { "" }},      {S, { "" }},      {S, { "" }} },  /* SHIFT left */
/* 45*/ { KBD_ASCII, I, {S, { "<" }},     {S, { ">" }},     {S, { "" }} },
/* 46*/ { KBD_ASCII, I, {S, { "z" }},     {S, { "Z" }},     {S, { "\032" }} },
/* 47*/ { KBD_ASCII, I, {S, { "x" }},     {S, { "X" }},     {S, { "\030" }} },
/* 48*/ { KBD_ASCII, I, {S, { "c" }},     {S, { "C" }},     {S, { "\003" }} },
/* 49*/ { KBD_ASCII, I, {S, { "v" }},     {S, { "V" }},     {S, { "\026" }} },
/* 50*/ { KBD_ASCII, I, {S, { "b" }},     {S, { "B" }},     {S, { "\002" }} },
/* 51*/ { KBD_ASCII, I, {S, { "n" }},     {S, { "N" }},     {S, { "\016" }} },
/* 52*/ { KBD_ASCII, I, {S, { "m" }},     {S, { "M" }},     {S, { "\r" }} },
/* 53*/ { KBD_ASCII, I, {S, { "," }},     {S, { "<" }},     {S, { "," }} },
/* 54*/ { KBD_ASCII, I, {S, { "." }},     {S, { ">" }},     {S, { "." }} },
/* 55*/ { KBD_ASCII, I, {S, { "/" }},     {S, { "?" }},     {S, { "/" }} },
/* 56*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 57*/ { KBD_SHIFT, I, {S, { "" }},      {S, { "" }},      {S, { "" }} }, /* SHIFT right */
/* 58*/ { KBD_CTL,   I, {S, { "" }},      {S, { "" }},      {S, { "" }} },    /* CTL left */
/* 59*/ { KBD_ASCII, I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 60*/ { KBD_META,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },    /* ALT left */
#if !PCVT_NULLCHARS
/* 61*/ { KBD_ASCII, I, {S, { " " }},     {S, { " " }},     {S, { " " }} },      /* SPACE */
#else
/* 61*/ { KBD_ASCII, I, {S, { " " }},     {S, { " " }},     {S, { "\000" }} },   /* SPACE */
#endif /* PCVT_NULLCHARS */
/* 62*/ { KBD_META,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },   /* ALT right */
/* 63*/ { KBD_ASCII, I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 64*/ { KBD_CTL,   I, {S, { "" }},      {S, { "" }},      {S, { "" }} },   /* CTL right */
/* 65*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 66*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 67*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 68*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 69*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 70*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 71*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 72*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 73*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 74*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 75*/ { KBD_FUNC,  I, {S, { "\033[2~" }},{S, { "\033[2~" }},{S, { "\033[2~" }} },/* INS */
/* 76*/ { KBD_FUNC,  I, {S, { "\033[3~" }},{S, { "\033[3~" }},{S, { "\033[3~" }} },/* DEL */
/* 77*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 78*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 79*/ { KBD_CURSOR,I, {S, { "\033[D" }},{S, { "\033OD" }},{S, { "\033[D" }} }, /* CU <- */
/* 80*/ { KBD_FUNC,  I, {S, { "\033[1~" }},{S, { "\033[1~" }},{S, { "\033[1~" }} },/* HOME = FIND*/
/* 81*/ { KBD_FUNC,  I, {S, { "\033[4~" }},{S, { "\033[4~" }},{S, { "\033[4~" }} },/* END = SELECT */
/* 82*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 83*/ { KBD_CURSOR,I, {S, { "\033[A" }},{S, { "\033OA" }},{S, { "\033[A" }} }, /* CU ^ */
/* 84*/ { KBD_CURSOR,I, {S, { "\033[B" }},{S, { "\033OB" }},{S, { "\033[B" }} }, /* CU v */
/* 85*/ { KBD_FUNC,  I, {S, { "\033[5~" }},{S, { "\033[5~" }},{S, { "\033[5~" }} },/*PG UP*/
/* 86*/ { KBD_FUNC,  I, {S, { "\033[6~" }},{S, { "\033[6~" }},{S, { "\033[6~" }} },/*PG DN*/
/* 87*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 88*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 89*/ { KBD_CURSOR,I, {S, { "\033[C" }},{S, { "\033OC" }},{S, { "\033[C" }} }, /* CU -> */
/* 90*/ { KBD_NUM,   I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 91*/ { KBD_KP,    I, {S, { "7" }},     {S, { "\033Ow" }},{S, { "7" }} },
/* 92*/ { KBD_KP,    I, {S, { "4" }},     {S, { "\033Ot" }},{S, { "4" }} },
/* 93*/ { KBD_KP,    I, {S, { "1" }},     {S, { "\033Oq" }},{S, { "1" }} },
/* 94*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/* 95*/ { KBD_KP,    I, {S, { "/" }},     {S, { "/" }},     {S, { "/" }} },
/* 96*/ { KBD_KP,    I, {S, { "8" }},     {S, { "\033Ox" }},{S, { "8" }} },
/* 97*/ { KBD_KP,    I, {S, { "5" }},     {S, { "\033Ou" }},{S, { "5" }} },
/* 98*/ { KBD_KP,    I, {S, { "2" }},     {S, { "\033Or" }},{S, { "2" }} },
/* 99*/ { KBD_KP,    I, {S, { "0" }},     {S, { "\033Op" }},{S, { "0" }} },
/*100*/ { KBD_KP,    I, {S, { "*" }},     {S, { "*" }},     {S, { "*" }} },
/*101*/ { KBD_KP,    I, {S, { "9" }},     {S, { "\033Oy" }},{S, { "9" }} },
/*102*/ { KBD_KP,    I, {S, { "6" }},     {S, { "\033Ov" }},{S, { "6" }} },
/*103*/ { KBD_KP,    I, {S, { "3" }},     {S, { "\033Os" }},{S, { "3" }} },
/*104*/ { KBD_KP,    I, {S, { "." }},     {S, { "\033On" }},{S, { "." }} },
/*105*/ { KBD_KP,    I, {S, { "-" }},     {S, { "\033Om" }},{S, { "-" }} },
/*106*/ { KBD_KP,    I, {S, { "+" }},     {S, { "+" }},     {S, { "+" }} },
/*107*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/*108*/ { KBD_RETURN,I, {S, { "\r" }},    {S, { "\033OM" }},{S, { "\r" }} },  /* KP ENTER */
/*109*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/*110*/ { KBD_ASCII, I, {S, { "\033" }},  {S, { "\033" }},  {S, { "\033" }} },
/*111*/ { KBD_NONE,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/*112*/ { KBD_FUNC,  I, {F, { C fkey1 }},   {F, { C sfkey1 }},  {F, { C cfkey1 }} },  /* F1 */
/*113*/ { KBD_FUNC,  I, {F, { C fkey2 }},   {F, { C sfkey2 }},  {F, { C cfkey2 }} },  /* F2 */
/*114*/ { KBD_FUNC,  I, {F, { C fkey3 }},   {F, { C sfkey3 }},  {F, { C cfkey3 }} },  /* F3 */
/*115*/ { KBD_FUNC,  I, {F, { C fkey4 }},   {F, { C sfkey4 }},  {F, { C cfkey4 }} },  /* F4 */
/*116*/ { KBD_FUNC,  I, {F, { C fkey5 }},   {F, { C sfkey5 }},  {F, { C cfkey5 }} },  /* F5 */
/*117*/ { KBD_FUNC,  I, {F, { C fkey6 }},   {F, { C sfkey6 }},  {F, { C cfkey6 }} },  /* F6 */
/*118*/ { KBD_FUNC,  I, {F, { C fkey7 }},   {F, { C sfkey7 }},  {F, { C cfkey7 }} },  /* F7 */
/*119*/ { KBD_FUNC,  I, {F, { C fkey8 }},   {F, { C sfkey8 }},  {F, { C cfkey8 }} },  /* F8 */
/*120*/ { KBD_FUNC,  I, {F, { C fkey9 }},   {F, { C sfkey9 }},  {F, { C cfkey9 }} },  /* F9 */
/*121*/ { KBD_FUNC,  I, {F, { C fkey10 }},  {F, { C sfkey10 }}, {F, { C cfkey10 }} }, /* F10 */
/*122*/ { KBD_FUNC,  I, {F, { C fkey11 }},  {F, { C sfkey11 }}, {F, { C cfkey11 }} }, /* F11 */
/*123*/ { KBD_FUNC,  I, {F, { C fkey12 }},  {F, { C sfkey12 }}, {F, { C cfkey12 }} }, /* F12 */
/*124*/ { KBD_KP,    I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/*125*/ { KBD_SCROLL,I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/*126*/ { KBD_BREAK, I, {S, { "" }},      {S, { "" }},      {S, { "" }} },
/*127*/ { KBD_FUNC,  I, {S, { "" }},      {S, { "" }},      {S, { "" }} },      /* SysRq */

#undef C
#undef V
#undef S
#undef F
#undef I
};

#endif /* PCVT_ALT_ENH */

static short	keypad2num[] = {
	7, 4, 1, -1, -1, 8, 5, 2, 0, -1, 9, 6, 3, -1, -1, -1, -1
};

#define N_KEYNUMS 128

/*
 * this is the reverse mapping from keynumbers to scanset 1 codes
 * it is used to emulate the SysV-style GIO_KEYMAP ioctl cmd
 */

static u_char key2scan1[N_KEYNUMS] = {
	   0,0x29,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09, /*   0 */
	0x0a,0x0b,0x0c,0x0d,   0,0x0e,0x0f,0x10,0x11,0x12, /*  10 */
	0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x2b, /*  20 */
	0x3a,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26, /*  30 */
	0x27,0x28,   0,0x1c,0x2a,0x56,0x2c,0x2d,0x2e,0x2f, /*  40 */
	0x30,0x31,0x32,0x33,0x34,0x35,0x56,0x36,0x1d,   0, /*  50 */
	0x38,0x39,   0,   0,   0,   0,   0,   0,   0,   0, /*  60 */
	   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, /*  70 */
	   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, /*  80 */
	0x45,0x47,0x4b,0x4f,   0,   0,0x48,0x4c,0x50,0x52, /*  90 */
	0x37,0x49,0x4d,0x51,0x53,0x4a,0x4e,   0,   0,   0, /* 100 */
	0x01,   0,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42, /* 110 */
	0x43,0x44,0x57,0x58,   0,0x46,   0,0x54		   /* 120 */
};

/*
 * SysV is brain-dead enough to stick on the IBM code page 437. So we
 * have to translate our keymapping into IBM 437 (possibly losing keys),
 * in order to have the X server convert it back into ISO8859.1
 */

/* NB: this table only contains the mapping for codes >= 128 */

static u_char iso2ibm437[] =
{
	   0,     0,     0,     0,     0,     0,     0,     0,
	   0,     0,     0,     0,     0,     0,     0,     0,
	   0,     0,     0,     0,     0,     0,     0,     0,
	   0,     0,     0,     0,     0,     0,     0,     0,
	0xff,  0xad,  0x9b,  0x9c,     0,  0x9d,     0,  0x40,
	0x6f,  0x63,  0x61,  0xae,     0,     0,     0,     0,
	0xf8,  0xf1,  0xfd,  0x33,     0,  0xe6,     0,  0xfa,
	   0,  0x31,  0x6f,  0xaf,  0xac,  0xab,     0,  0xa8,
	0x41,  0x41,  0x41,  0x41,  0x8e,  0x8f,  0x92,  0x80,
	0x45,  0x90,  0x45,  0x45,  0x49,  0x49,  0x49,  0x49,
	0x81,  0xa5,  0x4f,  0x4f,  0x4f,  0x4f,  0x99,  0x4f,
	0x4f,  0x55,  0x55,  0x55,  0x9a,  0x59,     0,  0xe1,
	0x85,  0xa0,  0x83,  0x61,  0x84,  0x86,  0x91,  0x87,
	0x8a,  0x82,  0x88,  0x89,  0x8d,  0xa1,  0x8c,  0x8b,
	   0,  0xa4,  0x95,  0xa2,  0x93,  0x6f,  0x94,  0x6f,
	0x6f,  0x97,  0xa3,  0x96,  0x81,  0x98,     0,     0
};

/* EOF */
