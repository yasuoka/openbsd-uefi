/*	$OpenBSD: wskbdmap_mfii.c,v 1.15 2002/04/04 21:24:33 mickey Exp $ */
/*	$NetBSD: wskbdmap_mfii.c,v 1.15 2000/05/19 16:40:04 drochner Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Juergen Hannken-Illjes.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>
#include <dev/pckbc/wskbdmap_mfii.h>

#define KC(n) KS_KEYCODE(n)

static const keysym_t pckbd_keydesc_us[] = {
/*  pos      command		normal		shifted */
    KC(1),   KS_Cmd_Debugger,	KS_Escape,
    KC(2),  			KS_1,		KS_exclam,
    KC(3),  			KS_2,		KS_at,
    KC(4),  			KS_3,		KS_numbersign,
    KC(5),  			KS_4,		KS_dollar,
    KC(6),  			KS_5,		KS_percent,
    KC(7),  			KS_6,		KS_asciicircum,
    KC(8),  			KS_7,		KS_ampersand,
    KC(9),  			KS_8,		KS_asterisk,
    KC(10), 			KS_9,		KS_parenleft,
    KC(11), 			KS_0,		KS_parenright,
    KC(12), 			KS_minus,	KS_underscore,
    KC(13), 			KS_equal,	KS_plus,
    KC(14),  KS_Cmd_ResetEmul,	KS_Delete,
    KC(15), 			KS_Tab,
    KC(16), 			KS_q,
    KC(17), 			KS_w,
    KC(18), 			KS_e,
    KC(19), 			KS_r,
    KC(20), 			KS_t,
    KC(21), 			KS_y,
    KC(22), 			KS_u,
    KC(23), 			KS_i,
    KC(24), 			KS_o,
    KC(25), 			KS_p,
    KC(26), 			KS_bracketleft,	KS_braceleft,
    KC(27), 			KS_bracketright, KS_braceright,
    KC(28), 			KS_Return,
    KC(29),  KS_Cmd1,		KS_Control_L,
    KC(30), 			KS_a,
    KC(31), 			KS_s,
    KC(32), 			KS_d,
    KC(33), 			KS_f,
    KC(34), 			KS_g,
    KC(35), 			KS_h,
    KC(36), 			KS_j,
    KC(37), 			KS_k,
    KC(38), 			KS_l,
    KC(39), 			KS_semicolon,	KS_colon,
    KC(40), 			KS_apostrophe,	KS_quotedbl,
    KC(41), 			KS_grave,	KS_asciitilde,
    KC(42), 			KS_Shift_L,
    KC(43), 			KS_backslash,	KS_bar,
    KC(44), 			KS_z,
    KC(45), 			KS_x,
    KC(46), 			KS_c,
    KC(47), 			KS_v,
    KC(48), 			KS_b,
    KC(49), 			KS_n,
    KC(50), 			KS_m,
    KC(51), 			KS_comma,	KS_less,
    KC(52), 			KS_period,	KS_greater,
    KC(53), 			KS_slash,	KS_question,
    KC(54), 			KS_Shift_R,
    KC(55), 			KS_KP_Multiply,
    KC(56),  KS_Cmd2,		KS_Alt_L,
    KC(57), 			KS_space,
    KC(58), 			KS_Caps_Lock,
    KC(59),  KS_Cmd_Screen0,	KS_f1,
    KC(60),  KS_Cmd_Screen1,	KS_f2,
    KC(61),  KS_Cmd_Screen2,	KS_f3,
    KC(62),  KS_Cmd_Screen3,	KS_f4,
    KC(63),  KS_Cmd_Screen4,	KS_f5,
    KC(64),  KS_Cmd_Screen5,	KS_f6,
    KC(65),  KS_Cmd_Screen6,	KS_f7,
    KC(66),  KS_Cmd_Screen7,	KS_f8,
    KC(67),  KS_Cmd_Screen8,	KS_f9,
    KC(68),  KS_Cmd_Screen9,	KS_f10,
    KC(69), 			KS_Num_Lock,
    KC(70), 			KS_Hold_Screen,
    KC(71), 			KS_KP_Home,	KS_KP_7,
    KC(72), 			KS_KP_Up,	KS_KP_8,
    KC(73), 			KS_KP_Prior,	KS_KP_9,
    KC(74), 			KS_KP_Subtract,
    KC(75), 			KS_KP_Left,	KS_KP_4,
    KC(76), 			KS_KP_Begin,	KS_KP_5,
    KC(77), 			KS_KP_Right,	KS_KP_6,
    KC(78), 			KS_KP_Add,
    KC(79), 			KS_KP_End,	KS_KP_1,
    KC(80), 			KS_KP_Down,	KS_KP_2,
    KC(81), 			KS_KP_Next,	KS_KP_3,
    KC(82), 			KS_KP_Insert,	KS_KP_0,
    KC(83), 			KS_KP_Delete,	KS_KP_Decimal,
    KC(87),  KS_Cmd_Screen10,	KS_f11,
    KC(88),  KS_Cmd_Screen11,	KS_f12,
    KC(127),			KS_Pause, /* Break */
    KC(156),			KS_KP_Enter,
    KC(157), KS_Cmd1,		KS_Control_R,
    KC(170),			KS_Print_Screen,
    KC(181),			KS_KP_Divide,
    KC(183),			KS_Print_Screen,
    KC(184), KS_Cmd2,		KS_Alt_R,	KS_Multi_key,
#if 0
    KC(198),  KS_Cmd_ResetClose, /* CTL-Break */
#endif
    KC(199),			KS_Home,
    KC(200),			KS_Up,
    KC(201), KS_Cmd_ScrollBack,	KS_Prior,
    KC(203),			KS_Left,
    KC(205),			KS_Right,
    KC(207),			KS_End,
    KC(208),			KS_Down,
    KC(209), KS_Cmd_ScrollFwd,	KS_Next,
    KC(210),			KS_Insert,
    KC(211), KS_Cmd_KbdReset,	KS_KP_Delete,
    KC(219),			KS_Meta_L,
    KC(220),			KS_Meta_R,
    KC(221),			KS_Menu,
};

static const keysym_t pckbd_keydesc_de[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(3),   KS_2,		KS_quotedbl,	KS_twosuperior,
    KC(4),   KS_3,		KS_section,	KS_threesuperior,
    KC(7),   KS_6,		KS_ampersand,
    KC(8),   KS_7,		KS_slash,	KS_braceleft,
    KC(9),   KS_8,		KS_parenleft,	KS_bracketleft,
    KC(10),  KS_9,		KS_parenright,	KS_bracketright,
    KC(11),  KS_0,		KS_equal,	KS_braceright,
    KC(12),  KS_ssharp,		KS_question,	KS_backslash,
    KC(13),  KS_dead_acute,	KS_dead_grave,
    KC(16),  KS_q,		KS_Q,		KS_at,
    KC(21),  KS_z,
    KC(26),  KS_udiaeresis,
    KC(27),  KS_plus,		KS_asterisk,	KS_dead_tilde,
    KC(39),  KS_odiaeresis,
    KC(40),  KS_adiaeresis,
    KC(41),  KS_dead_circumflex,KS_dead_abovering,
    KC(43),  KS_numbersign,	KS_apostrophe,
    KC(44),  KS_y,
    KC(50),  KS_m,		KS_M,		KS_mu,
    KC(51),  KS_comma,		KS_semicolon,
    KC(52),  KS_period,		KS_colon,
    KC(53),  KS_minus,		KS_underscore,
    KC(86),  KS_less,		KS_greater,	KS_bar,		KS_brokenbar,
    KC(184), KS_Mode_switch,	KS_Multi_key,
};

static const keysym_t pckbd_keydesc_de_nodead[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(13),  KS_apostrophe,	KS_grave,
    KC(27),  KS_plus,		KS_asterisk,	KS_asciitilde,
    KC(41),  KS_asciicircum,	KS_degree,
};

static const keysym_t pckbd_keydesc_dk[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(3),   KS_2,		KS_quotedbl,	KS_at,
    KC(4),   KS_3,		KS_numbersign,	KS_sterling,
    KC(5),   KS_4,		KS_currency,	KS_dollar,
    KC(7),   KS_6,		KS_ampersand,
    KC(8),   KS_7,		KS_slash,	KS_braceleft,
    KC(9),   KS_8,		KS_parenleft,	KS_bracketleft,
    KC(10),  KS_9,		KS_parenright,	KS_bracketright,
    KC(11),  KS_0,		KS_equal,	KS_braceright,
    KC(12),  KS_plus,		KS_question,
    KC(13),  KS_dead_acute,	KS_dead_grave,	KS_bar,
    KC(26),  KS_aring,
    KC(27),  KS_dead_diaeresis,	KS_dead_circumflex, KS_dead_tilde,
    KC(39),  KS_ae,
    KC(40),  KS_oslash,
    KC(41),  KS_onehalf,	KS_paragraph,
    KC(43),  KS_apostrophe,	KS_asterisk,
    KC(51),  KS_comma,		KS_semicolon,
    KC(52),  KS_period,		KS_colon,
    KC(53),  KS_minus,		KS_underscore,
    KC(86),  KS_less,		KS_greater,	KS_backslash,
    KC(184), KS_Mode_switch,	KS_Multi_key,
};

static const keysym_t pckbd_keydesc_dk_nodead[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(13),  KS_apostrophe,	KS_grave,	KS_bar,
    KC(27),  KS_diaeresis,	KS_asciicircum,	KS_asciitilde,
};

static const keysym_t pckbd_keydesc_sv[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(12),  KS_plus,		KS_question,	KS_backslash,
    KC(27),  KS_dead_diaeresis,	KS_dead_circumflex, KS_dead_tilde,
    KC(39),  KS_odiaeresis,
    KC(40),  KS_adiaeresis,
    KC(41),  KS_paragraph,	KS_onehalf,
    KC(86),  KS_less,		KS_greater,	KS_bar,
    KC(184), KS_Mode_switch,	KS_Multi_key,
};

static const keysym_t pckbd_keydesc_sv_nodead[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(13),  KS_apostrophe,	KS_grave,	KS_bar,
    KC(27),  KS_diaeresis,	KS_asciicircum,	KS_asciitilde,
};

static const keysym_t pckbd_keydesc_no[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(13),  KS_backslash,	KS_dead_grave,	KS_dead_acute,
    KC(27),  KS_dead_diaeresis,	KS_dead_circumflex, KS_dead_tilde,
    KC(39),  KS_oslash,
    KC(40),  KS_ae,
    KC(41),  KS_bar,		KS_paragraph,
    KC(86),  KS_less,		KS_greater,
};

static const keysym_t pckbd_keydesc_no_nodead[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(13),  KS_backslash,	KS_grave,	KS_acute,
    KC(27),  KS_diaeresis,	KS_asciicircum,	KS_asciitilde,
};

static const keysym_t pckbd_keydesc_fr[] = {
/*  pos	     normal		shifted		altgr		shift-altgr */
    KC(2),   KS_ampersand,	KS_1,
    KC(3),   KS_eacute,		KS_2,		KS_asciitilde,
    KC(4),   KS_quotedbl,	KS_3,		KS_numbersign,
    KC(5),   KS_apostrophe,	KS_4,		KS_braceleft,
    KC(6),   KS_parenleft,	KS_5,		KS_bracketleft,
    KC(7),   KS_minus,		KS_6,		KS_bar,
    KC(8),   KS_egrave,		KS_7,		KS_grave,
    KC(9),   KS_underscore,	KS_8,		KS_backslash,
    KC(10),  KS_ccedilla,	KS_9,		KS_asciicircum,
    KC(11),  KS_agrave,		KS_0,		KS_at,
    KC(12),  KS_parenright,	KS_degree,	KS_bracketright,
    KC(13),  KS_equal,		KS_plus,	KS_braceright,
    KC(16),  KS_a,
    KC(17),  KS_z,
    KC(26),  KS_dead_circumflex, KS_dead_diaeresis, 
    KC(27),  KS_dollar,		KS_sterling,	KS_currency,
    KC(30),  KS_q,
    KC(39),  KS_m,
    KC(40),  KS_ugrave,		KS_percent,
    KC(41),  KS_twosuperior,
    KC(43),  KS_asterisk,	KS_mu,
    KC(44),  KS_w,
    KC(50),  KS_comma,		KS_question,
    KC(51),  KS_semicolon,	KS_period,
    KC(52),  KS_colon,		KS_slash,
    KC(53),  KS_exclam,		KS_section,
    KC(86),  KS_less,		KS_greater,
    KC(184), KS_Mode_switch,	KS_Multi_key,
};

static const keysym_t pckbd_keydesc_it[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(3),   KS_2,	    	KS_quotedbl,	KS_twosuperior,
    KC(4),   KS_3,	    	KS_sterling,	KS_threesuperior,
    KC(5),   KS_4,	    	KS_dollar,
    KC(6),   KS_5,	    	KS_percent,
    KC(7),   KS_6,	    	KS_ampersand,
    KC(8),   KS_7,	    	KS_slash,
    KC(9),   KS_8,	    	KS_parenleft,
    KC(10),  KS_9,	    	KS_parenright,
    KC(11),  KS_0,	    	KS_equal,
    KC(12),  KS_apostrophe,	KS_question,
    KC(13),  KS_igrave,	    	KS_asciicircum,
    KC(26),  KS_egrave,		KS_eacute,	KS_braceleft,	KS_bracketleft,
    KC(27),  KS_plus,		KS_asterisk,	KS_braceright,	KS_bracketright,
    KC(39),  KS_ograve,		KS_Ccedilla,	KS_at,
    KC(40),  KS_agrave,		KS_degree,	KS_numbersign,
    KC(41),  KS_backslash,	KS_bar,
    KC(43),  KS_ugrave,		KS_section,
    KC(51),  KS_comma,		KS_semicolon,
    KC(52),  KS_period,		KS_colon,
    KC(53),  KS_minus,		KS_underscore,
    KC(86),  KS_less,		KS_greater,
    KC(184), KS_Mode_switch,	KS_Multi_key,
};

static const keysym_t pckbd_keydesc_uk[] = {
/*  pos      normal             shifted         altgr           shift-altgr */
    KC(2),   KS_1,              KS_exclam,      KS_plusminus,   KS_exclamdown,
    KC(3),   KS_2,              KS_quotedbl,    KS_twosuperior, KS_cent,
    KC(4),   KS_3,              KS_sterling,    KS_threesuperior,
    KC(5),   KS_4,              KS_dollar,      KS_acute,       KS_currency,
    KC(6),   KS_5,              KS_percent,     KS_mu,          KS_yen,
    KC(7),   KS_6,              KS_asciicircum, KS_paragraph,
    KC(8),   KS_7,              KS_ampersand,   KS_periodcentered, KS_brokenbar,
    KC(9),   KS_8,              KS_asterisk,    KS_cedilla,     KS_ordfeminine,
    KC(10),  KS_9,              KS_parenleft,   KS_onesuperior, KS_diaeresis,
    KC(11),  KS_0,              KS_parenright,  KS_masculine,   KS_copyright,
    KC(12),  KS_minus,          KS_underscore,  KS_hyphen,      KS_ssharp,
    KC(13),  KS_equal,          KS_plus,        KS_onehalf,    KS_guillemotleft,
    KC(40),  KS_apostrophe,     KS_at,          KS_section,     KS_Agrave,
    KC(41),  KS_grave,          KS_grave,       KS_agrave,      KS_agrave,
    KC(43),  KS_numbersign,     KS_asciitilde,  KS_sterling,    KS_thorn,
    KC(86),  KS_backslash,      KS_bar,         KS_Udiaeresis,
};

static const keysym_t pckbd_keydesc_jp[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(3),   KS_2,              KS_quotedbl,
    KC(7),   KS_6,              KS_ampersand,
    KC(8),   KS_7,              KS_apostrophe,
    KC(9),   KS_8,              KS_parenleft,
    KC(10),  KS_9,              KS_parenright,
    KC(11),  KS_0,
    KC(12),  KS_minus,          KS_equal,
    KC(13),  KS_asciicircum,    KS_asciitilde,
    KC(26),  KS_at,             KS_grave,
    KC(27),  KS_bracketleft,    KS_braceleft,
    KC(39),  KS_semicolon,      KS_plus,
    KC(40),  KS_colon,          KS_asterisk,
    KC(41),  KS_Zenkaku_Hankaku, /* replace grave/tilde */
    KC(43),  KS_bracketright,   KS_braceright,
    KC(112), KS_Hiragana_Katakana,
    KC(115), KS_backslash,      KS_underscore,
    KC(121), KS_Henkan,
    KC(123), KS_Muhenkan,
    KC(125), KS_backslash,      KS_bar,
};

static const keysym_t pckbd_keydesc_es[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(2),   KS_1,		KS_exclam,	KS_bar,
    KC(3),   KS_2,		KS_quotedbl,	KS_at,
    KC(4),   KS_3,		KS_periodcentered, KS_numbersign,
    KC(5),   KS_4,		KS_dollar,	KS_asciitilde,
    KC(7),   KS_6,		KS_ampersand,
    KC(8),   KS_7,		KS_slash,
    KC(9),   KS_8,		KS_parenleft,
    KC(10),  KS_9,		KS_parenright,
    KC(11),  KS_0,		KS_equal,
    KC(12),  KS_apostrophe,	KS_question,
    KC(13),  KS_exclamdown,	KS_questiondown,
    KC(26),  KS_dead_grave,	KS_dead_circumflex, KS_bracketleft,
    KC(27),  KS_plus,		KS_asterisk,	KS_bracketright,
    KC(39),  KS_ntilde,
    KC(40),  KS_dead_acute,	KS_dead_diaeresis, KS_braceleft,
    KC(41),  KS_degree,		KS_ordfeminine,	KS_backslash,
    KC(43),  KS_ccedilla,	KS_Ccedilla,	KS_braceright,
    KC(51),  KS_comma,		KS_semicolon,
    KC(52),  KS_period,		KS_colon,
    KC(53),  KS_minus,		KS_underscore,
    KC(86),  KS_less,		KS_greater,
    KC(184), KS_Mode_switch,	KS_Multi_key,
};

static const keysym_t pckbd_keydesc_lt[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(2),   KS_1,		KS_exclam,	KS_agrave,	KS_Agrave,
    KC(3),   KS_2,		KS_at,		KS_egrave,	KS_Egrave,
    KC(3),   KS_2,		KS_numbersign,	KS_ae,		KS_AE,
    KC(3),   KS_2,		KS_dollar,	KS_ediaeresis,	KS_Ediaeresis,
    KC(3),   KS_2,		KS_percent,	KS_aacute,	KS_Aacute,
    KC(3),   KS_2,		KS_asciicircum,	KS_eth,		KS_ETH,
    KC(3),   KS_2,		KS_ampersand,	KS_oslash,	KS_Ooblique,
    KC(3),   KS_2,		KS_asterisk,	KS_ucircumflex,	KS_Ucircumflex,
    KC(13),  KS_equal,		KS_plus,	KS_thorn,	KS_THORN,
    KC(184), KS_Mode_switch,	KS_Multi_key,
};

static const keysym_t pckbd_keydesc_be[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(2),   KS_ampersand,	KS_1,		KS_bar,
    KC(3),   KS_eacute,		KS_2,		KS_at,
    KC(4),   KS_quotedbl,	KS_3,		KS_numbersign,
    KC(5),   KS_apostrophe,	KS_4,
    KC(6),   KS_parenleft,	KS_5,
    KC(7),   KS_currency,	KS_6,		KS_asciicircum,
    KC(8),   KS_egrave,		KS_7,
    KC(9),   KS_exclam,		KS_8,
    KC(10),  KS_ccedilla,	KS_9,		KS_braceleft,
    KC(11),  KS_agrave,		KS_0,		KS_braceright,
    KC(12),  KS_parenright,	KS_degree,
    KC(13),  KS_minus,		KS_underscore,
    KC(16),  KS_a,
    KC(17),  KS_z,
    KC(26),  KS_dead_circumflex, KS_dead_diaeresis, KS_bracketleft,
    KC(27),  KS_dollar,		KS_asterisk,	KS_bracketright,
    KC(30),  KS_q,
    KC(39),  KS_m,
    KC(40),  KS_ugrave,		KS_percent,	KS_section,
    KC(41),  KS_twosuperior,
    KC(43),  KS_mu,		KS_sterling,	KS_grave,
    KC(44),  KS_w,
    KC(50),  KS_comma,		KS_question,
    KC(51),  KS_semicolon,	KS_period,
    KC(52),  KS_colon,		KS_slash,
    KC(53),  KS_equal,		KS_plus,	KS_asciitilde,
    KC(86),  KS_less,		KS_greater,	KS_backslash,
    KC(184), KS_Mode_switch,	KS_Multi_key,
};

static const keysym_t pckbd_keydesc_us_declk[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(1),	KS_grave,	KS_asciitilde, /* replace escape */
    KC(41),	KS_less,	KS_greater, /* replace grave/tilde */
    KC(143),	KS_Multi_key, /* left compose */
    KC(157),	KS_Multi_key, /* right compose, replace right control */
    KC(87),	KS_Cmd_Debugger,	KS_Escape, /* replace F11 */
    KC(189),	KS_f13,
    KC(190),	KS_f14,
    KC(191),	KS_Help,
    KC(192),	KS_Execute,
    KC(193),	KS_f17,
    KC(183),	KS_f18,
    KC(70),	KS_f19, /* replace scroll lock */
    KC(127),	KS_f20, /* replace break */
    KC(69),	KS_KP_F1, /* replace num lock */
    KC(181),	KS_KP_F2, /* replace divide */
    KC(55),	KS_KP_F3, /* replace multiply */
    KC(74),	KS_KP_F4, /* replace subtract */

    /* keypad is numbers only - no num lock */
    KC(71), 	KS_KP_7,
    KC(72), 	KS_KP_8,
    KC(73), 	KS_KP_9,
    KC(75), 	KS_KP_4,
    KC(76), 	KS_KP_5,
    KC(77), 	KS_KP_6,
    KC(79), 	KS_KP_1,
    KC(80), 	KS_KP_2,
    KC(81), 	KS_KP_3,
    KC(82), 	KS_KP_0,
    KC(83), 	KS_KP_Decimal,

    KC(206),	KS_KP_Subtract,
    KC(78),	KS_KP_Separator, /* replace add */
    KC(199),	KS_Find, /* replace home */
    KC(207),	KS_Select, /* replace end */
};

static const keysym_t pckbd_keydesc_us_dvorak[] = {
/*  pos      command		normal		shifted */
    KC(12), 			KS_bracketleft,	KS_braceleft,
    KC(13), 			KS_bracketright, KS_braceright,
    KC(16), 			KS_apostrophe, KS_quotedbl,
    KC(17), 			KS_comma, KS_less,
    KC(18), 			KS_period, KS_greater,
    KC(19), 			KS_p,
    KC(20), 			KS_y,
    KC(21), 			KS_f,
    KC(22), 			KS_g,
    KC(23), 			KS_c,
    KC(24), 			KS_r,
    KC(25), 			KS_l,
    KC(26), 			KS_slash, KS_question,
    KC(27), 			KS_equal, KS_plus,
    KC(31), 			KS_o,
    KC(32), 			KS_e,
    KC(33), 			KS_u,
    KC(34), 			KS_i,
    KC(35), 			KS_d,
    KC(36), 			KS_h,
    KC(37), 			KS_t,
    KC(38), 			KS_n,
    KC(39), 			KS_s,
    KC(40), 			KS_minus, KS_underscore,
    KC(44), 			KS_semicolon, KS_colon,
    KC(45), 			KS_q,
    KC(46), 			KS_j,
    KC(47), 			KS_k,
    KC(48), 			KS_x,
    KC(49), 			KS_b,
    KC(51), 			KS_w,
    KC(52), 			KS_v,
    KC(53), 			KS_z,
};

static const keysym_t pckbd_keydesc_swapctrlcaps[] = {
/*  pos      command		normal		shifted */
    KC(29), 			KS_Caps_Lock,
    KC(58),  KS_Cmd1,		KS_Control_L,
};

static const keysym_t pckbd_keydesc_iopener[] = {
/*  pos      command		normal		shifted */
    KC(59),  KS_Cmd_Debugger,	KS_Escape,
    KC(60),  KS_Cmd_Screen0,	KS_f1,
    KC(61),  KS_Cmd_Screen1,	KS_f2,
    KC(62),  KS_Cmd_Screen2,	KS_f3,
    KC(63),  KS_Cmd_Screen3,	KS_f4,
    KC(64),  KS_Cmd_Screen4,	KS_f5,
    KC(65),  KS_Cmd_Screen5,	KS_f6,
    KC(66),  KS_Cmd_Screen6,	KS_f7,
    KC(67),  KS_Cmd_Screen7,	KS_f8,
    KC(68),  KS_Cmd_Screen8,	KS_f9,
    KC(87),  KS_Cmd_Screen9,	KS_f10,
    KC(88), 			KS_f11,
};

static const keysym_t pckbd_keydesc_ru[] = {
/*  pos      normal		shifted		altgr			shift-altgr */
    KC(7),   KS_6,		KS_asciicircum,	KS_6,			KS_comma,
    KC(8),   KS_7,		KS_ampersand,	KS_7,			KS_period,
    KC(16),  KS_q,		KS_Q,		KS_Cyrillic_ishort,	KS_Cyrillic_ISHORT,
    KC(17),  KS_w,		KS_W,		KS_Cyrillic_tse,	KS_Cyrillic_TSE,
    KC(18),  KS_e,		KS_E,		KS_Cyrillic_u,		KS_Cyrillic_U,
    KC(19),  KS_r,		KS_R,		KS_Cyrillic_ka,		KS_Cyrillic_KA,
    KC(20),  KS_t,		KS_T,		KS_Cyrillic_ie,		KS_Cyrillic_IE,
    KC(21),  KS_y,		KS_Y,		KS_Cyrillic_en,		KS_Cyrillic_EN,
    KC(22),  KS_u,		KS_U,		KS_Cyrillic_ge,		KS_Cyrillic_GE,
    KC(23),  KS_i,		KS_I,		KS_Cyrillic_sha,	KS_Cyrillic_SHA,
    KC(24),  KS_o,		KS_O,		KS_Cyrillic_scha,	KS_Cyrillic_SCHA,
    KC(25),  KS_p,		KS_P,		KS_Cyrillic_ze,		KS_Cyrillic_ZE,
    KC(26),  KS_bracketleft,	KS_braceleft,	KS_Cyrillic_ha,		KS_Cyrillic_HA,
    KC(27),  KS_bracketright,	KS_braceright,	KS_Cyrillic_hsighn,	KS_Cyrillic_HSIGHN,
    KC(30),  KS_a,		KS_A,		KS_Cyrillic_ef,		KS_Cyrillic_EF,
    KC(31),  KS_s,		KS_S,		KS_Cyrillic_yeru,	KS_Cyrillic_YERU,
    KC(32),  KS_d,		KS_D,		KS_Cyrillic_ve,		KS_Cyrillic_VE,
    KC(33),  KS_f,		KS_F,		KS_Cyrillic_a,		KS_Cyrillic_A,
    KC(34),  KS_g,		KS_G,		KS_Cyrillic_pe,		KS_Cyrillic_PE,
    KC(35),  KS_h,		KS_H,		KS_Cyrillic_er,		KS_Cyrillic_ER,
    KC(36),  KS_j,		KS_J,		KS_Cyrillic_o,		KS_Cyrillic_O,
    KC(37),  KS_k,		KS_K,		KS_Cyrillic_el,		KS_Cyrillic_EL,
    KC(38),  KS_l,		KS_L,		KS_Cyrillic_de,		KS_Cyrillic_DE,
    KC(39),  KS_semicolon,	KS_colon,	KS_Cyrillic_zhe,	KS_Cyrillic_ZHE,
    KC(40),  KS_apostrophe,	KS_quotedbl,	KS_Cyrillic_e,		KS_Cyrillic_E,
    KC(44),  KS_z,		KS_Z,		KS_Cyrillic_ya,		KS_Cyrillic_YA,
    KC(45),  KS_x,		KS_X,		KS_Cyrillic_che,	KS_Cyrillic_CHE,
    KC(46),  KS_c,		KS_C,		KS_Cyrillic_es,		KS_Cyrillic_ES,
    KC(47),  KS_v,		KS_V,		KS_Cyrillic_em,		KS_Cyrillic_EM,
    KC(48),  KS_b,		KS_B,		KS_Cyrillic_i,		KS_Cyrillic_I,
    KC(49),  KS_n,		KS_N,		KS_Cyrillic_te,		KS_Cyrillic_TE,
    KC(50),  KS_m,		KS_M,		KS_Cyrillic_ssighn,	KS_Cyrillic_SSIGHN,
    KC(51),  KS_comma,		KS_less,	KS_Cyrillic_be,		KS_Cyrillic_BE,
    KC(52),  KS_period,		KS_greater,	KS_Cyrillic_yu,		KS_Cyrillic_YU,
    KC(53),  KS_slash,		KS_question,	KS_Cyrillic_yo,		KS_Cyrillic_YO,
    KC(184), KS_Mode_switch,   KS_Multi_key,
};

static const keysym_t pckbd_keydesc_ua[] = {
/*  pos      normal		shifted		altgr			shift-altgr */
    KC(7),   KS_6,		KS_asciicircum,	KS_6,			KS_comma,
    KC(8),   KS_7,		KS_ampersand,	KS_7,			KS_period,
    KC(12),   KS_minus,		KS_underscore,	KS_Cyrillic_iukr,			KS_Cyrillic_IUKR,
    KC(13),   KS_equal,		KS_plus,	KS_Cyrillic_yeukr,			KS_Cyrillic_YEUKR,
    KC(16),  KS_q,		KS_Q,		KS_Cyrillic_ishort,	KS_Cyrillic_ISHORT,
    KC(17),  KS_w,		KS_W,		KS_Cyrillic_tse,	KS_Cyrillic_TSE,
    KC(18),  KS_e,		KS_E,		KS_Cyrillic_u,		KS_Cyrillic_U,
    KC(19),  KS_r,		KS_R,		KS_Cyrillic_ka,		KS_Cyrillic_KA,
    KC(20),  KS_t,		KS_T,		KS_Cyrillic_ie,		KS_Cyrillic_IE,
    KC(21),  KS_y,		KS_Y,		KS_Cyrillic_en,		KS_Cyrillic_EN,
    KC(22),  KS_u,		KS_U,		KS_Cyrillic_ge,		KS_Cyrillic_GE,
    KC(23),  KS_i,		KS_I,		KS_Cyrillic_sha,	KS_Cyrillic_SHA,
    KC(24),  KS_o,		KS_O,		KS_Cyrillic_scha,	KS_Cyrillic_SCHA,
    KC(25),  KS_p,		KS_P,		KS_Cyrillic_ze,		KS_Cyrillic_ZE,
    KC(26),  KS_bracketleft,	KS_braceleft,	KS_Cyrillic_ha,		KS_Cyrillic_HA,
    KC(27),  KS_bracketright,	KS_braceright,	KS_Cyrillic_hsighn,	KS_Cyrillic_HSIGHN,
    KC(30),  KS_a,		KS_A,		KS_Cyrillic_ef,		KS_Cyrillic_EF,
    KC(31),  KS_s,		KS_S,		KS_Cyrillic_yeru,	KS_Cyrillic_YERU,
    KC(32),  KS_d,		KS_D,		KS_Cyrillic_ve,		KS_Cyrillic_VE,
    KC(33),  KS_f,		KS_F,		KS_Cyrillic_a,		KS_Cyrillic_A,
    KC(34),  KS_g,		KS_G,		KS_Cyrillic_pe,		KS_Cyrillic_PE,
    KC(35),  KS_h,		KS_H,		KS_Cyrillic_er,		KS_Cyrillic_ER,
    KC(36),  KS_j,		KS_J,		KS_Cyrillic_o,		KS_Cyrillic_O,
    KC(37),  KS_k,		KS_K,		KS_Cyrillic_el,		KS_Cyrillic_EL,
    KC(38),  KS_l,		KS_L,		KS_Cyrillic_de,		KS_Cyrillic_DE,
    KC(39),  KS_semicolon,	KS_colon,	KS_Cyrillic_zhe,	KS_Cyrillic_ZHE,
    KC(40),  KS_apostrophe,	KS_quotedbl,	KS_Cyrillic_e,		KS_Cyrillic_E,
    KC(41),  KS_grave,	KS_asciitilde,	KS_Cyrillic_gheukr,		KS_Cyrillic_GHEUKR,
    KC(43),  KS_backslash,	KS_bar,	KS_Cyrillic_yi,		KS_Cyrillic_YI,
    KC(44),  KS_z,		KS_Z,		KS_Cyrillic_ya,		KS_Cyrillic_YA,
    KC(45),  KS_x,		KS_X,		KS_Cyrillic_che,	KS_Cyrillic_CHE,
    KC(46),  KS_c,		KS_C,		KS_Cyrillic_es,		KS_Cyrillic_ES,
    KC(47),  KS_v,		KS_V,		KS_Cyrillic_em,		KS_Cyrillic_EM,
    KC(48),  KS_b,		KS_B,		KS_Cyrillic_i,		KS_Cyrillic_I,
    KC(49),  KS_n,		KS_N,		KS_Cyrillic_te,		KS_Cyrillic_TE,
    KC(50),  KS_m,		KS_M,		KS_Cyrillic_ssighn,	KS_Cyrillic_SSIGHN,
    KC(51),  KS_comma,		KS_less,	KS_Cyrillic_be,		KS_Cyrillic_BE,
    KC(52),  KS_period,		KS_greater,	KS_Cyrillic_yu,		KS_Cyrillic_YU,
    KC(53),  KS_slash,		KS_question,	KS_Cyrillic_yo,		KS_Cyrillic_YO,
    KC(184), KS_Mode_switch,   KS_Multi_key,
};

static const keysym_t pckbd_keydesc_sg[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(2),   KS_1,		KS_plus,	KS_bar,
    KC(3),   KS_2,		KS_quotedbl,	KS_at,
    KC(4),   KS_3,		KS_asterisk,	KS_numbersign,
    KC(5),   KS_4,		KS_ccedilla,
    KC(7),   KS_6,		KS_ampersand,	KS_notsign,
    KC(8),   KS_7,		KS_slash,	KS_brokenbar,
    KC(9),   KS_8,		KS_parenleft,	KS_cent,
    KC(10),  KS_9,		KS_parenright,
    KC(11),  KS_0,		KS_equal,
    KC(12),  KS_apostrophe,	KS_question,	KS_dead_acute,
    KC(13),  KS_dead_circumflex,KS_dead_grave,	KS_dead_tilde,
    KC(18),  KS_e,		KS_E,		KS_currency,
    KC(21),  KS_z,
    KC(26),  KS_udiaeresis,	KS_egrave,	KS_bracketleft,
    KC(27),  KS_dead_diaeresis,	KS_exclam,	KS_bracketright,
    KC(39),  KS_odiaeresis,	KS_eacute,
    KC(40),  KS_adiaeresis,	KS_agrave,	KS_braceleft,
    KC(41),  KS_section,	KS_degree,	KS_dead_abovering,
    KC(43),  KS_dollar,		KS_sterling,	KS_braceright,
    KC(44),  KS_y,
    KC(51),  KS_comma,		KS_semicolon,
    KC(52),  KS_period,		KS_colon,
    KC(53),  KS_minus,		KS_underscore,
    KC(86),  KS_less,		KS_greater,	KS_backslash,
    KC(184), KS_Mode_switch,	KS_Multi_key,
};

static const keysym_t pckbd_keydesc_sg_nodead[] = {
/*  pos      normal		shifted		altgr		shift-altgr */
    KC(12),  KS_apostrophe,	KS_question,	KS_acute,
    KC(13),  KS_asciicircum,	KS_grave,	KS_asciitilde,
    KC(27),  KS_diaeresis,	KS_exclam,	KS_bracketright
};

static const keysym_t pckbd_keydesc_sf[] = {
/*  pos      normal            shifted         altgr           shift-altgr */
    KC(26),  KS_egrave,        KS_udiaeresis,  KS_bracketleft,
    KC(39),  KS_eacute,        KS_odiaeresis,
    KC(40),  KS_agrave,        KS_adiaeresis,  KS_braceleft
};

static const keysym_t pckbd_keydesc_pt[] = {
/*  pos      normal            shifted         altgr           shift-altgr */
    KC(3),   KS_2,             KS_quotedbl,    KS_at,
    KC(4),   KS_3,             KS_numbersign,  KS_sterling,
    KC(5),   KS_4,             KS_dollar,
    KC(7),   KS_6,             KS_ampersand,
    KC(8),   KS_7,             KS_slash,       KS_braceleft,
    KC(9),   KS_8,             KS_parenleft,   KS_bracketleft,
    KC(10),  KS_9,             KS_parenright,  KS_bracketright,
    KC(11),  KS_0,             KS_equal,       KS_braceright,
    KC(12),  KS_apostrophe,    KS_question,
    KC(13),  KS_less,          KS_greater,
    KC(26),  KS_plus,          KS_asterisk,
    KC(27),  KS_dead_acute,    KS_dead_grave,
    KC(39),  KS_ccedilla,      KS_Ccedilla,
    KC(40),  KS_masculine,     KS_ordfeminine,
    KC(41),  KS_backslash,     KS_bar,
    KC(43),  KS_dead_tilde,    KS_dead_circumflex,
    KC(51),  KS_comma,         KS_semicolon,
    KC(52),  KS_period,        KS_colon,
    KC(53),  KS_minus,         KS_underscore,
    KC(86),  KS_less,          KS_greater,
    KC(184), KS_Mode_switch,   KS_Multi_key,
};

#define KBD_MAP(name, base, map) \
			{ name, base, sizeof(map)/sizeof(keysym_t), map }

const struct wscons_keydesc pckbd_keydesctab[] = {
	KBD_MAP(KB_US,			0,	pckbd_keydesc_us),
	KBD_MAP(KB_DE,			KB_US,	pckbd_keydesc_de),
	KBD_MAP(KB_DE | KB_NODEAD,	KB_DE,	pckbd_keydesc_de_nodead),
	KBD_MAP(KB_FR,                  KB_US,  pckbd_keydesc_fr),
	KBD_MAP(KB_DK,			KB_US,	pckbd_keydesc_dk),
	KBD_MAP(KB_DK | KB_NODEAD,	KB_DK,	pckbd_keydesc_dk_nodead),
	KBD_MAP(KB_IT,			KB_US,	pckbd_keydesc_it),
	KBD_MAP(KB_UK,			KB_US,	pckbd_keydesc_uk),
	KBD_MAP(KB_JP,			KB_US,	pckbd_keydesc_jp),
	KBD_MAP(KB_SV,			KB_DK,	pckbd_keydesc_sv),
	KBD_MAP(KB_SV | KB_NODEAD,	KB_SV,	pckbd_keydesc_sv_nodead),
	KBD_MAP(KB_NO,			KB_DK,	pckbd_keydesc_no),
	KBD_MAP(KB_NO | KB_NODEAD,	KB_NO,	pckbd_keydesc_no_nodead),
	KBD_MAP(KB_US | KB_DECLK,	KB_US,	pckbd_keydesc_us_declk),
	KBD_MAP(KB_US | KB_DVORAK,	KB_US,	pckbd_keydesc_us_dvorak),
	KBD_MAP(KB_US | KB_SWAPCTRLCAPS, KB_US,	pckbd_keydesc_swapctrlcaps),
	KBD_MAP(KB_US | KB_IOPENER, KB_US,	pckbd_keydesc_iopener),
	KBD_MAP(KB_JP | KB_SWAPCTRLCAPS, KB_JP, pckbd_keydesc_swapctrlcaps),
	KBD_MAP(KB_FR | KB_SWAPCTRLCAPS, KB_FR, pckbd_keydesc_swapctrlcaps),
	KBD_MAP(KB_BE | KB_SWAPCTRLCAPS, KB_BE, pckbd_keydesc_swapctrlcaps),
	KBD_MAP(KB_US | KB_DVORAK | KB_SWAPCTRLCAPS,	KB_US | KB_DVORAK,
		pckbd_keydesc_swapctrlcaps),
	KBD_MAP(KB_US | KB_IOPENER | KB_SWAPCTRLCAPS,	KB_US | KB_IOPENER,
		pckbd_keydesc_swapctrlcaps),
	KBD_MAP(KB_ES,			KB_US,	pckbd_keydesc_es),
	KBD_MAP(KB_BE,			KB_US,	pckbd_keydesc_be),
	KBD_MAP(KB_RU,			KB_US,	pckbd_keydesc_ru),
	KBD_MAP(KB_UA,			KB_US,	pckbd_keydesc_ua),
	KBD_MAP(KB_SG,			KB_US,	pckbd_keydesc_sg),
	KBD_MAP(KB_SG | KB_NODEAD,	KB_SG,	pckbd_keydesc_sg_nodead),
	KBD_MAP(KB_SF,			KB_SG,	pckbd_keydesc_sf),
	KBD_MAP(KB_SF | KB_NODEAD,	KB_SF,	pckbd_keydesc_sg_nodead),
	KBD_MAP(KB_PT,			KB_US,	pckbd_keydesc_pt),
	KBD_MAP(KB_LT,			KB_US,	pckbd_keydesc_lt),
	{0, 0, 0, 0}
};

#undef KBD_MAP
#undef KC
