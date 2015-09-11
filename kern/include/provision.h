/*
 * Copyright (c) 2015 The Regents of the University of California
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifndef	PROVISION_H
#define	PROVISION_H

#include <sys/queue.h>
#include <arch/topology.h>

struct proc;
void provision_unprov_proc(struct proc *p);
void provision_prov_core(struct proc *p, uint32_t pcoreid);

#endif

