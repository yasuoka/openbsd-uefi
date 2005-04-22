/*	$OpenBSD: dnkbdmap.c,v 1.1 2005/04/22 11:59:11 miod Exp $	*/
/*
 * Copyright (c) 2005, Miodrag Vallat
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>

#include <hp300/dev/dnkbdmap.h>

#ifdef WSDISPLAY_COMPAT_RAWKBD
#include <dev/wscons/wskbdraw.h>

/*
 * Translate Domain keycodes to US keyboard XT scancodes, for proper
 * X11-over-wsmux operation.
 */
const u_int8_t dnkbd_raw[0x80] = {
	RAWKEY_Null,
	RAWKEY_Null,		/* 01 Ins Mark */
	RAWKEY_Null,		/* 02 Line Del */
	RAWKEY_Null,		/* 03 Char Del */
	RAWKEY_f10,		/* 04 f0 */
	RAWKEY_f1,
	RAWKEY_f2,
	RAWKEY_f3,
	RAWKEY_f4,
	RAWKEY_f5,
	RAWKEY_f6,
	RAWKEY_f7,
	RAWKEY_f8,
	RAWKEY_f9,
	RAWKEY_Null,		/* 0e Again */
	RAWKEY_Null,		/* 0f Read */
	RAWKEY_Null,		/* 10 Save Edit */
	RAWKEY_Null,		/* 11 Abort Exit */
	RAWKEY_Hold_Screen,	/* 12 Help Hold */
	RAWKEY_Null,		/* 13 Cut Copy */
	RAWKEY_Null,		/* 14 Undo Paste */
	RAWKEY_Null,		/* 15 Move Grow */
	RAWKEY_Null,
	RAWKEY_Escape,
	RAWKEY_1,
	RAWKEY_2,
	RAWKEY_3,
	RAWKEY_4,
	RAWKEY_5,
	RAWKEY_6,
	RAWKEY_7,
	RAWKEY_8,
	RAWKEY_9,
	RAWKEY_0,
	RAWKEY_minus,
	RAWKEY_equal,
	RAWKEY_grave,
	RAWKEY_BackSpace,
	RAWKEY_Null,
	RAWKEY_Null,		/* 27 Left Edge */
	RAWKEY_Null,		/* 28 Shell Cmd */
	RAWKEY_Null,		/* 29 Right Edge */
	RAWKEY_Null,
	RAWKEY_Null,
	RAWKEY_Tab,
	RAWKEY_q,
	RAWKEY_w,
	RAWKEY_e,
	RAWKEY_r,
	RAWKEY_t,
	RAWKEY_y,
	RAWKEY_u,
	RAWKEY_i,
	RAWKEY_o,
	RAWKEY_p,
	RAWKEY_bracketleft,
	RAWKEY_bracketright,
	RAWKEY_Null,
	RAWKEY_Delete,
	RAWKEY_Null,
	RAWKEY_KP_Home,
	RAWKEY_KP_Up,
	RAWKEY_KP_Prior,
	RAWKEY_KP_Add,
	RAWKEY_Null,		/* 40 Left Box */
	RAWKEY_Up,
	RAWKEY_Null,		/* 42 Right Box */
	RAWKEY_Control_L,
	RAWKEY_Null,
	RAWKEY_Null,
	RAWKEY_a,
	RAWKEY_s,
	RAWKEY_d,
	RAWKEY_f,
	RAWKEY_g,
	RAWKEY_h,
	RAWKEY_j,
	RAWKEY_k,
	RAWKEY_l,
	RAWKEY_semicolon,
	RAWKEY_apostrophe,
	RAWKEY_Null,
	RAWKEY_Return,
	RAWKEY_backslash,
	RAWKEY_Null,
	RAWKEY_KP_Left,
	RAWKEY_KP_Begin,
	RAWKEY_KP_Right,
	RAWKEY_KP_Subtract,
	RAWKEY_Left,
	RAWKEY_Null,		/* 5a Next Wndw */
	RAWKEY_Right,
	RAWKEY_Null,
	RAWKEY_Null,		/* 5d Rept */
	RAWKEY_Shift_L,
	RAWKEY_Null,
	RAWKEY_z,
	RAWKEY_x,
	RAWKEY_c,
	RAWKEY_v,
	RAWKEY_b,
	RAWKEY_n,
	RAWKEY_m,
	RAWKEY_comma,
	RAWKEY_period,
	RAWKEY_slash,
	RAWKEY_Shift_R,
	RAWKEY_Null,
	RAWKEY_Null,		/* 6c Pop */
	RAWKEY_Null,
	RAWKEY_KP_End,
	RAWKEY_KP_Down,
	RAWKEY_KP_Next,
	RAWKEY_Null,
	RAWKEY_Null,		/* 72 Top Box */
	RAWKEY_Down,
	RAWKEY_Null,		/* 74 Bottom Box */
	RAWKEY_Alt_L,
	RAWKEY_space,
	RAWKEY_Alt_R,
	RAWKEY_Null,
	RAWKEY_KP_Insert,
	RAWKEY_Null,
	RAWKEY_KP_Delete,
	RAWKEY_KP_Enter,
	RAWKEY_Null,
	RAWKEY_Caps_Lock,
	RAWKEY_Null
};
#endif

#define	KC(n)	KS_KEYCODE(n)

const keysym_t dnkbd_keydesc_us[] = {
/*	pos		command		normal		shifted */
	/* 01 Ins Mark */
	/* 02 Line Del */
	/* 03 Char Del */
	KC(0x04),			KS_f10,
	KC(0x05),			KS_f1,
	KC(0x06),			KS_f2,
	KC(0x07),			KS_f3,
	KC(0x08),			KS_f4,
	KC(0x09),			KS_f5,
	KC(0x0a),			KS_f6,
	KC(0x0b),			KS_f7,
	KC(0x0c),			KS_f8,
	KC(0x0d),			KS_f9,
	/* 0e Again */
	/* 0f Read */
	/* 10 Save Edit */
	/* 11 Abort Exit */
	KC(0x12),			KS_Hold_Screen,
	/* 13 Cut Copy */
	/* 14 Undo Paste */
	/* 15 Move Grow */
	KC(0x17),  KS_Cmd_Debugger,	KS_Escape,
	KC(0x18),			KS_1,		KS_exclam,
	KC(0x19),			KS_2,		KS_at,
	KC(0x1a),			KS_3,		KS_numbersign,
	KC(0x1b),			KS_4,		KS_dollar,
	KC(0x1c),			KS_5,		KS_percent,
	KC(0x1d),			KS_6,		KS_asciicircum,
	KC(0x1e),			KS_7,		KS_ampersand,
	KC(0x1f),			KS_8,		KS_asterisk,
	KC(0x20),			KS_9,		KS_parenleft,
	KC(0x21),			KS_0,		KS_parenright,
	KC(0x22),			KS_minus,	KS_underscore,
	KC(0x23),			KS_equal,	KS_plus,
	KC(0x24),			KS_grave,	KS_asciitilde,
	KC(0x25),  KS_Cmd_ResetEmul,	KS_Delete,	/* backspace */
	KC(0x27),			KS_Home,
	/* 28 Shell Cmd */
	KC(0x29),			KS_End,
	KC(0x2c),			KS_Tab,
	KC(0x2d),			KS_q,
	KC(0x2e),			KS_w,
	KC(0x2f),			KS_e,
	KC(0x30),			KS_r,
	KC(0x31),			KS_t,
	KC(0x32),			KS_y,
	KC(0x33),			KS_u,
	KC(0x34),			KS_i,
	KC(0x35),			KS_o,
	KC(0x36),			KS_p,
	KC(0x37),			KS_bracketleft,	KS_braceleft,
	KC(0x38),			KS_bracketright,KS_braceright,
	KC(0x3a),			KS_Delete,
	KC(0x3c),			KS_KP_7,
	KC(0x3d),			KS_KP_8,
	KC(0x3e),			KS_KP_9,
	KC(0x3f),			KS_KP_Add,
	/* 40 Left Box */
	KC(0x41),			KS_Up,
	/* 42 Right Box */
	KC(0x43),  KS_Cmd1,		KS_Control_L,
	KC(0x46),			KS_a,
	KC(0x47),			KS_s,
	KC(0x48),			KS_d,
	KC(0x49),			KS_f,
	KC(0x4a),			KS_g,
	KC(0x4b),			KS_h,
	KC(0x4c),			KS_j,
	KC(0x4d),			KS_k,
	KC(0x4e),			KS_l,
	KC(0x4f),			KS_semicolon,	KS_colon,
	KC(0x50),			KS_apostrophe,	KS_quotedbl,
	KC(0x52),			KS_Return,
	KC(0x53),			KS_backslash,	KS_bar,
	KC(0x55),			KS_KP_4,
	KC(0x56),			KS_KP_5,
	KC(0x57),			KS_KP_6,
	KC(0x58),			KS_KP_Subtract,
	KC(0x59),			KS_Left,
	/* 5a Next Wndw */
	KC(0x5b),			KS_Right,
	/* 5d Rept */
	KC(0x5e),			KS_Shift_L,
	KC(0x60),			KS_z,
	KC(0x61),			KS_x,
	KC(0x62),			KS_c,
	KC(0x63),			KS_v,
	KC(0x64),			KS_b,
	KC(0x65),			KS_n,
	KC(0x66),			KS_m,
	KC(0x67),			KS_comma,	KS_less,
	KC(0x68),			KS_period,	KS_greater,
	KC(0x69),			KS_slash,	KS_question,
	KC(0x6a),			KS_Shift_R,
	/* 6c Pop */
	KC(0x6e),			KS_KP_1,
	KC(0x6f),			KS_KP_2,
	KC(0x70),			KS_KP_3,
	/* 72 Top Box */
	KC(0x73),			KS_Down,
	/* 74 Bottom Box */
	KC(0x75),  KS_Cmd2,		KS_Alt_L,
	KC(0x76),			KS_space,
	KC(0x77),  KS_Cmd2,		KS_Alt_R,
	KC(0x79),			KS_KP_0,
	KC(0x7b),			KS_KP_Separator,
	KC(0x7c),			KS_KP_Enter,
	KC(0x7e),			KS_Caps_Lock
};

#define	KBD_MAP(name, base, map) \
	{ name, base, sizeof(map) / sizeof(keysym_t), map }

const struct wscons_keydesc dnkbd_keydesctab[] = {
	KBD_MAP(KB_US,			0,	dnkbd_keydesc_us),
	{ 0, 0, 0, 0 }
};
