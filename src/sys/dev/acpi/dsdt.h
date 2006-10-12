/* $OpenBSD: dsdt.h,v 1.15 2006/10/12 23:16:11 jordan Exp $ */
/*
 * Copyright (c) 2005 Marco Peereboom <marco@openbsd.org>
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

#ifndef __DEV_ACPI_DSDT_H__
#define __DEV_ACPI_DSDT_H__

struct acpi_context
{
	int			depth;
	u_int8_t		*pos;
	u_int8_t		*start;
	struct acpi_softc	*sc;
	struct aml_value	**locals;
	struct aml_value	**args;
	struct aml_node		*scope;
};

struct aml_opcode
{
	u_int32_t		opcode;
	const char		*mnem;
	const char		*args;
};

const char		*aml_eisaid(u_int32_t);
const char		*aml_mnem(int);
const char		*aml_parse_name(struct acpi_context *);
int			aml_parse_length(struct acpi_context *);
int64_t			aml_eparseint(struct acpi_context *, int);
int64_t			aml_val2int(struct aml_value *);
struct aml_node		*aml_searchname(struct aml_node *, const void *);
struct aml_node         *aml_createname(struct aml_node *, const void *, struct aml_value *);

struct aml_opcode	*aml_getopcode(struct acpi_context *);
struct aml_value	*aml_allocint(uint64_t);
struct aml_value	*aml_allocstr(const char *);
struct aml_value	*aml_allocvalue(int, int64_t, const void *);
u_int8_t		*aml_eparselen(struct acpi_context *);
void			acpi_freecontext(struct acpi_context *);
void			aml_freevalue(struct aml_value *);
void			aml_notify(struct aml_node *, int);
void			aml_notify_dev(const char *, int);
void			aml_showvalue(struct aml_value *);
void			aml_walkroot(void);
void			aml_walktree(struct aml_node *);

int			aml_comparevalue(struct acpi_context *, int,
			    struct aml_value *, struct aml_value *);
int			aml_find_node(struct aml_node *, const char *,
			    void (*)(struct aml_node *, void *), void *);
int			acpi_parse_aml(struct acpi_softc *, u_int8_t *,
			    u_int32_t);
int			aml_eval_object(struct acpi_softc *, struct aml_node *,
			    struct aml_value *, int, struct aml_value *);
struct acpi_context	*acpi_alloccontext(struct acpi_softc *,
			    struct aml_node *, int, struct aml_value *);
void			aml_register_notify(struct aml_node *, const char *,
			    int (*)(struct aml_node *, int, void *), void *);

u_int64_t aml_getpciaddr(struct acpi_softc *, struct aml_node *);

int aml_evalnode(struct acpi_softc *, struct aml_node *,
		 int , struct aml_value *,
		 struct aml_value *);

int aml_evalname(struct acpi_softc *, struct aml_node *, 
		 const char *, int, struct aml_value *,
		 struct aml_value *);

void aml_fixup_dsdt(u_int8_t *, u_int8_t *, int);
void aml_create_defaultobjects(void);

#define ACPI_E_NOERROR   0x00
#define ACPI_E_BADVALUE  0x01

#endif /* __DEV_ACPI_DSDT_H__ */
