/*	$OpenBSD: linux_cdrom.c,v 1.1 1997/12/07 22:59:14 provos Exp $	*/
/*
 * Copyright 1997 Niels Provos <provos@physnet.uni-hamburg.de>
 * All rights reserved.
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
 *      This product includes software developed by Niels Provos.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/cdio.h>

#include <sys/syscallargs.h>

#include <compat/linux/linux_types.h>
#include <compat/linux/linux_ioctl.h>
#include <compat/linux/linux_signal.h>
#include <compat/linux/linux_syscallargs.h>
#include <compat/linux/linux_util.h>
#include <compat/linux/linux_cdrom.h>

void bsd_addr_to_linux_addr __P((union msf_lba *bsd, union linux_cdrom_addr *linux, int format));

void 
bsd_addr_to_linux_addr(bsd, linux, format)
        union msf_lba *bsd;
        union linux_cdrom_addr *linux;
        int format;
{
        if (format == CD_MSF_FORMAT) {
 	        linux->msf.minute = bsd->msf.minute;
		linux->msf.second = bsd->msf.second;
		linux->msf.frame = bsd->msf.frame;
	} else 
	        linux->lba = bsd->lba;
}

int
linux_ioctl_cdrom(p, uap, retval)
	register struct proc *p;
	register struct linux_sys_ioctl_args /* {
		syscallarg(int) fd;
		syscallarg(u_long) com;
		syscallarg(caddr_t) data;
	} */ *uap;
	register_t *retval;
{
	register struct file *fp;
	register struct filedesc *fdp;
	caddr_t sg;
	u_long com;
	struct sys_ioctl_args ia;
	int error;

	union {
	        struct cd_toc_entry te;
	        struct cd_sub_channel_info scinfo;
	} data;
	union {
	        struct ioc_toc_header th;
	        struct ioc_read_toc_entry tes;
	        struct ioc_play_track ti;
	        struct ioc_read_subchannel sc;
	} tmpb;
	union {
	        struct linux_cdrom_tochdr th;
	        struct linux_cdrom_tocentry te;
	        struct linux_cdrom_ti ti;
	        struct linux_cdrom_subchnl sc;
	} tmpl;


	fdp = p->p_fd;
	if ((u_int)SCARG(uap, fd) >= fdp->fd_nfiles ||
	    (fp = fdp->fd_ofiles[SCARG(uap, fd)]) == NULL)
		return (EBADF);

	if ((fp->f_flag & (FREAD | FWRITE)) == 0)
		return (EBADF);

	com = SCARG(uap, com);
	retval[0] = 0;
                
	switch (com) {
	case LINUX_CDROMREADTOCHDR:
	        error = (*fp->f_ops->fo_ioctl)(fp, CDIOREADTOCHEADER, (caddr_t)&tmpb.th, p);
	        if (error)
		        return error;
		tmpl.th.cdth_trk0 = tmpb.th.starting_track;
		tmpl.th.cdth_trk1 = tmpb.th.ending_track;
		error = copyout(&tmpl, SCARG(uap, data), sizeof tmpl.th);
		if (error)
			return error;
		return 0;
	case LINUX_CDROMREADTOCENTRY:
		error = copyin(SCARG(uap, data), &tmpl.te, sizeof tmpl.te);
		if (error)
		        return error;

		sg = stackgap_init(p->p_emul);
		
		bzero(&tmpb.tes, sizeof tmpb.tes);
		tmpb.tes.starting_track = tmpl.te.cdte_track;
		tmpb.tes.address_format = tmpl.te.cdte_format == LINUX_CDROM_MSF ? CD_MSF_FORMAT : CD_LBA_FORMAT;
		tmpb.tes.data_len = sizeof(struct cd_toc_entry);
		tmpb.tes.data = stackgap_alloc(&sg, tmpb.tes.data_len);

	        error = (*fp->f_ops->fo_ioctl)(fp, CDIOREADTOCENTRYS, (caddr_t)&tmpb.tes, p);
	        if (error) 
		        return error;
		if ((error = copyin(tmpb.tes.data, &data.te, sizeof data.te)))
		        return error;
		
		tmpl.te.cdte_ctrl = data.te.control;
		tmpl.te.cdte_adr = data.te.addr_type;
		tmpl.te.cdte_track = data.te.track;
		tmpl.te.cdte_datamode = CD_TRACK_INFO;
		bsd_addr_to_linux_addr(&data.te.addr, &tmpl.te.cdte_addr, 
				       tmpb.tes.address_format);
		error = copyout(&tmpl, SCARG(uap, data), sizeof tmpl.te);
		if (error)
			return error;
		return 0;
	case LINUX_CDROMSUBCHNL:
		error = copyin(SCARG(uap, data), &tmpl.sc, sizeof tmpl.sc);
		if (error)
		        return error;

		sg = stackgap_init(p->p_emul);
		
		bzero(&tmpb.sc, sizeof tmpb.sc);
		tmpb.sc.data_format = CD_CURRENT_POSITION;
		tmpb.sc.address_format = tmpl.sc.cdsc_format == LINUX_CDROM_MSF ? CD_MSF_FORMAT : CD_LBA_FORMAT;
		tmpb.sc.data_len = sizeof(struct cd_sub_channel_info);
		tmpb.sc.data = stackgap_alloc(&sg, tmpb.sc.data_len);

	        error = (*fp->f_ops->fo_ioctl)(fp, CDIOCREADSUBCHANNEL, (caddr_t)&tmpb.sc, p);
	        if (error)
		        return error;
		if ((error = copyin(tmpb.sc.data, &data.scinfo, sizeof data.scinfo)))
		        return error;
		
		tmpl.sc.cdsc_audiostatus = data.scinfo.header.audio_status;
		tmpl.sc.cdsc_adr = data.scinfo.what.position.addr_type;
		tmpl.sc.cdsc_ctrl = data.scinfo.what.position.control;
		tmpl.sc.cdsc_trk = data.scinfo.what.position.track_number;
		tmpl.sc.cdsc_ind = data.scinfo.what.position.index_number;
		bsd_addr_to_linux_addr(&data.scinfo.what.position.absaddr, 
				       &tmpl.sc.cdsc_absaddr, 
				       tmpb.sc.address_format);
		bsd_addr_to_linux_addr(&data.scinfo.what.position.reladdr, 
				       &tmpl.sc.cdsc_reladdr, 
				       tmpb.sc.address_format);

		error = copyout(&tmpl, SCARG(uap, data), sizeof tmpl.sc);
		if (error)
			return error;
		return 0;
	case LINUX_CDROMPLAYTRKIND:
		error = copyin(SCARG(uap, data), &tmpl.ti, sizeof tmpl.ti);
		if (error)
		        return error;

		tmpb.ti.start_track = tmpl.ti.cdti_trk0;
		tmpb.ti.start_index = tmpl.ti.cdti_ind0;
		tmpb.ti.end_track = tmpl.ti.cdti_trk1;
		tmpb.ti.end_index = tmpl.ti.cdti_ind1;
	        error = (*fp->f_ops->fo_ioctl)(fp, CDIOCPLAYTRACKS, (caddr_t)&tmpb.ti, p);
	        if (error)
		        return error;
		return 0;
	case LINUX_CDROMPAUSE:
		SCARG(&ia, com) = CDIOCPAUSE;
		break;
	case LINUX_CDROMRESUME:
		SCARG(&ia, com) = CDIOCRESUME;
		break;
	case LINUX_CDROMSTOP:
		SCARG(&ia, com) = CDIOCSTOP;
		break;
	case LINUX_CDROMSTART:
		SCARG(&ia, com) = CDIOCSTART;
		break;
	default:
	        printf("linux_ioctl_cdrom: invalid ioctl %08lx\n", com);
		return EINVAL;
	}

	SCARG(&ia, fd) = SCARG(uap, fd);
	SCARG(&ia, data) = SCARG(uap, data);
	return sys_ioctl(p, &ia, retval);
}
