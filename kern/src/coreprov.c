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

#include <corerequest.h>
#include <stdio.h>
#include <string.h>
#include <arch/topology.h>
#include <process.h>


/* Provision a given core to the proc p. This function is just used for test
 * here, we should move it to an other file dedicated to provisioning stuffs */
void corerequest_prov_core(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc;
	struct sched_pcore_tailq *prov_list;
	spc = pcoreid2spc(pcoreid);
	/* If the core is already prov to someone else, take it away.  (last write
	 * wins, some other layer or new func can handle permissions). */
	if (spc->prov_proc) {
		/* the list the spc is on depends on whether it is alloced to the
		 * prov_proc or not */
		prov_list = (spc->alloc_proc == spc->prov_proc ?
		             &spc->prov_proc->ksched_data.prov_alloc_me :
		             &spc->prov_proc->ksched_data.
					 prov_not_alloc_me);
		TAILQ_REMOVE(prov_list, spc, prov_next);
	}
	/* Now prov it to p.  Again, the list it goes on depends on whether it is
	 * alloced to p or not.  Callers can also send in 0 to de-provision. */
	if (p) {
		if (spc->alloc_proc == p) {
			TAILQ_INSERT_TAIL(&p->ksched_data.prov_alloc_me, spc,
							  prov_next);
		} else {
			/* this is be the victim list, which can be sorted so that we pick
			 * the right victim (sort by alloc_proc reverse priority, etc). */
			TAILQ_INSERT_TAIL(&p->ksched_data.prov_not_alloc_me,
							  spc, prov_next);
		}
	}
	spc->prov_proc = p;
}

/* Helper for the destroy CB : unprovisions any pcores for the given list */
static void unprov_pcore_list(struct sched_pcore_tailq *list_head)
{
	struct sched_pcore *spc_i;
	/* We can leave them connected within the tailq, since the scps don't have a
	 * default list (if they aren't on a proc's list, then we don't care about
	 * them), and since the INSERTs don't care what list you were on before
	 * (chummy with the implementation).  Pretty sure this is right.  If there's
	 * suspected list corruption, be safer here. */
	TAILQ_FOREACH(spc_i, list_head, prov_next)
		spc_i->prov_proc = 0;
	TAILQ_INIT(list_head);
}

void corerequest_unprov_proc(struct proc *p)
{
	unprov_pcore_list(&p->ksched_data.prov_alloc_me);
	unprov_pcore_list(&p->ksched_data.prov_not_alloc_me);
}

static void deprovision_core(struct sched_pcore *c)
{
	struct proc *p = c-> prov_proc;
	c->prov_proc = NULL;
	if (c->alloc_proc == p)
		TAILQ_REMOVE(&p->ksched_data.prov_alloc_me, c, prov_next);
	else
		TAILQ_REMOVE(&p->ksched_data.prov_alloc_me, c, prov_next);
}
