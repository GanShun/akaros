/*
 * Copyright (c) 2015 The Regents of the University of California
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifndef	PROVALLOC_H
#define	PROVALLOC_H

#include <sys/queue.h>
#include <arch/topology.h>

struct proc;	/* process.h includes us, but we need pointers now */
struct sched_pnode;
enum pnode_type { CORE, CPU, SOCKET, NUMA, MACHINE, NUM_NODE_TYPES};
static char pnode_label[5][8] = { "CORE", "CPU", "SOCKET", "NUMA", "MACHINE" };

/* The ksched maintains an internal array of these: the global pcore map.  Note
 * the prov_proc and alloc_proc are weak (internal) references, and should only
 * be used as a ref source while the ksched has a valid kref. */
struct sched_pcore {
	struct sched_pnode          *spn;
	struct core_info            *spc_info;
	TAILQ_ENTRY(sched_pcore)	prov_next;			/* on a proc's prov list */
	TAILQ_ENTRY(sched_pcore)	alloc_next;			/* on an alloc list (idle)*/
	struct proc					*prov_proc;			/* who this is prov to */
	struct proc					*alloc_proc;		/* who this is alloc to */
};
TAILQ_HEAD(sched_pcore_tailq, sched_pcore);

struct corealloc_data {
	struct sched_pcore_tailq    alloc_me;           /* any core alloc to us */
	struct sched_pcore_tailq	prov_alloc_me;		/* prov cores alloced us */
	struct sched_pcore_tailq	prov_not_alloc_me;	/* maybe alloc to others */
};

void corerequest_nodes_init();
struct sched_pcore *corerequest_alloc_core(struct proc *p);
void corerequest_track_alloc(struct proc *p, struct sched_pcore *c);
void corerequest_free_core(struct proc *p, uint32_t core_id);
void corerequest_register_proc(struct proc *p);
int corerequest_get_any_core();
void corerequest_print_idlecoremap();
uint32_t corerequest_spc2pcoreid(struct sched_pcore *spc);
struct sched_pcore *corerequest_pcoreid2spc(uint32_t pcoreid);
void corerequest_unprov_proc(struct proc *p);
void corerequest_prov_core(struct proc *p, uint32_t pcoreid);


void print_node(struct sched_pnode *n);
void print_nodes(int type);
void print_all_nodes();
void test_structure();

/* Use this inline fucntions in schedule.c to avoid accessing fields directly*/
static inline struct proc *get_alloc_proc(struct sched_pcore *c)
{
	return c->alloc_proc;
}

static inline struct proc *get_prov_proc(struct sched_pcore *c)
{
	return c->prov_proc;
}

#endif
