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

};

void provalloc_nodes_init();
struct sched_pcore *provalloc_alloc_core(struct proc *p);
void provalloc_track_alloc(struct proc *p, struct sched_pcore *c);
void provalloc_free_core(struct proc *p, uint32_t core_id);
void provalloc_prov_core(struct proc *p, uint32_t core_id);

void print_node(struct sched_pnode *n);
void print_nodes(int type);
void print_all_nodes();
void test_structure();

#endif
