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
static struct sched_pcore *all_pcores;
/* TAILQ of all unallocated, idle (CG) cores */
static struct sched_pcore_tailq idlecores = TAILQ_HEAD_INITIALIZER(idlecores);

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
	all_pcores = kzmalloc(sizeof(struct sched_pcore) * num_cores, 0);
	for (int i = 1; i < num_cores; i++) {
		struct sched_pcore *spc_i = corerequest_pcoreid2spc(i);
		spc_i->alloc_proc = NULL;
		spc_i->prov_proc = NULL;
		TAILQ_INSERT_TAIL(&idlecores, spc_i, alloc_next);
	}
}

/* Find the best core to give to p. First check p's list of cores
 * provisioned to it, but not yet allocated. If no cores are found, try and
 * pull from the idle list.  If no cores found on either list, return NULL.
 * */
struct sched_pcore *corerequest_alloc_core(struct proc *p)
{
	struct sched_pcore *spc_i = NULL;
	spc_i = TAILQ_FIRST(&p->ksched_data.corealloc_data.prov_not_alloc_me);
	if (!spc_i)
		spc_i = TAILQ_FIRST(&idlecores);
	return spc_i;
}

void corerequest_track_alloc(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc;
	spc = corerequest_pcoreid2spc(pcoreid);
	assert(spc->alloc_proc != p);	/* corruption or double-alloc */
	spc->alloc_proc = p;
	/* if the pcore is prov to them and now allocated, move lists */
	if (spc->prov_proc == p) {
		TAILQ_REMOVE(&p->ksched_data.corealloc_data.prov_not_alloc_me,
		             spc, prov_next);
		TAILQ_INSERT_TAIL(&p->ksched_data.corealloc_data.prov_alloc_me,
		                  spc, prov_next);
	}
	/* Actually allocate the core, removing it from the idle core list. */
	TAILQ_REMOVE(&idlecores, spc, alloc_next);
}

void corerequest_track_dealloc(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc;
	spc = corerequest_pcoreid2spc(pcoreid);
	spc->alloc_proc = 0;
	/* if the pcore is prov to them and now deallocated, move lists */
	if (spc->prov_proc == p) {
		TAILQ_REMOVE(&p->ksched_data.corealloc_data.prov_alloc_me,
		             spc, prov_next);
		/* this is the victim list, which can be sorted so that we pick the
		 * right victim (sort by alloc_proc reverse priority, etc).  In this
		 * case, the core isn't alloc'd by anyone, so it should be the first
		 * victim. */
		TAILQ_INSERT_HEAD(&p->ksched_data.corealloc_data.prov_not_alloc_me,
		                  spc, prov_next);
	}
	/* Actually dealloc the core, putting it back on the idle core list. */
	TAILQ_INSERT_TAIL(&idlecores, spc, alloc_next);
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
	TAILQ_INIT(&p->ksched_data.corealloc_data.prov_alloc_me);
	TAILQ_INIT(&p->ksched_data.corealloc_data.prov_not_alloc_me);
}

void corerequest_print_idlecoremap(void)
{
	struct sched_pcore *spc_i;
	TAILQ_FOREACH(spc_i, &idlecores, alloc_next)
		printk("Core %d, prov to %d (%p)\n", corerequest_spc2pcoreid(spc_i),
		       spc_i->prov_proc ? spc_i->prov_proc->pid :
			   0, spc_i->prov_proc);
}

void test_structure()
{
	struct sched_pcore *c = NULL;
	struct proc *p1 = kmalloc(sizeof(struct proc), 0);
	struct proc *p2 = kmalloc(sizeof(struct proc), 0);
	TAILQ_INIT(&p1->ksched_data.corealloc_data.prov_alloc_me);
	TAILQ_INIT(&p2->ksched_data.corealloc_data.prov_alloc_me);
	TAILQ_INIT(&p1->ksched_data.corealloc_data.prov_not_alloc_me);
	TAILQ_INIT(&p2->ksched_data.corealloc_data.prov_not_alloc_me);

	corerequest_prov_core(p1, 7);
	c = corerequest_alloc_core(p1);
	corerequest_track_alloc(p1, corerequest_spc2pcoreid(c));

	corerequest_prov_core(p2, 3);
	c = corerequest_alloc_core(p2);
	corerequest_track_alloc(p2, corerequest_spc2pcoreid(c));

	corerequest_prov_core(p2, 4);
	c = corerequest_alloc_core(p2);
	corerequest_track_alloc(p2, corerequest_spc2pcoreid(c));

	corerequest_prov_core(p1, 4);
	c = corerequest_alloc_core(p1);
	corerequest_track_alloc(p1, corerequest_spc2pcoreid(c));

	c = corerequest_alloc_core(p1);
	corerequest_track_alloc(p1, corerequest_spc2pcoreid(c));

	corerequest_track_dealloc(p1, 7);
	corerequest_prov_core(p2, 5);


	printk("Cores allocated:\n");
	for (int i = 1; i < num_cores; i++) {
		struct sched_pcore *c = corerequest_pcoreid2spc(i);
		if (c->alloc_proc) {
			int pid = (c->alloc_proc == p1) ? 1 : 2;
			printk("proc%d :core %d\n", pid, corerequest_spc2pcoreid(c));
		}
	}
	printk("\n");
	TAILQ_FOREACH(c, &(p2->ksched_data.corealloc_data.alloc_me), alloc_next) {
		printk("proc%d :core %d\n",2, corerequest_spc2pcoreid(c));
	}
	printk("\nCores prov_allocated:\n");
	TAILQ_FOREACH(c, &(p1->ksched_data.corealloc_data.prov_alloc_me), prov_next) {
		printk("proc%d :core %d\n",1, corerequest_spc2pcoreid(c));
	}
	printk("\n");
	TAILQ_FOREACH(c, &(p2->ksched_data.corealloc_data.prov_alloc_me), prov_next) {
		printk("proc%d :core %d\n",2, corerequest_spc2pcoreid(c));
	}
	printk("\nCores prov_not_allocated:\n");
	TAILQ_FOREACH(c, &(p1->ksched_data.corealloc_data.prov_not_alloc_me), prov_next) {
		printk("proc%d :core %d\n",1, corerequest_spc2pcoreid(c));
	}
	printk("\n");
	TAILQ_FOREACH(c, &(p2->ksched_data.corealloc_data.prov_not_alloc_me), prov_next) {
		printk("proc%d :core %d\n",2, corerequest_spc2pcoreid(c));
	}
	printk("\n");
}
