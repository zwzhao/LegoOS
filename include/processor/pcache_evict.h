/*
 * Copyright (c) 2016-2017 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _LEGO_PROCESSOR_PCACHE_SWEEP_H_
#define _LEGO_PROCESSOR_PCACHE_SWEEP_H_

#include <processor/pcache_types.h>

/*
 * Common sweep threads for certain eviction algorithms:
 * 	LRU: Least-Recently Used
 */
#ifdef CONFIG_PCACHE_EVICT_GENERIC_SWEEP
int __init evict_sweep_init(void);
#else
static inline int evict_sweep_init(void) { return 0; }
#endif

/*
 * Eviction Algorithm: LRU
 */
#ifdef CONFIG_PCACHE_EVICT_LRU

static inline void
__add_to_lru_list(struct pcache_meta *pcm, struct pcache_set *pset)
{
	list_add(&pcm->lru, pset->lru_list);
}

static inline void
add_to_lru_list(struct pcache_meta *pcm, struct pcache_set *pset)
{
	spin_lock(&pset->lru_lock);
	__add_to_lru_list(pcm, pset);
	spin_unlock(&pset->lru_lock);
}

static inline void
__del_from_lru_list(struct pcache_meta *pcm, struct pcache_set *pset)
{
	list_del(&pcm->lru);
}

static inline void
del_from_lru_list(struct pcache_meta *pcm, struct pcache_set *pset)
{
	spin_lock(&pset->lru_lock);
	__del_from_lru_list(pcm, pset);
	spin_unlock(&pset->lru_lock);
}

static inline void init_pcache_lru(struct pcache_meta *pcm)
{
	INIT_LIST_HEAD(&pcm->lru);
}

#else
static inline void
add_to_lru_list(struct pcache_meta *pcm, struct pcache_set *pset) { }
static inline void
del_from_lru_list(struct pcache_meta *pcm, struct pcache_set *pset) { }

static inline void init_pcache_lru(struct pcache_meta *pcm) { }

#endif /* EVICT_LRU */

#endif /* _LEGO_PROCESSOR_PCACHE_SWEEP_H_ */