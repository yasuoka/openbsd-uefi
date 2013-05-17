/*	$OpenBSD: dart.c,v 1.58 2013/05/17 22:46:27 miod Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
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

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/autoconf.h>
#include <machine/board.h>
#include <machine/conf.h>
#include <machine/cpu.h>

#include <dev/cons.h>

#include <machine/mvme188.h>
#include <mvme88k/dev/dartreg.h>

#ifdef	DDB
#include <ddb/db_var.h>
#endif

#define	NDARTPORTS	2	/* Number of ports */

struct dart_info {
	struct tty		*tty;
	u_char			dart_swflags;
};

/* saved registers */
struct dart_sv_reg {
	u_int8_t	sv_mr1[NDARTPORTS];
	u_int8_t	sv_mr2[NDARTPORTS];
	u_int8_t	sv_csr[NDARTPORTS];
	u_int8_t	sv_cr[NDARTPORTS];
	u_int8_t	sv_imr;
};

struct dartsoftc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	struct intrhand		sc_ih;

	struct dart_sv_reg	sc_sv_reg;
	struct dart_info	sc_dart[NDARTPORTS];
};

int	dartmatch(struct device *parent, void *self, void *aux);
void	dartattach(struct device *parent, struct device *self, void *aux);

struct cfattach dart_ca = {
	sizeof(struct dartsoftc), dartmatch, dartattach
};

struct cfdriver dart_cd = {
	NULL, "dart", DV_TTY
};

/* console is on the first port */
#define	CONS_PORT	A_PORT

/* prototypes */
cons_decl(dart);
int	dart_speed(int);
struct tty *darttty(dev_t);
void	dartstart(struct tty *);
int	dartmctl(dev_t, int, int);
int	dartparam(struct tty *, struct termios *);
void	dartmodemtrans(struct dartsoftc *, unsigned int, unsigned int);
void	dartrint(struct dartsoftc *, int);
void	dartxint(struct dartsoftc *, int);
int	dartintr(void *);

/*
 * DUART registers are mapped as the least-significant byte of 32-bit
 * addresses. The following macros hide this.
 */

#define	DART_SIZE	0x40
#define	dart_read(sc, reg) \
	bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, 3 + ((reg) << 2))
#define	dart_write(sc, reg, val) \
	bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, 3 + ((reg) << 2), (val))

#define DART_PORT(dev) minor(dev)

int
dartmatch(struct device *parent, void *cf, void *aux)
{
	struct confargs *ca = aux;
	bus_space_handle_t ioh;
	int rc;

	if (brdtyp != BRD_188)
		return (0);

	/*
	 * We do not accept empty locators here...
	 */
	if (ca->ca_paddr != DART_BASE)
		return (0);

	if (bus_space_map(ca->ca_iot, ca->ca_paddr, DART_SIZE, 0, &ioh) != 0)
		return (0);
	rc = badaddr((vaddr_t)bus_space_vaddr(ca->ca_iot, ioh), 4);
	bus_space_unmap(ca->ca_iot, ca->ca_paddr, DART_SIZE);

	return (rc == 0);
}

void
dartattach(struct device *parent, struct device *self, void *aux)
{
	struct dartsoftc *sc = (struct dartsoftc *)self;
	struct confargs *ca = aux;
	bus_space_handle_t ioh;

	if (ca->ca_ipl < 0)
		ca->ca_ipl = IPL_TTY;

	sc->sc_iot = ca->ca_iot;
	if (bus_space_map(sc->sc_iot, ca->ca_paddr, DART_SIZE, 0, &ioh) != 0) {
		printf(": can't map registers!\n");
		return;
	}
	sc->sc_ioh = ioh;

	/* save standard initialization */
	sc->sc_sv_reg.sv_mr1[A_PORT] = PARDIS | RXRTS | CL8;
	sc->sc_sv_reg.sv_mr2[A_PORT] = /* TXCTS | */ SB1;
	sc->sc_sv_reg.sv_csr[A_PORT] = BD9600;
	sc->sc_sv_reg.sv_cr[A_PORT]  = TXEN | RXEN;

	sc->sc_sv_reg.sv_mr1[B_PORT] = PARDIS | RXRTS | CL8;
	sc->sc_sv_reg.sv_mr2[B_PORT] = /* TXCTS | */ SB1;
	sc->sc_sv_reg.sv_csr[B_PORT] = BD9600;
	sc->sc_sv_reg.sv_cr[B_PORT]  = TXEN | RXEN;

	/* Start out with Tx and RX interrupts disabled */
	/* Enable input port change interrupt */
	sc->sc_sv_reg.sv_imr  = IIPCHG;

	/*
	 * Although we are still running using the BUG routines,
	 * this device will be elected as the console after
	 * autoconf.
	 * We do not even test since we know we are an MVME188 and
	 * console is always on the first port.
	 */
	printf(": console");

	/* reset port a */
	dart_write(sc, DART_CRA, RXRESET | TXDIS | RXDIS);
	DELAY_CR;
	dart_write(sc, DART_CRA, TXRESET | TXDIS | RXDIS);
	DELAY_CR;
	dart_write(sc, DART_CRA, ERRRESET | TXDIS | RXDIS);
	DELAY_CR;
	dart_write(sc, DART_CRA, BRKINTRESET | TXDIS | RXDIS);
	DELAY_CR;
	dart_write(sc, DART_CRA, MRRESET | TXDIS | RXDIS);
	DELAY_CR;

	/* reset port b */
	dart_write(sc, DART_CRB, RXRESET | TXDIS | RXDIS);
	DELAY_CR;
	dart_write(sc, DART_CRB, TXRESET | TXDIS | RXDIS);
	DELAY_CR;
	dart_write(sc, DART_CRB, ERRRESET | TXDIS | RXDIS);
	DELAY_CR;
	dart_write(sc, DART_CRB, BRKINTRESET | TXDIS | RXDIS);
	DELAY_CR;
	dart_write(sc, DART_CRB, MRRESET | TXDIS | RXDIS);
	DELAY_CR;

	/* initialize ports */
	dart_write(sc, DART_MR1A, sc->sc_sv_reg.sv_mr1[A_PORT]);
	dart_write(sc, DART_MR2A, sc->sc_sv_reg.sv_mr2[A_PORT]);
	dart_write(sc, DART_CSRA, sc->sc_sv_reg.sv_csr[A_PORT]);
	dart_write(sc, DART_CRA, sc->sc_sv_reg.sv_cr[A_PORT]);

	dart_write(sc, DART_MR1B, sc->sc_sv_reg.sv_mr1[B_PORT]);
	dart_write(sc, DART_MR2B, sc->sc_sv_reg.sv_mr2[B_PORT]);
	dart_write(sc, DART_CSRB, sc->sc_sv_reg.sv_csr[B_PORT]);
	dart_write(sc, DART_CRB, sc->sc_sv_reg.sv_cr[B_PORT]);

	/* initialize common register of a DUART */
	dart_write(sc, DART_OPRS, OPDTRA | OPRTSA | OPDTRB | OPRTSB);

	dart_write(sc, DART_CTUR, SLCTIM >> 8);
	dart_write(sc, DART_CTLR, SLCTIM & 0xff);
	dart_write(sc, DART_ACR, BDSET2 | CCLK16 | IPDCDIB | IPDCDIA);
	dart_write(sc, DART_IMR, sc->sc_sv_reg.sv_imr);
	dart_write(sc, DART_OPCR, OPSET);

	/* enable interrupts */
	sc->sc_ih.ih_fn = dartintr;
	sc->sc_ih.ih_arg = sc;
	sc->sc_ih.ih_wantframe = 0;
	sc->sc_ih.ih_ipl = ca->ca_ipl;

	platform->intsrc_establish(INTSRC_DUART, &sc->sc_ih, self->dv_xname);

	printf("\n");
}

/* speed tables */
const struct dart_s {
	int kspeed;
	int dspeed;
} dart_speeds[] = {
	{ B0,		0	},	/* 0 baud, special HUP condition */
        { B50,		NOBAUD	},	/* 50 baud, not implemented */
	{ B75,		BD75	},	/* 75 baud */
	{ B110,		BD110	},	/* 110 baud */
	{ B134,		BD134	},	/* 134.5 baud */
	{ B150,		BD150	},	/* 150 baud */
	{ B200,		NOBAUD	},	/* 200 baud, not implemented */
	{ B300,		BD300	},	/* 300 baud */
	{ B600,		BD600	},	/* 600 baud */
	{ B1200,	BD1200	},	/* 1200 baud */
	{ B1800,	BD1800	},	/* 1800 baud */
	{ B2400,	BD2400	},	/* 2400 baud */
	{ B4800,	BD4800	},	/* 4800 baud */
	{ B9600,	BD9600	},	/* 9600 baud */
	{ B19200,	BD19200	},	/* 19200 baud */
	{ -1,		NOBAUD	},	/* anything more is uncivilized */
};

int
dart_speed(int speed)
{
	const struct dart_s *ds;

	for (ds = dart_speeds; ds->kspeed != -1; ds++)
		if (ds->kspeed == speed)
			return ds->dspeed;

	return NOBAUD;
}

struct tty *
darttty(dev_t dev)
{
	unsigned int port;
	struct dartsoftc *sc;

	port = DART_PORT(dev);
	if (dart_cd.cd_ndevs == 0 || port >= NDARTPORTS)
		return (NULL);

	sc = (struct dartsoftc *)dart_cd.cd_devs[0];
	return sc->sc_dart[port].tty;
}

void
dartstart(struct tty *tp)
{
	struct dartsoftc *sc;
	dev_t dev;
	int s;
	int port, tries;
	int c;
	bus_addr_t ptaddr;

	dev = tp->t_dev;
	port = DART_PORT(dev);
	if (dart_cd.cd_ndevs == 0 || port >= NDARTPORTS)
		return;

	if ((tp->t_state & TS_ISOPEN) == 0)
		return;

	sc = (struct dartsoftc *)dart_cd.cd_devs[0];
	ptaddr = port ? DART_B_BASE : DART_A_BASE;

	s = spltty();

	if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP))
		goto bail;

	ttwakeupwr(tp);
	if (tp->t_outq.c_cc == 0)
		goto bail;

	tp->t_state |= TS_BUSY;
	while (tp->t_outq.c_cc != 0) {

		/* load transmitter until it is full */
		for (tries = 10000; tries != 0; tries --)
			if (dart_read(sc, ptaddr + DART_SRA) & TXRDY)
				break;

		if (tries == 0) {
			timeout_add(&tp->t_rstrt_to, 1);
			tp->t_state |= TS_TIMEOUT;
			break;
		} else {
			c = getc(&tp->t_outq);

			dart_write(sc, ptaddr + DART_TBA, c & 0xff);

			if (port == A_PORT)
				sc->sc_sv_reg.sv_imr |= ITXRDYA;
			else
				sc->sc_sv_reg.sv_imr |= ITXRDYB;
			dart_write(sc, DART_IMR, sc->sc_sv_reg.sv_imr);
		}
	}
	tp->t_state &= ~TS_BUSY;

bail:
	splx(s);
}

int
dartstop(struct tty *tp, int flag)
{
	int s;

	s = spltty();
	if (tp->t_state & TS_BUSY) {
		if ((tp->t_state & TS_TTSTOP) == 0)
			tp->t_state |= TS_FLUSH;
	}
	splx(s);

	return 0;
}

#define HANDLE_FLAG(_FLAG_, _PORT_, _AFLAG_, _BFLAG_) \
do { \
	if (flags & (_FLAG_)) { \
		newflags |= ((_PORT_) == A_PORT) ? (_AFLAG_) : (_BFLAG_); \
		flags &= ~(_FLAG_); \
	} \
} while (0)

/*
 * To be called at spltty - tty already locked.
 * Returns status of carrier.
 */

int
dartmctl(dev_t dev, int flags, int how)
{
	struct dartsoftc *sc;
	int port;
	int newflags = 0;
	struct dart_info *dart;
	int s;

	port = DART_PORT(dev);
	if (dart_cd.cd_ndevs == 0 || port >= NDARTPORTS)
		return (ENODEV);

	sc = (struct dartsoftc *)dart_cd.cd_devs[0];
	dart = &sc->sc_dart[port];

	s = spltty();

	HANDLE_FLAG(TIOCM_DTR, port, OPDTRA, OPDTRB);
	HANDLE_FLAG(TIOCM_RTS, port, OPRTSA, OPRTSB);

	switch (how) {
	case DMSET:
		dart_write(sc, DART_OPRS, newflags);
		dart_write(sc, DART_OPRR, ~newflags);
		break;
	case DMBIS:
		dart_write(sc, DART_OPRS, newflags);
		break;
	case DMBIC:
		dart_write(sc, DART_OPRR, newflags);
		break;
	case DMGET:
		flags = 0;	/* XXX not supported */
		break;
	}

	splx(s);
	return (flags);
}

int
dartioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	int error;
	int port;
	struct tty *tp;
	struct dart_info *dart;
	struct dartsoftc *sc;

	port = DART_PORT(dev);
	sc = (struct dartsoftc *)dart_cd.cd_devs[0];
	dart = &sc->sc_dart[port];

	tp = dart->tty;
	if (tp == NULL)
		return (ENXIO);

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return(error);

	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return(error);

	switch (cmd) {
	case TIOCSBRK:
	case TIOCCBRK:
		break;
	case TIOCSDTR:
		(void)dartmctl(dev, TIOCM_DTR | TIOCM_RTS, DMBIS);
		break;
	case TIOCCDTR:
		(void)dartmctl(dev, TIOCM_DTR | TIOCM_RTS, DMBIC);
		break;
	case TIOCMSET:
		(void)dartmctl(dev, *(int *) data, DMSET);
		break;
	case TIOCMBIS:
		(void)dartmctl(dev, *(int *) data, DMBIS);
		break;
	case TIOCMBIC:
		(void)dartmctl(dev, *(int *) data, DMBIC);
		break;
	case TIOCMGET:
		*(int *)data = dartmctl(dev, 0, DMGET);
		break;
	case TIOCGFLAGS:
		if (CONS_PORT == port)
			dart->dart_swflags |= TIOCFLAG_SOFTCAR;
		*(int *)data = dart->dart_swflags;
		break;
	case TIOCSFLAGS:
		error = suser(p, 0);
		if (error != 0)
			return (EPERM);

		dart->dart_swflags = *(int *)data;
		if (CONS_PORT == port)
			dart->dart_swflags |= TIOCFLAG_SOFTCAR;
		dart->dart_swflags &= /* only allow valid flags */
			(TIOCFLAG_SOFTCAR | TIOCFLAG_CLOCAL | TIOCFLAG_CRTSCTS);
		break;
	default:
		return (ENOTTY);
	}

	return (0);
}

int
dartparam(struct tty *tp, struct termios *t)
{
	int flags;
	int port;
	int speeds;
	unsigned char mr1, mr2;
	struct dart_info *dart;
	struct dartsoftc *sc;
	dev_t dev;
	bus_addr_t ptaddr;

	dev = tp->t_dev;
	sc = (struct dartsoftc *)dart_cd.cd_devs[0];
	port = DART_PORT(dev);
	dart = &sc->sc_dart[port];
	ptaddr = port ? DART_B_BASE : DART_A_BASE;

	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;

	flags = tp->t_flags;

	/* Reset to make global changes*/
	/* disable Tx and Rx */

	if (CONS_PORT != port) {
		if (port == A_PORT)
			sc->sc_sv_reg.sv_imr &= ~(ITXRDYA | IRXRDYA);
		else
			sc->sc_sv_reg.sv_imr &= ~(ITXRDYB | IRXRDYB);
		dart_write(sc, DART_IMR, sc->sc_sv_reg.sv_imr);

		/* hang up on zero baud rate */
		if (tp->t_ispeed == 0) {
			dartmctl(dev, HUPCL, DMSET);
			return (0);
		} else {
			/* set baudrate */
			speeds = dart_speed(tp->t_ispeed);
			if (speeds == NOBAUD)
				speeds = sc->sc_sv_reg.sv_csr[port];
			dart_write(sc, ptaddr + DART_CSRA, speeds);
			sc->sc_sv_reg.sv_csr[port] = speeds;
		}

		/* get saved mode registers and clear set up parameters */
		mr1 = sc->sc_sv_reg.sv_mr1[port];
		mr1 &= ~(CLMASK | PARTYPEMASK | PARMODEMASK);

		mr2 = sc->sc_sv_reg.sv_mr2[port];
		mr2 &= ~SBMASK;

		/* set up character size */
		if (flags & CS8)
			mr1 |= CL8;
		else if (tp->t_ispeed == B134)
			mr1 |= CL6;
		else
			mr1 |= CL7;

		/* set up stop bits */
		if (tp->t_ospeed == B110)
			mr2 |= SB2;
		else
			mr2 |= SB1;

		/* set up parity */
		if (((flags & PARENB) != PARENB) &&
		    (flags & PARENB)) {
			mr1 |= PAREN;
			if (flags & PARODD)
				mr1 |= ODDPAR;
			else
				mr1 |= EVENPAR;
		} else
			mr1 |= PARDIS;

		if (sc->sc_sv_reg.sv_mr1[port] != mr1 ||
		    sc->sc_sv_reg.sv_mr2[port] != mr2) {
			/* write mode registers to duart */
			dart_write(sc, ptaddr + DART_CRA, MRRESET);
			dart_write(sc, ptaddr + DART_MR1A, mr1);
			dart_write(sc, ptaddr + DART_MR2A, mr2);

			/* save changed mode registers */
			sc->sc_sv_reg.sv_mr1[port] = mr1;
			sc->sc_sv_reg.sv_mr2[port] = mr2;
		}
	}

	/* enable transmitter? */
	if (tp->t_state & TS_BUSY) {
		if (port == A_PORT)
			sc->sc_sv_reg.sv_imr |= ITXRDYA;
		else
			sc->sc_sv_reg.sv_imr |= ITXRDYB;
		dart_write(sc, DART_IMR, sc->sc_sv_reg.sv_imr);
	}

	/* re-enable the receiver */
	DELAY_CR;
	if (port == A_PORT)
		sc->sc_sv_reg.sv_imr |= IRXRDYA;
	else
		sc->sc_sv_reg.sv_imr |= IRXRDYB;
	dart_write(sc, DART_IMR, sc->sc_sv_reg.sv_imr);

	return (0);
}

void
dartmodemtrans(struct dartsoftc *sc, unsigned int ip, unsigned int ipcr)
{
	unsigned int dcdstate;
	struct tty *tp;
	int port;
	struct dart_info *dart;

	/* input is inverted at port!!! */
	if (ipcr & IPCRDCDA) {
		port = A_PORT;
		dcdstate = !(ip & IPDCDA);
	} else if (ipcr & IPCRDCDB) {
		port = B_PORT;
		dcdstate = !(ip & IPDCDB);
	} else {
		printf("dartmodemtrans: unknown transition ip=0x%x ipcr=0x%x\n",
		       ip, ipcr);
		return;
	}

	dart = &sc->sc_dart[port];
	tp = dart->tty;
	if (tp != NULL)
		ttymodem(tp, dcdstate);
}

int
dartopen(dev_t dev, int flag, int mode, struct proc *p)
{
	int s, port;
	struct dart_info *dart;
	struct dartsoftc *sc;
	struct tty *tp;

	port = DART_PORT(dev);
	if (dart_cd.cd_ndevs == 0 || port >= NDARTPORTS)
		return (ENODEV);
	sc = (struct dartsoftc *)dart_cd.cd_devs[0]; /* the only one */
	dart = &sc->sc_dart[port];

	s = spltty();
	if (dart->tty != NULL)
		tp = dart->tty;
	else
		tp = dart->tty = ttymalloc(0);

	tp->t_oproc = dartstart;
	tp->t_param = dartparam;
	tp->t_dev = dev;

	if ((tp->t_state & TS_ISOPEN) == 0) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = B9600;
		dartparam(tp, &tp->t_termios);
		if (port == CONS_PORT) {
			/* console is 8N1 */
			tp->t_cflag = (CREAD | CS8 | HUPCL);
		} else {
			tp->t_cflag = TTYDEF_CFLAG;
		}
		ttsetwater(tp);
		(void)dartmctl(dev, TIOCM_DTR | TIOCM_RTS, DMSET);
		tp->t_state |= TS_CARR_ON;
	} else if (tp->t_state & TS_XCLUDE && suser(p, 0) != 0) {
		splx(s);
		return (EBUSY);
	}

	/*
	 * Reset the tty pointer, as there could have been a dialout
	 * use of the tty with a dialin open waiting.
	 */
	tp->t_dev = dev;
	splx(s);
	return ((*linesw[tp->t_line].l_open)(dev, tp, p));
}

int
dartclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct tty *tp;
	struct dart_info *dart;
	struct dartsoftc *sc;
	int port;

	sc = (struct dartsoftc *)dart_cd.cd_devs[0];
	port = DART_PORT(dev);
	dart = &sc->sc_dart[port];

	tp = dart->tty;
	(*linesw[tp->t_line].l_close)(tp, flag, p);
	ttyclose(tp);

	return (0);
}

int
dartread(dev_t dev, struct uio *uio, int flag)
{
	int port;
	struct tty *tp;
	struct dart_info *dart;
	struct dartsoftc *sc;

	sc = (struct dartsoftc *)dart_cd.cd_devs[0];
	port = DART_PORT(dev);
	dart = &sc->sc_dart[port];

	tp = dart->tty;
	if (tp == NULL)
		return (ENXIO);
	return ((*linesw[tp->t_line].l_read)(tp, uio, flag));
}

int
dartwrite(dev_t dev, struct uio *uio, int flag)
{
	int port;
	struct tty *tp;
	struct dart_info *dart;
	struct dartsoftc *sc;

	sc = (struct dartsoftc *)dart_cd.cd_devs[0];
	port = DART_PORT(dev);
	dart = &sc->sc_dart[port];

	tp = dart->tty;
	if (tp == NULL)
		return (ENXIO);
	return ((*linesw[tp->t_line].l_write)(tp, uio, flag));
}

void
dartrint(struct dartsoftc *sc, int port)
{
	struct tty *tp;
	unsigned char data, sr;
	struct dart_info *dart;
	bus_addr_t ptaddr;

	dart = &sc->sc_dart[port];
	ptaddr = port ? DART_B_BASE : DART_A_BASE;
	tp = dart->tty;

	/* read status reg */
	while ((sr = dart_read(sc, ptaddr + DART_SRA)) & RXRDY) {
		/* read data and reset receiver */
		data = dart_read(sc, ptaddr + DART_RBA);

		if ((tp->t_state & (TS_ISOPEN|TS_WOPEN)) == 0 &&
		    CONS_PORT != port) {
			return;
		}

		if (sr & RBRK) {
			/* clear break state */
			dart_write(sc, ptaddr + DART_CRA, BRKINTRESET);
			DELAY_CR;
			dart_write(sc, ptaddr + DART_CRA, ERRRESET);

#if defined(DDB)
			if (db_console != 0 && port == CONS_PORT)
				Debugger();
#endif
		} else {
			if (sr & (FRERR|PERR|ROVRN)) { /* errors */
				if (sr & ROVRN)
					printf("%s: receiver overrun port %c\n",
					    sc->sc_dev.dv_xname, 'A' + port);
				if (sr & FRERR)
					printf("%s: framing error port %c\n",
					    sc->sc_dev.dv_xname, 'A' + port);
				if (sr & PERR)
					printf("%s: parity error port %c\n",
					    sc->sc_dev.dv_xname, 'A' + port);
				/* clear error state */
				dart_write(sc, ptaddr + DART_CRA, ERRRESET);
			} else {
				/* no errors */
				(*linesw[tp->t_line].l_rint)(data,tp);
#if 0
				{
					if (tp->t_ispeed == B134) /* CS6 */
						data &= 077;
					else if (tp->t_flags & CS8)
						;
					else
						data &= 0177; /* CS7 */
					ttyinput(data, tp);
				}
#endif
			}
		}
	}
}

void
dartxint(struct dartsoftc *sc, int port)
{
	struct tty *tp;
	struct dart_info *dart;

	dart = &sc->sc_dart[port];
	tp = dart->tty;

	if ((tp->t_state & (TS_ISOPEN|TS_WOPEN))==0)
		goto out;

	if (tp->t_state & TS_FLUSH)
		tp->t_state &= ~TS_FLUSH;

	if (tp->t_state & TS_BUSY) {
		tp->t_state &= ~TS_BUSY;
		dartstart(tp);
		if (tp->t_state & TS_BUSY) {
			return;
		}
	}
out:

	/* disable transmitter */
	if (port == 0)
		sc->sc_sv_reg.sv_imr &= ~ITXRDYA;
	else
		sc->sc_sv_reg.sv_imr &= ~ITXRDYB;

	dart_write(sc, DART_IMR, sc->sc_sv_reg.sv_imr);
}

int
dartintr(void *arg)
{
	struct dartsoftc *sc = arg;
	unsigned char isr, imr;
	int port;

	/* read interrupt status register and mask with imr */
	isr = dart_read(sc, DART_ISR);
	imr = sc->sc_sv_reg.sv_imr;

	if ((isr & imr) == 0) {
		/*
		 * We got an interrupt on a disabled condition (such as TX
		 * ready change on a disabled port). This should not happen,
		 * but we have to claim the interrupt anyway.
		 */
#if defined(DIAGNOSTIC) && !defined(MULTIPROCESSOR)
		printf("dartintr: spurious interrupt, isr %x imr %x\n",
		    isr, imr);
#endif
		return (1);
	}
	isr &= imr;

	if (isr & IIPCHG) {
		unsigned int ip, ipcr;

		ip = dart_read(sc, DART_IP);
		ipcr = dart_read(sc, DART_IPCR);
		dartmodemtrans(sc, ip, ipcr);
		return (1);
	}

	if (isr & (IRXRDYA | ITXRDYA))
		port = 0;
	else if (isr & (IRXRDYB | ITXRDYB))
		port = 1;
	else {
		printf("dartintr: spurious interrupt, isr 0x%08x\n", isr);
		return (1);	/* claim it anyway */
	}

	if (isr & (IRXRDYA | IRXRDYB)) {
		dartrint(sc, port);
	}
	if (isr & (ITXRDYA | ITXRDYB)) {
		dartxint(sc, port);
	}
	if ((port == A_PORT && (isr & IBRKA)) ||
	    (port == B_PORT && (isr & IBRKB))) {
		dart_write(sc, port ? DART_CRB : DART_CRA, BRKINTRESET);
	}

	return (1);
}

/*
 * Console interface routines.
 * Since we select the actual console after all devices are attached,
 * we can safely pick the appropriate softc and use its information.
 */

void
dartcnprobe(struct consdev *cp)
{
	int maj;

	if (brdtyp != BRD_188 || badaddr(DART_BASE, 4) != 0)
		return;

	/* do not attach as console if dart has been disabled */
	if (dart_cd.cd_ndevs == 0 || dart_cd.cd_devs[0] == NULL)
		return;

	/* locate the major number */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == dartopen)
			break;
	if (maj == nchrdev)
		return;

	cp->cn_dev = makedev(maj, CONS_PORT);
	cp->cn_pri = CN_LOWPRI;
}

void
dartcninit(cp)
	struct consdev *cp;
{
}

void
dartcnputc(dev_t dev, int c)
{
	struct dartsoftc *sc;
	int s;
	int port;
	bus_addr_t ptaddr;

	sc = (struct dartsoftc *)dart_cd.cd_devs[0];
	port = DART_PORT(dev);
	ptaddr = port ? DART_B_BASE : DART_A_BASE;

	s = spltty();

	/* inhibit interrupts on the chip */
	dart_write(sc, DART_IMR, sc->sc_sv_reg.sv_imr & ~ITXRDYA);
	/* make sure transmitter is enabled */
	DELAY_CR;
	dart_write(sc, ptaddr + DART_CRA, TXEN);

	while ((dart_read(sc, ptaddr + DART_SRA) & TXRDY) == 0)
		;
	dart_write(sc, ptaddr + DART_TBA, c);

	/* wait for transmitter to empty */
	while ((dart_read(sc, ptaddr + DART_SRA) & TXEMT) == 0)
		;

	/* restore the previous state */
	dart_write(sc, DART_IMR, sc->sc_sv_reg.sv_imr);
	DELAY_CR;
	dart_write(sc, ptaddr + DART_CRA, sc->sc_sv_reg.sv_cr[0]);

	splx(s);
}

int
dartcngetc(dev_t dev)
{
	struct dartsoftc *sc;
	unsigned char sr;	/* status reg of port a/b */
	u_char c;		/* received character */
	int s;
	int port;
	bus_addr_t ptaddr;

	sc = (struct dartsoftc *)dart_cd.cd_devs[0];
	port = DART_PORT(dev);
	ptaddr = port ? DART_B_BASE : DART_A_BASE;

	s = spltty();

	/* enable receiver */
	dart_write(sc, ptaddr + DART_CRA, RXEN);

	for (;;) {
		/* read status reg */
		sr = dart_read(sc, ptaddr + DART_SRA);

		/* receiver interrupt handler*/
		if (sr & RXRDY) {
			/* read character from port */
			c = dart_read(sc, ptaddr + DART_RBA);

			/* check break condition */
			if (sr & RBRK) {
				/* clear break state */
				dart_write(sc, ptaddr + DART_CRA, BRKINTRESET);
				DELAY_CR;
				dart_write(sc, ptaddr + DART_CRA, ERRRESET);
				break;
			}

			if (sr & (FRERR | PERR | ROVRN)) {
				/* clear error state */
				dart_write(sc, ptaddr + DART_CRA, ERRRESET);
				DELAY_CR;
				dart_write(sc, ptaddr + DART_CRA, BRKINTRESET);
			} else {
				break;
			}
		}
	}
	splx(s);

	return ((int)c);
}
