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
void set_cache_param(param, value);

/* initialize one cache instance */
void init_cache_instance(cache *c, int cache_size)
{
	int offset_bits;
	int set_bits;
	int mask;			

	c->size = cache_size;
	c->associativity = cache_assoc;

	// size of set.
	c->n_sets = cache_size / (cache_assoc * cache_block_size);

	set_bits = LOG2(c->n_sets);
	offset_bits = LOG2(cache_block_size);

	mask = (1 << set_bits) - 1;

	c->index_mask = mask << offset_bits;
	c->index_mask_offset = offset_bits;

	c->LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line) * c->n_sets);
	c->LRU_tail = (Pcache_line *)malloc(sizeof(Pcache_line) * c->n_sets);
	memset(c->LRU_head, 0, c->n_sets * sizeof(Pcache_line));
	memset(c->LRU_tail, 0, c->n_sets * sizeof(Pcache_line));

	c->set_contents = (int *)malloc(sizeof(int) * c->n_sets);
	memset(c->set_contents, 0, sizeof(int) * c->n_sets);

	c->contents = 0;
}
/************************************************************/

/************************************************************/
/* initialize the cache and cache statistics data structures */
void init_cache()
{	
	/* initialize the cache */
	if (cache_split)
	{
		init_cache_instance(&c1, cache_dsize);
		init_cache_instance(&c2, cache_isize);
	}
	else
	{
		init_cache_instance(&c1, cache_usize);
	}

	/* initialize the cache statistics */
	memset(&cache_stat_data, 0, sizeof(cache_stat));
	memset(&cache_stat_inst, 0, sizeof(cache_stat));
}
/************************************************************/

/************************************************************/
/* update lru cache line in the set. */
void apply_lru(Pcache c, unsigned int set_index, Pcache_line line)
{
	delete(&c->LRU_head[set_index], &c->LRU_tail[set_index], line);
	insert(&c->LRU_head[set_index], &c->LRU_tail[set_index], line);
}
/************************************************************/

/************************************************************/
void process_access_load_instruction(Pcache c, unsigned addr, unsigned int set_index, unsigned int block_word_size, unsigned int tag)
{
	cache_stat_inst.accesses++;

	// if set is empty
	if (c->LRU_head[set_index] == 0)
	{ // if the set is empty
		Pcache_line line = malloc(sizeof(cache_line));
		line->tag = tag;
		line->dirty = 0;

		c->set_contents[set_index] = 1;
		insert(&c->LRU_head[set_index], &c->LRU_tail[set_index], line);

		cache_stat_inst.misses++;
		cache_stat_inst.demand_fetches += block_word_size;
	}
	else
	{
		// if set is full.
		if (c->set_contents[set_index] == c->associativity)
		{
			// find hit line
			int found = 0;
			Pcache_line cl = c->LRU_head[set_index];
			for (int i = 0; i < c->set_contents[set_index]; i++)
			{
				if (cl->tag == tag)
				{
					found = 1;
					break;
				}
				cl = cl->LRU_next;
			}

			// if hit
			if (found)
			{
				// process LRU
				apply_lru(c, set_index, cl);
			}
			else
			{// if missed
				Pcache_line line = malloc(sizeof(cache_line));
				line->tag = tag;
				line->dirty = 0;

				// replace the exist LRU item with new line.
				insert(&c->LRU_head[set_index], &c->LRU_tail[set_index], line);

				if (c->LRU_tail[set_index]->dirty)
				{
					cache_stat_data.copies_back += block_word_size;
				}
				delete (&c->LRU_head[set_index], &c->LRU_tail[set_index], c->LRU_tail[set_index]);

				cache_stat_inst.misses++;
				cache_stat_inst.replacements++;
				cache_stat_inst.demand_fetches += block_word_size;
			}
		}
		else
		{
			// find hit line.
			int found = 0;
			Pcache_line cl = c->LRU_head[set_index];
			for (int i = 0; i < c->set_contents[set_index]; i++)
			{
				if (cl->tag == tag)
				{
					found = 1;
					break;
				}
				cl = cl->LRU_next;
			}

			// if hit
			if (found)
			{
				apply_lru(c, set_index, cl);
			}
			else
			{
				Pcache_line line = malloc(sizeof(cache_line));
				line->tag = tag;
				line->dirty = 0;

				insert(&(c->LRU_head[set_index]), &(c->LRU_tail[set_index]), line);
				c->set_contents[set_index]++;
				c->contents++;

				cache_stat_inst.demand_fetches += block_word_size;
				cache_stat_inst.misses++;
			}
		}
	}
}

/************************************************************/
void perform_access_load_data(Pcache c, unsigned addr, unsigned int set_index, unsigned int block_word_size, unsigned int tag)
{
	cache_stat_data.accesses++;

	// if set is emtpy
	if (c->LRU_head[set_index] == 0)
	{
		Pcache_line line = malloc(sizeof(cache_line));
		line->tag = tag;
		line->dirty = 0;

		c->set_contents[set_index] = 1;
		insert(&c->LRU_head[set_index], &c->LRU_tail[set_index], line);

		cache_stat_data.misses++;
		cache_stat_data.demand_fetches += block_word_size;
	}
	else
	{
		// if the set is full
		if (c->set_contents[set_index] == c->associativity)
		{
			// find hit line.
			int found = 0;
			Pcache_line cl = c->LRU_head[set_index];
			for (int i = 0; i < c->set_contents[set_index]; i++)
			{
				if (cl->tag == tag)
				{
					found = 1;
					break;
				}
				cl = cl->LRU_next;
			}

			if (found)
			{
				apply_lru(c, set_index, cl);
			}
			else
			{
				Pcache_line line = malloc(sizeof(cache_line));
				line->tag = tag;
				line->dirty = 0;

				if (c->LRU_tail[set_index]->dirty)
				{
					cache_stat_data.copies_back += block_word_size;
				}

				// replace the exist LRU item with new line.
				delete (&c->LRU_head[set_index], &c->LRU_tail[set_index], c->LRU_tail[set_index]);
				insert(&(c->LRU_head[set_index]), &(c->LRU_tail[set_index]), line);

				cache_stat_data.demand_fetches += block_word_size;
				cache_stat_data.misses++;
				cache_stat_data.replacements++;
			}
		}
		else
		{
			// find hit line
			int found = 0;
			Pcache_line cl = c->LRU_head[set_index];
			for (int i = 0; i < c->set_contents[set_index]; i++)
			{
				if (cl->tag == tag)
				{
					found = 1;
					break;
				}
				cl = cl->LRU_next;
			}

			if (found)
			{
				apply_lru(c, set_index, cl);
			}
			else
			{
				Pcache_line line = malloc(sizeof(cache_line));
				line->tag = tag;
				line->dirty = 0;

				insert(&(c->LRU_head[set_index]), &(c->LRU_tail[set_index]), line);
				c->set_contents[set_index]++;
				c->contents++;

				cache_stat_data.demand_fetches += block_word_size;
				cache_stat_data.misses++;
			}
		}
	}
}

/************************************************************/
void perform_access_store_data(Pcache c, unsigned addr, unsigned int set_index, unsigned int block_word_size, unsigned int tag)
{
	cache_stat_data.accesses++;

	// if set is empty
	if (c->LRU_head[set_index] == 0)
	{
		if (cache_writealloc == 0)
		{
			cache_stat_data.copies_back += 1;
			cache_stat_data.misses++;
		}

		else
		{
			Pcache_line line = malloc(sizeof(cache_line));

			line->tag = tag;
			line->dirty = 1;

			// modify to the cache memory
			if (cache_writeback == 0)
			{
				cache_stat_data.copies_back += 1;
				line->dirty = 0;
			}
			c->set_contents[set_index] = 1;
			insert(&c->LRU_head[set_index], &c->LRU_tail[set_index], line);

			cache_stat_data.misses++;
			cache_stat_data.demand_fetches += block_word_size;
		}
	}
	else
	{
		// if set is full
		if (c->set_contents[set_index] == c->associativity)
		{
			// find hit line
			int found = 0;
			Pcache_line cl = c->LRU_head[set_index];
			for (int i = 0; i < c->set_contents[set_index]; i++)
			{
				if (cl->tag == tag)
				{
					found = 1;
					break;
				}
				cl = cl->LRU_next;
			}
			if (found)
			{
				// apply LRU
				apply_lru(c, set_index, cl);

				c->LRU_head[set_index]->dirty = 1;
				if (cache_writeback == 0)
				{
					cache_stat_data.copies_back += 1;
					c->LRU_head[set_index]->dirty = 0;
				}
			}
			else
			{
				if (cache_writealloc == 0)
				{
					cache_stat_data.copies_back += 1;
					cache_stat_data.misses++;
				}
				else
				{
					Pcache_line line = malloc(sizeof(cache_line));
					line->tag = tag;
					line->dirty = 1;

					if (c->LRU_tail[set_index]->dirty)
					{
						cache_stat_data.copies_back += block_word_size;
					}
					if (cache_writeback == 0)
					{
						cache_stat_data.copies_back += 1;
						line->dirty = 0;
					}
					delete(&c->LRU_head[set_index], &c->LRU_tail[set_index], c->LRU_tail[set_index]);
					insert(&(c->LRU_head[set_index]), &(c->LRU_tail[set_index]), line);

					cache_stat_data.demand_fetches += block_word_size;
					cache_stat_data.misses++;
					cache_stat_data.replacements++;
				}
			}
		}
		else
		{
			// find hit line.
			int found = 0;
			Pcache_line cl = c->LRU_head[set_index];
			for (int i = 0; i < c->set_contents[set_index]; i++)
			{
				if (cl->tag == tag)
				{
					found = 1;
					break;
				}
				cl = cl->LRU_next;
			}

			if (found)
			{
				// apply LRU
				apply_lru(c, set_index, cl);

				c->LRU_head[set_index]->dirty = 1;
				if (cache_writeback == 0)
				{
					cache_stat_data.copies_back += 1;
					c->LRU_head[set_index]->dirty = 0;
				}
			}
			else
			{
				if (cache_writealloc == 0)
				{
					cache_stat_data.misses++;
					cache_stat_data.copies_back += 1;
				}

				else
				{
					Pcache_line line = malloc(sizeof(cache_line));
					line->tag = tag;
					line->dirty = 1;

					if (cache_writeback == 0)
					{
						cache_stat_data.copies_back += 1;
						line->dirty = 0;
					}
					insert(&(c->LRU_head[set_index]), &(c->LRU_tail[set_index]), line);
					
					c->set_contents[set_index]++;
					c->contents++;

					cache_stat_data.demand_fetches += block_word_size;
					cache_stat_data.misses++;
				}
			}
		}
	}
}

/************************************************************/
void perform_access_unified(Pcache c, unsigned int addr, unsigned access_type)
{
	int block_word_size = cache_block_size / WORD_SIZE;
	int set_index = (addr & c->index_mask) >> c->index_mask_offset;
	int tag = addr & 0xFFFFFFFF << (LOG2(cache_block_size) + LOG2(c->n_sets));
	
	switch (access_type)
	{
	case TRACE_INST_LOAD:
		process_access_load_instruction(c, addr, set_index, block_word_size, tag);
		break;

	case TRACE_DATA_LOAD:
		perform_access_load_data(c, addr, set_index, block_word_size, tag);
		break;

	case TRACE_DATA_STORE:
		perform_access_store_data(c, addr, set_index, block_word_size, tag);
		break;
	}
}

/************************************************************/

/************************************************************/
void perform_access_split(Pcache c_data, Pcache c_inst, unsigned int addr, unsigned int access_type)
{
	unsigned int data_set_index, inst_set_index;
	unsigned int data_tag, inst_tag;

	int block_word_size = cache_block_size / WORD_SIZE;
	
	data_tag = addr & (0xFFFFFFFF << (LOG2(cache_block_size) + LOG2(c_data->n_sets)));
	inst_tag = addr & (0xFFFFFFFF << (LOG2(cache_block_size) + LOG2(c_inst->n_sets)));

	data_set_index = (addr & c_data->index_mask) >> c_data->index_mask_offset;
	inst_set_index = (addr & c_inst->index_mask) >> c_inst->index_mask_offset;

	switch (access_type)
	{
	case TRACE_INST_LOAD:
		process_access_load_instruction(c_inst, addr, inst_set_index, block_word_size, inst_tag);
		break;
	case TRACE_DATA_LOAD:
		perform_access_load_data(c_data, addr, data_set_index, block_word_size, data_tag);
		break;
	case TRACE_DATA_STORE:
		perform_access_store_data(c_data, addr, data_set_index, block_word_size, data_tag);
		break;
	}
}

/************************************************************/
void perform_access(addr, access_type) 
unsigned addr, access_type;
{
	// handle the access to the cache
	if (cache_split)
	{// if data cache and instruction cache are used seperately
		perform_access_split(&c1, &c2, addr, access_type);
	}
	else
	{// if data cache and instruction cache are used integrally,
		perform_access_unified(&c1, addr, access_type);
	}
}
/************************************************************/

/************************************************************/
void flush()
{
	/* flush the cache */
	int block_word_size = cache_block_size / WORD_SIZE;

	Pcache_line cl;
	for (int i = 0; i < c1.n_sets; i++)
	{
		cl = c1.LRU_head[i];
		for (int j = 0; j < c1.set_contents[i]; j++)
		{
			if (cl->dirty)
				cache_stat_data.copies_back += block_word_size;
			cl = cl->LRU_next;
		}
	}

	if (cache_split)
	{
		for (int j = 0; j < c2.n_sets; j++)
		{
			cl = c2.LRU_head[j];
			for (int f = 0; f < c2.set_contents[j]; f++)
			{
				if (cl->dirty)
					cache_stat_data.copies_back += block_word_size;
				cl = cl->LRU_next; // move the node
			}
		}
	}
}
/************************************************************/

/************************************************************/
void delete (head, tail, item)
	Pcache_line *head,
	*tail;
Pcache_line item;
{
	if (item->LRU_prev)
	{
		item->LRU_prev->LRU_next = item->LRU_next;
	}
	else
	{
		/* item at head */
		*head = item->LRU_next;
	}

	if (item->LRU_next)
	{
		item->LRU_next->LRU_prev = item->LRU_prev;
	}
	else
	{
		/* item at tail */
		*tail = item->LRU_prev;
	}
}
/************************************************************/

/************************************************************/
/* inserts at the head of the list */
void insert(head, tail, item)
	Pcache_line *head,
	*tail;
Pcache_line item;
{
	item->LRU_next = *head;
	item->LRU_prev = (Pcache_line)0;

	if (item->LRU_next)
		item->LRU_next->LRU_prev = item;
	else
		*tail = item;

	*head = item;
}
/************************************************************/

/************************************************************/
void set_cache_param(param, value) int param;
int value;
{

	switch (param)
	{
	case CACHE_PARAM_BLOCK_SIZE:
		cache_block_size = value;
		words_per_block = value / WORD_SIZE;
		break;
	case CACHE_PARAM_USIZE:
		cache_split = 0;
		cache_usize = value;
		break;
	case CACHE_PARAM_ISIZE:
		cache_split = 1;
		cache_isize = value;
		break;
	case CACHE_PARAM_DSIZE:
		cache_split = 1;
		cache_dsize = value;
		break;
	case CACHE_PARAM_ASSOC:
		cache_assoc = value;
		break;
	case CACHE_PARAM_WRITEBACK:
		cache_writeback = 1;
		break;
	case CACHE_PARAM_WRITETHROUGH:
		cache_writeback = 0;
		break;
	case CACHE_PARAM_WRITEALLOC:
		cache_writealloc = 1;
		break;
	case CACHE_PARAM_NOWRITEALLOC:
		cache_writealloc = 0;
		break;
	default:
		printf("error set_cache_param: bad parameter value\n");
		exit(-1);
	}
}
/************************************************************/

/************************************************************/
void dump_settings()
{
	printf("*** CACHE SETTINGS ***\n");
	if (cache_split)
	{
		printf("  Split I- D-cache\n");
		printf("  I-cache size: \t%d\n", cache_isize);
		printf("  D-cache size: \t%d\n", cache_dsize);
	}
	else
	{
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
