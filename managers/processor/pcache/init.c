/*
 * Copyright (c) 2016-2017 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <lego/mm.h>
#include <lego/slab.h>
#include <lego/log2.h>
#include <lego/kernel.h>
#include <lego/pgfault.h>
#include <lego/syscalls.h>
#include <lego/comp_processor.h>
#include <asm/io.h>

#include <processor/pcache.h>

u64 pcache_registered_start;
u64 pcache_registered_size;

/* Final used size */
u64 llc_cache_size;

/* pages used by cacheline and metadata */
u64 nr_pages_cacheline;
u64 nr_pages_metadata;

/* nr_cachelines = nr_cachesets * associativity */
u64 nr_cachelines __read_mostly;
u64 nr_cachesets __read_mostly;

/*
 * Original physical and ioremap'd kernel virtual address
 * These are read frequently to calculate offsets between structures:
 */
u64 phys_start_cacheline __read_mostly;
u64 phys_start_metadata __read_mostly;
u64 virt_start_cacheline __read_mostly;
struct pcache_meta *pcache_meta_map __read_mostly;

struct pcache_set *pcache_set_map __read_mostly;

/*
 * Bits to mask virtual address:
 * |MSB ..     |    ...   |      ...      LSB|
 *   [tag_mask] [set_mask] [ cacheline_mask]
 */
u64 pcache_cacheline_mask __read_mostly;
u64 pcache_set_mask __read_mostly;
u64 pcache_tag_mask __read_mostly;

/* Address bits usage */
u64 nr_bits_cacheline;
u64 nr_bits_set;
u64 nr_bits_tag;

/* Offset between neighbouring ways within a set */
u64 pcache_way_cache_stride __read_mostly;

/*
 * We are using special memmap semantic.
 * Pcache pages are marked as *reserved* in memblock, so all
 * pages should have been marked as PageReserve by reserve_bootmem_region().
 */
static void pcache_sanity_check(void)
{
	int i;
	struct page *page;
	unsigned long nr_pages = pcache_registered_size / PAGE_SIZE;
	unsigned long va = virt_start_cacheline;

	for (i = 0; i < nr_pages; i++, va += PAGE_SIZE) {
		page = virt_to_page(va);

		if (unlikely(!PageReserved(page))) {
			dump_page(page, NULL);
			panic("Bug indeed");
		}
	}
}

/* Allocate and init pcache_set array */
static void init_pcache_set_map(void)
{
	u64 size;
	int i;
	struct pcache_set *pset;

	size = nr_cachesets * sizeof(struct pcache_set);

	pcache_set_map = kmalloc(size, GFP_KERNEL);
	if (!pcache_set_map)
		panic("Unable to allocate pcache set array!");

	for (i = 0; i < nr_cachesets; i++) {
		pset = pcache_set_map + i;
		spin_lock_init(&pset->lock);
	}
}

/* Init pcache_meta array */
static void init_pcache_meta_map(void)
{
	struct pcache_meta *pcm;
	int i;

	for (i = 0; i < nr_cachelines; i++) {
		pcm = pcache_meta_map + i;
		memset(pcm, 0, sizeof(*pcm));

		INIT_LIST_HEAD(&pcm->rmap);
		pcache_mapcount_reset(pcm);
	}
}

void __init pcache_init_waitqueue(void);

void __init pcache_init(void)
{
	u64 nr_cachelines_per_page, nr_units;
	u64 unit_size;

	if (pcache_registered_start == 0 || pcache_registered_size == 0)
		panic("Processor cache not registered, memmap $ needed!");

	if (!IS_ENABLED(CONFIG_LEGO_SPECIAL_MEMMAP))
		panic("Require special memmap $ semantic!");

	virt_start_cacheline = (unsigned long)phys_to_virt(pcache_registered_start);

	/*
	 * Clear any stale value
	 * This may happen if running on QEMU.
	 * Not sure about physical machine.
	 */
	memset((void *)virt_start_cacheline, 0, pcache_registered_size);

	pcache_sanity_check();

	nr_cachelines_per_page = PAGE_SIZE / PCACHE_META_SIZE;
	unit_size = nr_cachelines_per_page * PCACHE_LINE_SIZE;
	unit_size += PAGE_SIZE;

	/*
	 * nr_cachelines_per_page must already be a power of 2.
	 * We must make nr_units a power of 2, then the total
	 * number of cache lines can be a power of 2, too.
	 */
	nr_units = pcache_registered_size / unit_size;
	nr_units = rounddown_pow_of_two(nr_units);

	/* final valid used size */
	llc_cache_size = nr_units * unit_size;

	nr_cachelines = nr_units * nr_cachelines_per_page;
	nr_cachesets = nr_cachelines / PCACHE_ASSOCIATIVITY;

	nr_pages_cacheline = nr_cachelines;
	nr_pages_metadata = nr_units;

	/* Save physical/virtual starting address */
	phys_start_cacheline = pcache_registered_start;
	phys_start_metadata = phys_start_cacheline + nr_pages_cacheline * PAGE_SIZE;
	pcache_meta_map = (struct pcache_meta *)(virt_start_cacheline + nr_pages_cacheline * PAGE_SIZE);

	/* Now the pcache_set and pcache_meta array */
	init_pcache_set_map();
	init_pcache_meta_map();

	nr_bits_cacheline = ilog2(PCACHE_LINE_SIZE);
	nr_bits_set = ilog2(nr_cachesets);
	nr_bits_tag = 64 - nr_bits_cacheline - nr_bits_set;

	pr_info("Processor LLC Configurations:\n");
	pr_info("    PhysStart:         %#llx\n",	pcache_registered_start);
	pr_info("    VirtStart:         %#llx\n",	virt_start_cacheline);
	pr_info("    Registered Size:   %#llx\n",	pcache_registered_size);
	pr_info("    Actual Used Size:  %#llx\n",	llc_cache_size);
	pr_info("    NR cachelines:     %llu\n",	nr_cachelines);
	pr_info("    Associativity:     %lu\n",		PCACHE_ASSOCIATIVITY);
	pr_info("    NR Sets:           %llu\n",	nr_cachesets);
	pr_info("    Cacheline size:    %lu B\n",	PCACHE_LINE_SIZE);
	pr_info("    Metadata size:     %lu B\n",	PCACHE_META_SIZE);

	pcache_cacheline_mask = (1ULL << nr_bits_cacheline) - 1;
	pcache_set_mask = ((1ULL << (nr_bits_cacheline + nr_bits_set)) - 1) & ~pcache_cacheline_mask;
	pcache_tag_mask = ~((1ULL << (nr_bits_cacheline + nr_bits_set)) - 1);

	pr_info("    NR cacheline bits: %2llu [%2llu - %2llu] %#018llx\n",
		nr_bits_cacheline,
		0ULL,
		nr_bits_cacheline - 1,
		pcache_cacheline_mask);
	pr_info("    NR set-index bits: %2llu [%2llu - %2llu] %#018llx\n",
		nr_bits_set,
		nr_bits_cacheline,
		nr_bits_cacheline + nr_bits_set - 1,
		pcache_set_mask);
	pr_info("    NR tag bits:       %2llu [%2llu - %2llu] %#018llx\n",
		nr_bits_tag,
		nr_bits_cacheline + nr_bits_set,
		nr_bits_cacheline + nr_bits_set + nr_bits_tag - 1,
		pcache_tag_mask);

	pr_info("    NR pages for data: %llu\n",	nr_pages_cacheline);
	pr_info("    NR pages for meta: %llu\n",	nr_pages_metadata);
	pr_info("    Cacheline (pa) range:   [%#18llx - %#18llx]\n",
		phys_start_cacheline, phys_start_metadata - 1);
	pr_info("    Metadata (pa) range:    [%#18llx - %#18llx]\n",
		phys_start_metadata, phys_start_metadata + nr_pages_metadata * PAGE_SIZE - 1);

	pr_info("    Cacheline (va) range:   [%#18llx - %#18lx]\n",
		virt_start_cacheline, (unsigned long)pcache_meta_map - 1);
	pr_info("    Metadata (va) range:    [%18p - %#18lx]\n",
		pcache_meta_map, (unsigned long)(pcache_meta_map + nr_cachelines) - 1);

	pcache_way_cache_stride = nr_cachesets * PCACHE_LINE_SIZE;
	pr_info("    Way cache stride:  %#llx\n", pcache_way_cache_stride);

	pcache_init_waitqueue();
}

/**
 * pcache_range_register
 * @start: physical address of the first byte of the cache
 * @size: size of the cache
 *
 * Register a consecutive physical memory range as the last-level cache for
 * processor component. It is invoked at early boot before everything about
 * memory is initialized. For x86, this is registered during the parsing of
 * memmap=N$N command line option.
 *
 * If CONFIG_LEGO_SPECIAL_MEMMAP is ON, this range will not be bailed out
 * from e820 table, it is marked as reserved in memblock. So pages within
 * this range still have `struct page`, yeah!
 */
int __init pcache_range_register(u64 start, u64 size)
{
	if (WARN_ON(!start && !size))
		return -EINVAL;

	if (WARN_ON(offset_in_page(start) || offset_in_page(size)))
		return -EINVAL;

	if (pcache_registered_start || pcache_registered_size)
		panic("Remove extra memmap from kernel parameters!");

	pcache_registered_start = start;
	pcache_registered_size = size;

	return 0;
}

SYSCALL_DEFINE1(pcache_stat, struct pcache_stat __user *, statbuf)
{
	struct pcache_stat kstat;

	kstat.nr_cachelines = nr_cachelines;
	kstat.nr_cachesets = nr_cachesets;
	kstat.associativity = PCACHE_ASSOCIATIVITY;
	kstat.cacheline_size = PCACHE_LINE_SIZE;
	kstat.way_stride = pcache_way_cache_stride;

	if (copy_to_user(statbuf, &kstat, sizeof(kstat)))
		return -EFAULT;
	return 0;
}