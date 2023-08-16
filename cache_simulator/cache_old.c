/*
 * cache.c
 */


#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "cache.h"
#include "main.h"

 /* cache configuration parameters */
static int cache_split = 0;
static int cache_usize = DEFAULT_CACHE_SIZE;
static int cache_isize = DEFAULT_CACHE_SIZE;
static int cache_dsize = DEFAULT_CACHE_SIZE;
static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;
static int cache_assoc = DEFAULT_CACHE_ASSOC;
static int cache_writeback = DEFAULT_CACHE_WRITEBACK;
static int cache_writealloc = DEFAULT_CACHE_WRITEALLOC;

/* cache model data structures */
static Pcache icache;
static Pcache dcache;
static cache c1;
static cache c2;
static cache_stat cache_stat_inst;
static cache_stat cache_stat_data;

/************************************************************/
/* prototypes of functions */
void init_cache();
void set_cache_param(param, value);
void insert(Pcache_line* head, Pcache_line* tail, Pcache_line item);
void delete(Pcache_line* head, Pcache_line* tail, Pcache_line item);

/************************************************************/

/************************************************************/
Pcache_line create_new_line(Pcache c, Pcache_stat stat, unsigned tag, unsigned index_set, int is_new, int address)
{
	Pcache_line set_head = 0;

	Pcache_line line = malloc(sizeof(cache_line));
	memset(line, 0, sizeof(cache_line));

	// apply address to read/write cache.
	line->tag = tag;

	line->address = address;
	line->timestamp = clock();

	if (is_new == 1)
	{
		c->LRU_head[index_set] = line;
	}
	else
	{
		// make new one head of set.
		set_head = c->LRU_head[index_set];
		insert(&set_head->LRU_prev, &set_head->LRU_next, line);
		set_head->LRU_prev = 0;
	}

	c->set_contents[index_set]++;
	c->contents++;

	return line;
}

Pcache_line find_lru(Pcache_line head)
{
	Pcache_line found = 0;
	long lru = 0xFFFFFFFFFFFFFFFF;
	while (head)
	{
		if (head->timestamp < lru)
		{
			found = head;
			lru = head->timestamp;
		}

		head = head->LRU_next;
	}

	return found;

}

void read_cache_line(int type, Pcache c, Pcache_stat stat, unsigned tag, unsigned index_set, int address)
{
	// check if address exists.
	Pcache_line found = 0;
	Pcache_line set_head = c->LRU_head[index_set];
	Pcache_line head = set_head;
	int loop = 0;

	while (head)
	{
		if (head->tag == tag)
		{
			found = head;
			break;
		}

		head = head->LRU_next;
	}

	// if hit, read and return.
	if (found != 0)
	{
		// update LRU.
		if (found != set_head)
		{
			//delete(&set_head->LRU_prev, &set_head->LRU_next, found);
			//insert(&set_head->LRU_prev, &set_head->LRU_next, found);
		}

		found->timestamp = clock();

		return;
	}

	// if missed
	
	// if the set is full
	if (c->associativity <= c->set_contents[index_set])
	{
		if (set_head->dirty == 1 && cache_writeback == TRUE)
		{// if the line is dirty, copy-back.
			set_head->dirty = 0;
			stat->copies_back += (cache_block_size / 4);
		}



		stat->replacements++;
		// replace the contents of first line of the set
		set_head->tag = tag;
		set_head->address = address;

		stat->demand_fetches += (cache_block_size / 4);
		stat->misses++;
	}
	else
	{
		// create a new line and insert the set.
		create_new_line(dcache, stat, tag, index_set, 0, address);

		stat->demand_fetches += (cache_block_size / 4);
		stat->misses++;
	}
}

int cur_line = 0;
/************************************************************/
void perform_access(addr, access_type)
unsigned addr, access_type;
{
	/* handle an access to the cache */
	int tag, index_set, index_block;

	cur_line++;
	if (addr == 0x7ffebac8)
		addr = addr;

	switch (access_type)
	{
	case TRACE_DATA_LOAD:
	{
		cache_stat_data.accesses++;

		// get tag, index of set and index of block from address.
		tag = addr >> (32 - dcache->index_mask_offset);
		index_set = (addr & dcache->index_mask) >> dcache->block_bit_num;
		index_block = addr & ((1 << dcache->block_bit_num) - 1);

		// get the specific set of data cache.
		Pcache_line set_head = dcache->LRU_head[index_set];

		// if a set of cache is empty.
		if (set_head == 0)
		{
			// create a head line 
			create_new_line(dcache, &cache_stat_data, tag, index_set, 1, addr);

			cache_stat_data.misses++;
			// there is no set so need to fetch data from memory
			cache_stat_data.demand_fetches += (cache_block_size / 4);
		}
		else
		{
			read_cache_line(access_type, dcache, &cache_stat_data, tag, index_set, addr);
		}

		break;
	}
	case TRACE_INST_LOAD:
	{
		cache_stat_inst.accesses++;

		// get tag, index of set and index of block from address.
		tag = addr >> (32 - icache->index_mask_offset);
		index_set = (addr & icache->index_mask) >> icache->block_bit_num;
		index_block = addr & ((1 << icache->block_bit_num) - 1);

		// get the specific set of data cache.
		Pcache_line set_head = icache->LRU_head[index_set];

		// create a head line if a set of cache is empty.
		if (set_head == 0)
		{
			create_new_line(icache, &cache_stat_inst, tag, index_set, 1, addr);

			cache_stat_inst.misses++;
			// there is no set so need to fetch data from memory
			cache_stat_inst.demand_fetches += (cache_block_size / 4);
		}
		else
		{
			read_cache_line(access_type, icache, &cache_stat_inst, tag, index_set, addr);
		}

		break;
	}
	case TRACE_DATA_STORE:
	{
		cache_stat_data.accesses++;

		// get tag, index of set and index of block from address.
		tag = addr >> (32 - dcache->index_mask_offset);
		index_set = (addr & dcache->index_mask) >> dcache->block_bit_num;
		index_block = addr & ((1 << dcache->block_bit_num) - 1);

		// get the specific set of data cache.
		Pcache_line set_head = dcache->LRU_head[index_set];

		// if the specific set is empty
		if (set_head == 0)
		{
			// process in case of write-allocate policy.
			if (cache_writealloc == TRUE)
			{
				// treat a write to a word that is not in the cache as a cache miss
				cache_stat_data.misses++;

				// create a new line and insert the set.
				set_head = create_new_line(dcache, &cache_stat_data, tag, index_set, 1, addr);

				cache_stat_data.demand_fetches += (cache_block_size / 4);

				if (cache_writeback == TRUE)
				{
					set_head->dirty = 1;
				}
			}
			else
			{
				// if no write-allocate policy, updates only main memory without disturbing the cache
			}
		}
		else
		{
			// if the set is not empty, check if address exists
			Pcache_line found = 0;
			Pcache_line set_head = dcache->LRU_head[index_set];
			Pcache_line head = set_head;

			int loop = 0;
			while (head)
			{
				if (head->tag == tag)
				{
					found = head;
					break;
				}

				loop++;
				if (loop == 2)
				{
					loop = loop;
				}
				head = head->LRU_next;
			}

			// if hitted
			if (found)
			{
				if (found->dirty == 0)
				{
					if (cache_writeback == TRUE)
					{
						found->dirty = 1;
					}
				}
			}
			// if the line is not in the set
			else
			{
				// if write-allocate policy
				if (cache_writealloc == TRUE)
				{
					// treat a write to a word that is not in the cache as a cache miss
					cache_stat_data.misses++;

					// if the set is full
					if (dcache->associativity == dcache->set_contents[index_set])
					{
						if (set_head->dirty == 1 && cache_writeback == TRUE)
						{// if the line is dirty, copy-back.
							set_head->dirty = 0;
							cache_stat_data.copies_back += (cache_block_size / 4);
						}

						cache_stat_data.replacements++;
						cache_stat_data.demand_fetches += (cache_block_size / 4);

						//replace the contents of first line of the set.
						set_head->tag = tag;
						set_head->address = addr;

						if (cache_writeback == TRUE)
						{
							set_head->dirty = 1;
						}
					}
					// if the set is not full
					else
					{
						// create a new line and insert the set.
						set_head = create_new_line(dcache, &cache_stat_data, tag, index_set, 0, addr);

						cache_stat_data.demand_fetches += (cache_block_size / 4);

						if (cache_writeback == TRUE)
						{
							set_head->dirty = 1;
						}
					}

				}
				// if no write allocate policy
				else
				{
					// if no write-allocate policy, updates only main memory without disturbing the cache
				}
			}
		}

		break;
	}
	default: break;
	}

}

/************************************************************/
void init_cache()
{
	/* initialize the cache, and cache statistics data structures */

	/* initialize the cache instance */
	memset(&c1, 0, sizeof(cache));
	memset(&c2, 0, sizeof(cache));

	icache = &c1;
	dcache = (cache_split == 1) ? &c2 : &c1;

	icache->associativity = dcache->associativity = cache_assoc;

	if (cache_split == 1)
	{
		icache->size = cache_isize;
		dcache->size = cache_dsize;
	}
	else
		icache->size = cache_usize;

	icache->n_sets = icache->size / (cache_block_size * cache_assoc);
	dcache->n_sets = dcache->size / (cache_block_size * cache_assoc);

	icache->block_bit_num = dcache->block_bit_num = LOG2(cache_block_size);

	/* calcuate the index mask and offest */
	icache->index_mask_offset = 32 - LOG2(icache->n_sets) - icache->block_bit_num;
	dcache->index_mask_offset = 32 - LOG2(dcache->n_sets) - dcache->block_bit_num;
	icache->index_mask = (1 << (32 - icache->index_mask_offset)) - 1;
	dcache->index_mask = (1 << (32 - icache->index_mask_offset)) - 1;

	/* allocate the heads and tails for set */
	icache->LRU_head = malloc(icache->n_sets * sizeof(Pcache_line));
	memset(icache->LRU_head, 0, icache->n_sets * sizeof(Pcache_line));

	icache->LRU_tail = malloc(icache->n_sets * sizeof(Pcache_line));
	memset(icache->LRU_tail, 0, icache->n_sets * sizeof(Pcache_line));

	if (cache_split == 1)
	{
		dcache->LRU_head = malloc(dcache->n_sets * sizeof(Pcache_line));
		memset(dcache->LRU_head, 0, dcache->n_sets * sizeof(Pcache_line));

		dcache->LRU_tail = malloc(dcache->n_sets * sizeof(Pcache_line));
		memset(dcache->LRU_tail, 0, dcache->n_sets * sizeof(Pcache_line));
	}

	/* allocate the contents for set */
	icache->set_contents = malloc(sizeof(int) * icache->n_sets);
	memset(icache->set_contents, 0, sizeof(int) * icache->n_sets);

	if (cache_split == 1)
	{
		dcache->set_contents = malloc(sizeof(int) * dcache->n_sets);
		memset(dcache->set_contents, 0, sizeof(int) * dcache->n_sets);
	}

	icache->contents = dcache->contents = 0;

	/* initialize the cache statistics instance */
	memset(&cache_stat_inst, 0, sizeof(cache_stat));
	memset(&cache_stat_data, 0, sizeof(cache_stat));
}

/************************************************************/

/************************************************************/
void flush()
{
	/* flush the cache */
	for (int i = 0; i < dcache->n_sets; ++i)
	{
		Pcache_line set_head = dcache->LRU_head[i];
		Pcache_line head = set_head;

		while (head)
		{
			if (head->dirty == 1)
			{
				cache_stat_data.copies_back += (cache_block_size / 4);
			}
			head = head->LRU_next;
		}
	}

	free(dcache->set_contents);
	//free(dcache->LRU_head);
	//free(dcache->LRU_tail);

	if (icache != dcache)
	{
		for (int i = 0; i < icache->n_sets; ++i)
		{
			Pcache_line set_head = icache->LRU_head[i];
			Pcache_line head = set_head;

			while (head)
			{
				if (head->dirty == 1)
				{
					cache_stat_inst.copies_back += (cache_block_size / 4);
				}
				head = head->LRU_next;
			}
		}

		free(icache->set_contents);
		//free(icache->LRU_head);
		//free(icache->LRU_tail);
	}
}
/************************************************************/

/************************************************************/
void delete(head, tail, item)
Pcache_line* head, * tail;
Pcache_line item;
{
	if (item->LRU_prev) {
		item->LRU_prev->LRU_next = item->LRU_next;
	}
	else {
		/* item at head */
		*head = item->LRU_next;
	}

	if (item->LRU_next) {
		item->LRU_next->LRU_prev = item->LRU_prev;
	}
	else {
		/* item at tail */
		*tail = item->LRU_prev;
	}
}
/************************************************************/

/************************************************************/
/* inserts at the head of the list */
void insert(head, tail, item)
Pcache_line* head, * tail;
Pcache_line item;
{
	item->LRU_next = *head;
	item->LRU_prev = (Pcache_line)NULL;

	if (item->LRU_next)
		item->LRU_next->LRU_prev = item;
	else
		*tail = item;

	*head = item;
}
/************************************************************/

/************************************************************/
void dump_settings()
{
	printf("*** CACHE SETTINGS ***\n");
	if (cache_split) {
		printf("  Split I- D-cache\n");
		printf("  I-cache size: \t%d\n", cache_isize);
		printf("  D-cache size: \t%d\n", cache_dsize);
	}
	else {
		printf("  Unified I- D-cache\n");
		printf("  Size: \t%d\n", cache_usize);
	}
	printf("  Associativity: \t%d\n", cache_assoc);
	printf("  Block size: \t%d\n", cache_block_size);
	printf("  Write policy: \t%s\n",
		cache_writeback ? "WRITE BACK" : "WRITE THROUGH");
	printf("  Allocation policy: \t%s\n",
		cache_writealloc ? "WRITE ALLOCATE" : "WRITE NO ALLOCATE");
}
/************************************************************/

/************************************************************/
void set_cache_param(param, value)
int param;
int value;
{

	switch (param) {
	case CACHE_PARAM_BLOCK_SIZE:
		cache_block_size = value;
		words_per_block = value / WORD_SIZE;
		break;
	case CACHE_PARAM_USIZE:
		cache_split = FALSE;
		cache_usize = value;
		break;
	case CACHE_PARAM_ISIZE:
		cache_split = TRUE;
		cache_isize = value;
		break;
	case CACHE_PARAM_DSIZE:
		cache_split = TRUE;
		cache_dsize = value;
		break;
	case CACHE_PARAM_ASSOC:
		cache_assoc = value;
		break;
	case CACHE_PARAM_WRITEBACK:
		cache_writeback = TRUE;
		break;
	case CACHE_PARAM_WRITETHROUGH:
		cache_writeback = FALSE;
		break;
	case CACHE_PARAM_WRITEALLOC:
		cache_writealloc = TRUE;
		break;
	case CACHE_PARAM_NOWRITEALLOC:
		cache_writealloc = FALSE;
		break;
	default:
		printf("error set_cache_param: bad parameter value\n");
		exit(-1);
	}

}
/************************************************************/

/************************************************************/
void print_stats()
{
	printf("\n*** CACHE STATISTICS ***\n");

	printf(" INSTRUCTIONS\n");
	printf("  accesses:  %d\n", cache_stat_inst.accesses);
	printf("  misses:    %d\n", cache_stat_inst.misses);
	if (!cache_stat_inst.accesses)
		printf("  miss rate: 0 (0)\n");
	else
		printf("  miss rate: %2.4f (hit rate %2.4f)\n",
			(float)cache_stat_inst.misses / (float)cache_stat_inst.accesses,
			1.0 - (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses);
	printf("  replace:   %d\n", cache_stat_inst.replacements);

	printf(" DATA\n");
	printf("  accesses:  %d\n", cache_stat_data.accesses);
	printf("  misses:    %d\n", cache_stat_data.misses);
	if (!cache_stat_data.accesses)
		printf("  miss rate: 0 (0)\n");
	else
		printf("  miss rate: %2.4f (hit rate %2.4f)\n",
			(float)cache_stat_data.misses / (float)cache_stat_data.accesses,
			1.0 - (float)cache_stat_data.misses / (float)cache_stat_data.accesses);
	printf("  replace:   %d\n", cache_stat_data.replacements);

	printf(" TRAFFIC (in words)\n");
	printf("  demand fetch:  %d\n", cache_stat_inst.demand_fetches +
		cache_stat_data.demand_fetches);
	printf("  copies back:   %d\n", cache_stat_inst.copies_back +
		cache_stat_data.copies_back);
}
/************************************************************/
