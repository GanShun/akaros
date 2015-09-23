/*
 * Copyright (c) 2015 The Regents of the University of California
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifndef AKAROS_KERN_COREREQUEST_H
#define AKAROS_KERN_COREREQUEST_H

uint32_t spc2pcoreid(struct sched_pcore *spc);
struct sched_pcore *pcoreid2spc(uint32_t pcoreid);

#endif // AKAROS_KERN_COREREQUEST_H
