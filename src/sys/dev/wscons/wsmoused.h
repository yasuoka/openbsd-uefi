/* $OpenBSD: wsmoused.h,v 1.1 2001/04/14 04:44:02 aaron Exp $ */

/*
 * Copyright (c) 2001 Jean-Baptiste Marchand, Julien Montagne and Jerome Verdon
 * 
 * All rights reserved.
 *
 * This code is for mouse console support under the wscons console driver.
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
 *	This product includes software developed by
 *	Hellmuth Michaelis, Brian Dunford-Shore, Joerg Wunsch, Scott Turner
 *	and Charles Hannum.
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
 */


int wsmoused __P((struct wsdisplay_softc *, u_long, caddr_t, int,
		  struct proc *p));

void motion_event __P((u_int, int));
void button_event __P((int, int));
int ctrl_event __P((u_int, int, struct wsdisplay_softc *, struct proc *));

void mouse_moverel __P((char, char));

void inverse_char __P((unsigned short c));
void inverse_region __P((unsigned short start, unsigned short end));

unsigned char skip_spc_right __P((char));
unsigned char skip_spc_left __P((void));
unsigned char skip_char_right __P((unsigned short));
unsigned char skip_char_left __P((unsigned short));
unsigned char class_cmp __P((unsigned short, unsigned short));

void mouse_copy_start __P((void));
void mouse_copy_word __P((void));
void mouse_copy_line __P((void));
void mouse_copy_end __P((void));
void mouse_copy_extend __P((void));
void mouse_copy_extend_char __P((void));
void mouse_copy_extend_word __P((void));
void mouse_copy_extend_line __P((void));
void mouse_hide __P((struct wsdisplay_softc *));
void mouse_copy_extend_after __P((void));
void remove_selection __P((struct wsdisplay_softc *));
void mouse_copy_selection __P((void));
void mouse_paste __P((void));

void mouse_zaxis __P((int));
void allocate_copybuffer __P((struct wsdisplay_softc *));

void sysbeep __P((int, int));

char *Copybuffer = NULL; /* buffer that contains mouse selections */
unsigned int Copybuffer_size = 0;
char Paste_avail = 0; /* flag, to indicate whether a selection is in the
			 Copy buffer */
			      
#define NO_BORDER 0
#define BORDER 1

#define WS_NCOLS(ws) ((ws)->scr_dconf->scrdata->ncols)
#define WS_NROWS(ws) ((ws)->scr_dconf->scrdata->nrows)

#define MAXCOL (WS_NCOLS(sc->sc_focus) - 1)
#define MAXROW (WS_NROWS(sc->sc_focus) - 1)

#define N_COLS 		(WS_NCOLS(sc->sc_focus))
#define N_ROWS 		(WS_NROWS(sc->sc_focus))
#define MOUSE 		(sc->sc_focus->mouse)
#define CURSOR 		(sc->sc_focus->cursor)
#define CPY_START	(sc->sc_focus->cpy_start)
#define CPY_END		(sc->sc_focus->cpy_end)
#define ORIG_START	(sc->sc_focus->orig_start)
#define ORIG_END	(sc->sc_focus->orig_end)
#define MOUSE_FLAGS	(sc->sc_focus->mouse_flags)

#define XY_TO_POS(col, row) (((row) * N_COLS) + (col))
#define POS_TO_X(pos) ((pos) % (N_COLS))
#define POS_TO_Y(pos) ((pos) / (N_COLS))

#define GET_FULLCHAR(pos)\
((*sc->sc_accessops->getchar)\
 (sc->sc_accesscookie,\
 ((pos) / N_COLS), ((pos) % N_COLS)))
 
#define GETCHAR(pos)\
(((*sc->sc_accessops->getchar)\
 (sc->sc_accesscookie,\
 ((pos) / N_COLS), ((pos) % N_COLS)))\
 & 0x000000FF)
 
#define PUTCHAR(pos, uc, attr)\
((*sc->sc_focus->scr_dconf->emulops->putchar)\
(sc->sc_focus->scr_dconf->emulcookie, ((pos) / N_COLS),\
 ((pos) % N_COLS), (uc), (attr))); 

#define MOUSE_COPY_BUTTON 	0
#define MOUSE_PASTE_BUTTON 	1	
#define MOUSE_EXTEND_BUTTON	2

#define IS_ALPHANUM(pos) (GETCHAR((pos)) != ' ')
#define IS_SPACE(c) ((c) == ' ')
