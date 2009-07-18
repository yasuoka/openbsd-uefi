/*	$OpenBSD: midi.c,v 1.17 2009/07/18 10:58:41 ratchov Exp $	*/

/*
 * Copyright (c) 2003, 2004 Alexandre Ratchov
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * TODO
 *	- put the sequencer stuff in sequencer.c and sequencervar.h
 *	  there is no reason to have it here. The sequencer
 *	  driver need only to open the midi hw_if thus it does not 
 *	  need this driver 
 */

#include "midi.h"
#include "sequencer.h"
#if NMIDI > 0

#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/systm.h>
#include <sys/ioctl.h>
#include <sys/exec.h>
#include <sys/conf.h>
#include <sys/lkm.h>
#include <sys/proc.h>
#include <sys/poll.h>
#include <sys/kernel.h>
#include <sys/timeout.h>
#include <sys/vnode.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <dev/midi_if.h>
#include <dev/audio_if.h>
#include <dev/midivar.h>


int     midiopen(dev_t, int, int, struct proc *);
int     midiclose(dev_t, int, int, struct proc *);
int     midiread(dev_t, struct uio *, int);
int     midiwrite(dev_t, struct uio *, int);
int     midipoll(dev_t, int, struct proc *);
int	midiioctl(dev_t, u_long, caddr_t, int, struct proc *);
int	midiprobe(struct device *, void *, void *);
void	midiattach(struct device *, struct device *, void *);
int	mididetach(struct device *, int);
int	midiprint(void *, const char *);

void	midi_iintr(void *, int);
void 	midi_ointr(void *);
void	midi_out_start(struct midi_softc *);
void	midi_out_stop(struct midi_softc *);
void	midi_out_do(struct midi_softc *);
void	midi_attach(struct midi_softc *, struct device *);


#if NSEQUENCER > 0
int		   midi_unit_count(void);
struct midi_hw_if *midi_get_hwif(int);
void		   midi_toevent(struct midi_softc *, int);
int		   midi_writebytes(int, u_char *, int);
void		   midiseq_in(struct midi_dev *, u_char *, int);
#endif

struct cfattach midi_ca = {
	sizeof(struct midi_softc), midiprobe, midiattach, mididetach
};

struct cfdriver midi_cd = {
	NULL, "midi", DV_DULL
};


void
midi_iintr(void *addr, int data) 
{
	struct midi_softc  *sc = (struct midi_softc *)addr;
	struct midi_buffer *mb = &sc->inbuf;
	
	if (sc->isdying || !sc->isopen || !(sc->flags & FREAD)) return;
	
#if NSEQUENCER > 0
	if (sc->seqopen) {
		midi_toevent(sc, data);
		return;
	}
#endif
	if (MIDIBUF_ISFULL(mb))
		return; /* discard data */
	if (MIDIBUF_ISEMPTY(mb)) {
		if (sc->rchan) {
			sc->rchan = 0;
			wakeup(&sc->rchan);
		}	
		selwakeup(&sc->rsel);
		if (sc->async)
			psignal(sc->async, SIGIO);
	}
	MIDIBUF_WRITE(mb, data);
}


int
midiread(dev_t dev, struct uio *uio, int ioflag)
{
	struct midi_softc  *sc = MIDI_DEV2SC(dev);
	struct midi_buffer *mb = &sc->inbuf;
	unsigned 	    count;
	int		    s, error;
	
	if (!(sc->flags & FREAD))
		return ENXIO;
		
	/* if there is no data then sleep (unless IO_NDELAY flag is set) */

	s = splaudio();
	while(MIDIBUF_ISEMPTY(mb)) {
		if (sc->isdying) {
			splx(s);
			return EIO;
		}
		if (ioflag & IO_NDELAY) {
			splx(s);
			return EWOULDBLOCK;
		}
		sc->rchan = 1;
		error = tsleep(&sc->rchan, PWAIT|PCATCH, "mid_rd", 0);
		if (error) {
			splx(s);
			return error;			
		}
	}
	
	/* at this stage, there is at least 1 byte */

	while (uio->uio_resid > 0  &&  mb->used > 0) {
		count = MIDIBUF_SIZE - mb->start;
		if (count > mb->used) 
			count = mb->used;
		if (count > uio->uio_resid) 
			count = uio->uio_resid;
		error = uiomove(mb->data + mb->start, count, uio);
		if (error) {
			splx(s);
			return error;
		}
		MIDIBUF_REMOVE(mb, count);
	}
	splx(s);
	return 0;	
}


void 
midi_ointr(void *addr)
{
	struct midi_softc  *sc = (struct midi_softc *)addr;
	struct midi_buffer *mb;
	int 		   s;
	
	if (sc->isopen && !sc->isdying) {
#ifdef MIDI_DEBUG
		if (!sc->isbusy) {
			printf("midi_ointr: output should be busy\n");
		}
#endif
		mb = &sc->outbuf;
		s = splaudio();
		if (mb->used == 0)
			midi_out_stop(sc);
		else
			midi_out_do(sc); /* restart output */
		splx(s);
	}
}


void
midi_out_start(struct midi_softc *sc)
{
	if (!sc->isbusy) {
		sc->isbusy = 1;
		midi_out_do(sc);
	}
}

void
midi_out_stop(struct midi_softc *sc)
{
	sc->isbusy = 0;
	if (sc->wchan) {
		sc->wchan = 0;
		wakeup(&sc->wchan);
	}
	selwakeup(&sc->wsel);
	if (sc->async)
		psignal(sc->async, SIGIO);
}


	/*
	 * drain output buffer, must be called with
	 * interrupts disabled
	 */
void
midi_out_do(struct midi_softc *sc)
{
	struct midi_buffer *mb = &sc->outbuf;
	unsigned 	    i, max;
	int		    error;
	
	/*
	 * If output interrupts are not supported then we write MIDI_MAXWRITE
	 * bytes instead of 1, and then we wait sc->wait
	 */

	max = sc->props & MIDI_PROP_OUT_INTR ? 1 : MIDI_MAXWRITE;
	for (i = max; i != 0;) {
		if (mb->used == 0)
			break;
		error = sc->hw_if->output(sc->hw_hdl, mb->data[mb->start]);
		/*
		 * 0 means that data is being sent, an interrupt will 
		 * be generated when the interface becomes ready again
		 *
		 * EINPROGRESS means that data has been queued, but 
		 * will not be sent immediately and thus will not 
		 * generate interrupt, in this case we can send 
		 * another byte. The flush() method can be called
		 * to force the transfer.
		 *
		 * EAGAIN means that data cannot be queued or sent;
		 * because the interface isn't ready. An interrupt 
		 * will be generated once the interface is ready again
		 *
		 * any other (fatal) error code means that data couldn't 
		 * be sent and was lost, interrupt will not be generated
		 */
		if (error == EINPROGRESS) {
			MIDIBUF_REMOVE(mb, 1);
			if (MIDIBUF_ISEMPTY(mb)) {
				if (sc->hw_if->flush != NULL)
					sc->hw_if->flush(sc->hw_hdl);
				midi_out_stop(sc);
				return;
			}
		} else if (error == 0) {
			MIDIBUF_REMOVE(mb, 1);
			i--;
		} else if (error == EAGAIN) {
			break;
		} else {
			MIDIBUF_INIT(mb);
			midi_out_stop(sc);
			return;
		}
	}
	
	if (!(sc->props & MIDI_PROP_OUT_INTR)) {
		if (MIDIBUF_ISEMPTY(mb))
			midi_out_stop(sc);
		else
			timeout_add(&sc->timeo, sc->wait);
	}
}


int
midiwrite(dev_t dev, struct uio *uio, int ioflag)
{
	struct midi_softc  *sc = MIDI_DEV2SC(dev);
	struct midi_buffer *mb = &sc->outbuf;
	unsigned 	    count;
	int		    s, error;
	
	if (!(sc->flags & FWRITE))
		return ENXIO;
	if (sc->isdying)
		return EIO;

	/*
	 * If IO_NDELAY flag is set then check if there is enough room 
	 * in the buffer to store at least one byte. If not then dont 
	 * start the write process.
	 */

	if ((ioflag & IO_NDELAY) &&  MIDIBUF_ISFULL(mb)  &&
	    (uio->uio_resid > 0))
	    	return EWOULDBLOCK;
	
	while (uio->uio_resid > 0) {
		s = splaudio();
		while (MIDIBUF_ISFULL(mb)) {
			if (ioflag & IO_NDELAY) {
				/* 
				 * At this stage at least one byte is already
				 * moved so we do not return EWOULDBLOCK
				 */
				splx(s);
				return 0;
			}
			sc->wchan = 1;
			error = tsleep(&sc->wchan, PWAIT|PCATCH, "mid_wr", 0);
			if (error) {
				splx(s);
				return error;
			}
			if (sc->isdying) {
				splx(s);
				return EIO;
			}
		}
					
		count = MIDIBUF_SIZE - MIDIBUF_END(mb);
		if (count > MIDIBUF_AVAIL(mb))
			count = MIDIBUF_AVAIL(mb);
		if (count > uio->uio_resid) 
			count = uio->uio_resid;
		error = uiomove(mb->data + MIDIBUF_END(mb), count, uio);
		if (error) {
			splx(s);
			return error;
		}
		mb->used += count;
		midi_out_start(sc);
		splx(s);
	}
	return 0;
}


int
midipoll(dev_t dev, int events, struct proc *p)
{
	struct midi_softc *sc = MIDI_DEV2SC(dev);
	int		   s, revents;
	
	if (sc->isdying)
		return POLLERR;

	revents = 0;
	s = splaudio();
	if (events & (POLLIN | POLLRDNORM)) {
		if (!MIDIBUF_ISEMPTY(&sc->inbuf))
			revents |= events & (POLLIN | POLLRDNORM);
	}
	if (events & (POLLOUT | POLLWRNORM)) {
		if (!MIDIBUF_ISFULL(&sc->outbuf))
			revents |= events & (POLLOUT | POLLWRNORM);
	}
	if (revents == 0) {
		if (events & (POLLIN | POLLRDNORM))
			selrecord(p, &sc->rsel);
		if (events & (POLLOUT | POLLWRNORM))
			selrecord(p, &sc->wsel);
	}
	splx(s);
	return (revents);
}


int
midiioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	struct midi_softc *sc = MIDI_DEV2SC(dev);

	if (sc->isdying) return EIO;

	switch(cmd) {
	case FIONBIO:
		/* All handled in the upper FS layer */
		break;
	case FIOASYNC:
		if (*(int *)addr) {
			if (sc->async) return EBUSY;
			sc->async = p;
		} else
			sc->async = 0;
		break;
	default:
		return ENOTTY;
		break;
	}
	return 0;
}


int
midiopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct midi_softc *sc;
	int		   err;

	if (MIDI_UNIT(dev) >= midi_cd.cd_ndevs)
		return ENXIO;
	sc = MIDI_DEV2SC(dev);
	if (sc == NULL)		/* there may be more units than devices */
		return ENXIO;
	if (sc->isdying)
		return EIO;
	if (sc->isopen)
		return EBUSY;

	MIDIBUF_INIT(&sc->inbuf);
	MIDIBUF_INIT(&sc->outbuf);
	sc->isbusy = 0;
	sc->rchan = sc->wchan = 0;
	sc->async = 0;
	sc->flags = flags;

	err = sc->hw_if->open(sc->hw_hdl, flags, midi_iintr, midi_ointr, sc);
	if (err)
		return err;
	sc->isopen = 1;
#if NSEQUENCER > 0
	sc->seq_md = 0;
	sc->seqopen = 0;
	sc->evstatus = 0xff;
#endif
	return 0;
}


int 
midiclose(dev_t dev, int fflag, int devtype, struct proc *p)
{
	struct midi_softc  *sc = MIDI_DEV2SC(dev);
	struct midi_buffer *mb;
	int 		    error;
	int		    s;
	
	mb = &sc->outbuf;
	if (!sc->isdying) {
		/* start draining output buffer */
		s = splaudio();
		if (!MIDIBUF_ISEMPTY(mb))
			midi_out_start(sc);
		while (sc->isbusy) {
			sc->wchan = 1;
			error = tsleep(&sc->wchan, PWAIT|PCATCH, "mid_dr", 0);
			if (error || sc->isdying)
				break;
		}
		splx(s);
	}
	
	/* 
	 * some hw_if->close() reset immediately the midi uart
	 * which flushes the internal buffer of the uart device,
	 * so we may lose some (important) data. To avoid this, we sleep 2*wait,
	 * which gives the time to the uart to drain its internal buffers.
	 *
	 * Note: we'd better sleep in the corresponding hw_if->close()
	 */
	 
	tsleep(&sc->wchan, PWAIT|PCATCH, "mid_cl", 2 * sc->wait);
	sc->hw_if->close(sc->hw_hdl);
	sc->isopen = 0;
	return 0;
}


int
midiprobe(struct device *parent, void *match, void *aux)
{
	struct audio_attach_args *sa = aux;
	return (sa != NULL && (sa->type == AUDIODEV_TYPE_MIDI) ? 1 : 0);
}


void
midi_attach(struct midi_softc *sc, struct device *parent)
{
	struct midi_info 	  mi;
	
	sc->isdying = 0;
	sc->wait = (hz * MIDI_MAXWRITE) /  MIDI_RATE;
	if (sc->wait == 0) 
		sc->wait = 1;
	sc->hw_if->getinfo(sc->hw_hdl, &mi);
	sc->props = mi.props;
	sc->isopen = 0;
	timeout_set(&sc->timeo, midi_ointr, sc);
	printf(": <%s>\n", mi.name);
}


void
midiattach(struct device *parent, struct device *self, void *aux)
{
	struct midi_softc        *sc = (struct midi_softc *)self;
	struct audio_attach_args *sa = (struct audio_attach_args *)aux;
	struct midi_hw_if        *hwif = sa->hwif;
	void  			 *hdl = sa->hdl;
	
#ifdef DIAGNOSTIC
	if (hwif == 0 ||
	    hwif->open == 0 ||
	    hwif->close == 0 ||
	    hwif->output == 0 ||
	    hwif->getinfo == 0) {
		printf("midi: missing method\n");
		return;
	}
#endif
	sc->hw_if = hwif;
	sc->hw_hdl = hdl;
	midi_attach(sc, parent);
}


int
mididetach(struct device *self, int flags)
{
	struct midi_softc *sc = (struct midi_softc *)self;
	int    maj, mn;
	
	sc->isdying = 1;
	if (sc->wchan) {
		sc->wchan = 0;
		wakeup(&sc->wchan);
	}
	if (sc->rchan) {
		sc->rchan = 0;
		wakeup(&sc->rchan);
	}
	
	/* locate the major number */
        for (maj = 0; maj < nchrdev; maj++) {
                if (cdevsw[maj].d_open == midiopen) {
        		/* Nuke the vnodes for any open instances (calls close). */
        		mn = self->dv_unit;
        		vdevgone(maj, mn, mn, VCHR);
		}
	}
	return 0;
}


int
midiprint(void *aux, const char *pnp)
{
	if (pnp)
		printf("midi at %s", pnp);
	return (UNCONF);
}


void
midi_getinfo(dev_t dev, struct midi_info *mi)
{
	struct midi_softc *sc = MIDI_DEV2SC(dev);
	if (MIDI_UNIT(dev) >= midi_cd.cd_ndevs || sc == NULL || sc->isdying) {
		mi->name = "unconfigured";
		mi->props = 0;
		return;
	}
	sc->hw_if->getinfo(sc->hw_hdl, mi);
}


struct device *
midi_attach_mi(struct midi_hw_if *hwif, void *hdl, struct device *dev)
{
	struct audio_attach_args arg;

	arg.type = AUDIODEV_TYPE_MIDI;
	arg.hwif = hwif;
	arg.hdl = hdl;
	return config_found(dev, &arg, midiprint);
}


int
midi_unit_count(void)
{
	return midi_cd.cd_ndevs;
}


#if NSEQUENCER > 0
#define MIDI_EVLEN(status) 	(midi_evlen[((status) >> 4) & 7])
unsigned midi_evlen[] = { 2, 2, 2, 2, 1, 1, 2 };

void
midi_toevent(struct midi_softc *sc, int data)
{
	unsigned char mesg[3];
	
	if (data >= 0xf8) {		/* is it a realtime message ? */
		switch(data) {
		case 0xf8:		/* midi timer tic */
		case 0xfa:		/* midi timer start */
		case 0xfb:		/* midi timer continue (after stop) */
		case 0xfc:		/* midi timer stop */
			mesg[0] = data;
			midiseq_in(sc->seq_md, mesg, 1);
			break;
		default:
			break;
		}
	} else if (data >= 0x80) {	/* is it a common or voice message ? */
		sc->evstatus = data;
		sc->evindex = 0;
	} else {			/* else it is a data byte */	
		/* strip common messages and bogus data */
		if (sc->evstatus >= 0xf0 || sc->evstatus < 0x80)
			return;

		sc->evdata[sc->evindex++] = data;
		if (sc->evindex == MIDI_EVLEN(sc->evstatus)) {
			sc->evindex = 0;
			mesg[0] = sc->evstatus;
			mesg[1] = sc->evdata[0];
			mesg[2] = sc->evdata[1];
			midiseq_in(sc->seq_md, mesg, 1 + MIDI_EVLEN(sc->evstatus));
		}
	}
}


int
midi_writebytes(int unit, unsigned char *mesg, int mesglen)
{
	struct midi_softc  *sc = midi_cd.cd_devs[unit];
	struct midi_buffer *mb = &sc->outbuf;
	unsigned 	    count;
	int		    s;
	
	s = splaudio();
	if (mesglen > MIDIBUF_AVAIL(mb)) {
		splx(s);
		return EWOULDBLOCK;
	}
	
	while (mesglen > 0) {
		count = MIDIBUF_SIZE - MIDIBUF_END(mb);
		if (count > MIDIBUF_AVAIL(mb)) count = MIDIBUF_AVAIL(mb);
		if (count > mesglen) count = mesglen;
		bcopy(mesg, mb->data + MIDIBUF_END(mb), count);
		mb->used += count;
		mesg += count;
		mesglen -= count;
		midi_out_start(sc);
	}
	splx(s);
	return 0;
}

#endif /* NSEQUENCER > 0 */
#endif /* NMIDI > 0 */
