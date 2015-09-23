/*
 * Copyright (c) 2015 The Regents of the University of California
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifndef AKAROS_KERN_COREREQUEST_H
#define AKAROS_KERN_COREREQUEST_H

#include <sys/queue.h>

struct proc;	/* process.h includes us, but we need pointers now */

/* The ksched maintains an internal array of these: the global pcore map.  Note
 * the prov_proc and alloc_proc are weak (internal) references, and should only
 * be used as a ref source while the ksched has a valid kref. */
struct sched_pcore {
	TAILQ_ENTRY(sched_pcore)	prov_next;			/* on a proc's prov list */
	TAILQ_ENTRY(sched_pcore)	alloc_next;			/* on an alloc list (idle)*/
	struct proc					*prov_proc;			/* who this is prov to */
	struct proc					*alloc_proc;		/* who this is alloc to */
};
TAILQ_HEAD(sched_pcore_tailq, sched_pcore);

/* One of these embedded in every struct proc */
struct sched_proc_data {
	TAILQ_ENTRY(proc)			proc_link;			/* tailq linkage */
	struct proc_list 			*cur_list;			/* which tailq we're on */
	struct sched_pcore_tailq	prov_alloc_me;		/* prov cores alloced us */
	struct sched_pcore_tailq	prov_not_alloc_me;	/* maybe alloc to others */
	/* count of lists? */
	/* other accounting info */
};
uint32_t spc2pcoreid(struct sched_pcore *spc);
struct sched_pcore *pcoreid2spc(uint32_t pcoreid);

void corerequest_unprov_proc(struct proc *p);
void corerequest_prov_core(struct proc *p, uint32_t pcoreid);

#endif // AKAROS_KERN_COREREQUEST_H
