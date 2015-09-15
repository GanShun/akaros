/*
 * Copyright (c) 2015 The Regents of the University of California
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 *
 * The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <core_request.h>
#include <stdio.h>
#include <string.h>
#include <kmalloc.h>
#include <arch/topology.h>
#include <process.h>

/* The pcores in the system.  (array gets alloced in init()).  */
struct sched_pcore *all_pcores;
/* TAILQ of all unallocated, idle (CG) cores */
struct sched_pcore_tailq idlecores = TAILQ_HEAD_INITIALIZER(idlecores);

struct sched_pcore *corerequest_pcoreid2spc(uint32_t pcoreid)
{
	return &all_pcores[pcoreid];
}

uint32_t corerequest_spc2pcoreid(struct sched_pcore *spc)
{
	return spc - all_pcores;
}

void corerequest_nodes_init()
{
	all_pcores = kmalloc(sizeof(struct sched_pcore) * num_cores, 0);
	memset(all_pcores, 0, sizeof(struct sched_pcore) * num_cores);
	for (int i = 1; i < num_cores; i++) {
		struct sched_pcore *spc_i = corerequest_pcoreid2spc(i);
		spc_i->alloc_proc = NULL;
		spc_i->prov_proc = NULL;
		TAILQ_INSERT_TAIL(&idlecores, spc_i, alloc_next);
	}
	/* "Allocate" core 0 to the kernel */
	all_pcores[0].alloc_proc = -1;
}

struct sched_pcore *corerequest_alloc_core(struct proc *p)
{
	struct sched_pcore *spc_i = NULL;
	if (!TAILQ_EMPTY(&p->ksched_data.corealloc_data.prov_not_alloc_me))
		spc_i = TAILQ_FIRST(&p->ksched_data.corealloc_data.prov_not_alloc_me); 
	else
		spc_i = TAILQ_FIRST(&idlecores);
	return spc_i;
}

void corerequest_fcfs_track_alloc(struct proc *p, struct sched_pcore *c)
{
	__track_alloc(p, c);
	TAILQ_REMOVE(&idlecores, c, alloc_next);
}

void corerequest_track_dealloc(struct proc *p, uint32_t core_id)
{
	struct sched_pcore *c = &all_pcores[core_id];	
	__track_dealloc(p, c);
	TAILQ_INSERT_TAIL(&idlecores, c, alloc_next);
}

int corerequest_get_any_core()
{
	struct sched_pcore *spc_i = NULL;
	spc_i = TAILQ_FIRST(&idlecores); 
	if (spc_i == NULL)
		return -1;
	else
		return corerequest_spc2pcoreid(spc_i);
}

void corerequest_register_proc(struct proc *p)
{
	TAILQ_INIT(&p->ksched_data.corealloc_data.alloc_me);
	TAILQ_INIT(&p->ksched_data.corealloc_data.prov_alloc_me);
	TAILQ_INIT(&p->ksched_data.corealloc_data.prov_not_alloc_me);
}

void __track_alloc(struct proc *p, struct sched_pcore *c)
{
	struct proc *owner = c->alloc_proc;
	if (c->prov_proc == p) {
		TAILQ_REMOVE(&p->ksched_data.corealloc_data.prov_not_alloc_me, c,
					 prov_next);
		TAILQ_INSERT_HEAD(&p->ksched_data.corealloc_data.prov_alloc_me, c,
					      prov_next);
		if (owner != NULL) {
			TAILQ_REMOVE(&(owner->ksched_data.corealloc_data.alloc_me), c, alloc_next);
		}
	}
	TAILQ_INSERT_TAIL(&p->ksched_data.corealloc_data.alloc_me, c, alloc_next);
	c->alloc_proc = p;
}

void __track_dealloc(struct proc *p, struct sched_pcore *c)
{
	if ( corerequest_spc2pcoreid(c) == 0)
		return;
	c->alloc_proc = NULL;
	TAILQ_REMOVE(&(p->ksched_data.corealloc_data.alloc_me), c, alloc_next);
	if (c->prov_proc == p){
		TAILQ_REMOVE(&p->ksched_data.corealloc_data.prov_alloc_me, c,
					 prov_next);
		TAILQ_INSERT_HEAD(&(p->ksched_data.corealloc_data.prov_not_alloc_me), c,
						  prov_next);
	}
}

