/*
 * Copyright (c) 2015 The Regents of the University of California
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifndef	SCHEDULE_H
#define	SCHEDULE_H

#include <sys/queue.h>
#include <arch/topology.h>

enum pnode_type { CORE, CPU, SOCKET, NUMA, MACHINE, NUM_NODE_TYPES};
static char pnode_label[5][8] = { "CORE", "CPU", "SOCKET", "NUMA", "MACHINE" };

struct sched_pcore {
	struct sched_pnode *spn;
	struct core_info *spc_info;
	STAILQ_ENTRY(sched_pcore) prov_next;
	STAILQ_ENTRY(sched_pcore) alloc_next;
	struct proc *alloc_proc;
	struct proc *prov_proc;
};
STAILQ_HEAD(sched_pcore_tailq, sched_pcore);

struct sched_pnode {
	int id;
	enum pnode_type type;
	int refcount[NUM_NODE_TYPES];
	struct sched_pnode *parent;
	struct sched_pnode *children;
	struct sched_pcore *spc_data;
};

struct sched_proc_data {
	struct sched_pcore_tailq alloc_me;
	struct sched_pcore_tailq prov_alloc_me;
	struct sched_pcore_tailq prov_not_alloc_me;
};

struct proc {
	struct sched_proc_data ksched_data;
};

void nodes_init();
void alloc_core_any(struct proc *p, int amt);
void alloc_core_specific(struct proc *p, int core_id);
int free_core_specific(struct proc *p, int core_id);
void provision_core(struct proc *p, int core_id);

void print_node(struct sched_pnode *n);
void print_nodes(int type);
void print_all_nodes();
void test_structure();

#endif
