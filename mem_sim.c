/***************************************************************************
 * *    Inf2C-CS Coursework 2: Cache Simulation
 * *
 * *    Instructor: Boris Grot
 * *
 * *    TA: Siavash Katebzadeh
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>

/*
 * Various structures
 */
typedef enum {FIFO, LRU, Random} replacement_p;

const char* get_replacement_policy(uint32_t p) {
    switch(p) {
    case FIFO: return "FIFO";
    case LRU: return "LRU";
    case Random: return "Random";
    default: assert(0); return "";
    };
    return "";
}

typedef struct {
    uint32_t address;
} mem_access_t;

typedef struct {
    uint32_t cache_hits;
    uint32_t cache_misses;
} result_t;


/*
 * Parameters for the cache
 */

replacement_p replacement_policy = FIFO;
uint32_t associativity = 0;
uint32_t number_of_cache_blocks = 0;
uint32_t cache_block_size = 0;

uint32_t g_num_cache_tag_bits = 0;
uint32_t g_cache_offset_bits= 0;
result_t g_result;


/* Reads a memory access from the trace file and returns
 * 32-bit physical memory address
 */
mem_access_t read_transaction(FILE *ptr_file) {
    char buf[1002];
    char* token = NULL;
    char* string = buf;
    mem_access_t access;

    if (fgets(buf, 1000, ptr_file)!= NULL) {
        /* Get the address */
        token = strtok(string, " \n");
        access.address = (uint32_t)strtoul(token, NULL, 16);
        return access;
    }

    /* If there are no more entries in the file return an address 0 */
    access.address = 0;
    return access;
}

void print_statistics(uint32_t num_cache_tag_bits, uint32_t cache_offset_bits, result_t* r) {
    uint32_t cache_total_hits = r->cache_hits;
    uint32_t cache_total_misses = r->cache_misses;
    printf("CacheTagBits:%u\n", num_cache_tag_bits);
    printf("CacheOffsetBits:%u\n", cache_offset_bits);
    printf("Cache:hits:%u\n", r->cache_hits);
    printf("Cache:misses:%u\n", r->cache_misses);
    printf("Cache:hit-rate:%2.1f%%\n", cache_total_hits / (float)(cache_total_hits + cache_total_misses) * 100.0);
}

// address variables
uint32_t g_cache_index_bits;
uint32_t idx;
uint32_t tag;


// cache state variables
_Bool populated;
_Bool hit;

// update_stats function updates hit or miss stats according to input boolean
void update_stats(_Bool hit) {
    if (hit) {
        g_result.cache_hits += 1;
    } else {
        g_result.cache_misses += 1;
    }
}

int main(int argc, char** argv) {
    time_t t;
    /* Intializes random number generator */
    srand((unsigned) time(&t));

    /*
     *
     * Read command-line parameters and initialize configuration variables.
     *
     */
    int improper_args = 0;
    char file[10000];
    if (argc < 6) {
        improper_args = 1;
        printf("Usage: ./mem_sim [replacement_policy: FIFO LRU Random] [associativity: 1 2 4 8 ...] [number_of_cache_blocks: 16 64 256 1024] [cache_block_size: 32 64] mem_trace.txt\n");
    } else  {
        /* argv[0] is program name, parameters start with argv[1] */
        if (strcmp(argv[1], "FIFO") == 0) {
            replacement_policy = FIFO;
        } else if (strcmp(argv[1], "LRU") == 0) {
            replacement_policy = LRU;
        } else if (strcmp(argv[1], "Random") == 0) {
            replacement_policy = Random;
        } else {
            improper_args = 1;
            printf("Usage: ./mem_sim [replacement_policy: FIFO LRU Random] [associativity: 1 2 4 8 ...] [number_of_cache_blocks: 16 64 256 1024] [cache_block_size: 32 64] mem_trace.txt\n");
        }
        associativity = atoi(argv[2]);
        number_of_cache_blocks = atoi(argv[3]);
        cache_block_size = atoi(argv[4]);
        strcpy(file, argv[5]);
    }
    if (improper_args) {
        exit(-1);
    }
    assert(number_of_cache_blocks == 16 || number_of_cache_blocks == 64 || number_of_cache_blocks == 256 || number_of_cache_blocks == 1024);
    assert(cache_block_size == 32 || cache_block_size == 64);
    assert(number_of_cache_blocks >= associativity);
    assert(associativity >= 1);

    printf("input:trace_file: %s\n", file);
    printf("input:replacement_policy: %s\n", get_replacement_policy(replacement_policy));
    printf("input:associativity: %u\n", associativity);
    printf("input:number_of_cache_blocks: %u\n", number_of_cache_blocks);
    printf("input:cache_block_size: %u\n", cache_block_size);
    printf("\n");

    /* Open the file mem_trace.txt to read memory accesses */
    FILE *ptr_file;
    ptr_file = fopen(file,"r");
    if (!ptr_file) {
        printf("Unable to open the trace file: %s\n", file);
        exit(-1);
    }

    memset(&g_result, 0, sizeof(result_t));

    // block structure definition
    typedef struct {
        _Bool valid;
        uint32_t tag;
    } block;

    // calcualte length of index, offset and tag
    g_cache_index_bits = log2(number_of_cache_blocks / associativity);
    g_cache_offset_bits = log2(cache_block_size);
    g_num_cache_tag_bits = 32 - g_cache_index_bits - g_cache_offset_bits;

    // for loop variables
    int i;
    int j;
    
    // define dynamic array of pointers to blocks and initialize every block to have data values of 0
    block** cache = (block**) malloc((number_of_cache_blocks / associativity) * sizeof(block*));
    for (i = 0; i < number_of_cache_blocks / associativity; ++i) {
        cache[i] = (block*) malloc(associativity * sizeof(block));
        
        for (j = 0; j < associativity; ++j) {
            cache[i][j] = (block) { 0, 0 };
        } 
    }

    // define replacement parameters for LRU and FIFO replacement policies

    // define dynamic array of triple pointers representing a queue for the LRU block in each set, a temporary block pointer, and the index of the LRU in the LRU_queue for the LRU replacement policy
    block*** LRU_queue = (block***) malloc((number_of_cache_blocks / associativity) * sizeof(block**));
    block* LRU_temp;
    int LRU_idx;
    
    // initialize each pointer in LRU_queue to point to the adress of its respective cache block
    for (i = 0; i < number_of_cache_blocks / associativity; ++i) {
        LRU_queue[i] = (block**) malloc(associativity * sizeof(block*));

        for (j = 0; j < associativity; ++j) {
            LRU_queue[i][j] = &cache[i][j];
        } 
    }
    
    // define dynamic array representing the indexes of the next block to be evicted in the FIFO replacement policy, and initialize each entry in array to 0
    int* FIFO_queue = malloc((number_of_cache_blocks / associativity) * sizeof(int));
    for (i = 0; i < number_of_cache_blocks / associativity; ++i) {
        FIFO_queue[i] = 0;
    }
    

    
    mem_access_t access;
    /* Loop until the whole trace file has been read. */
    while(1) {
        access = read_transaction(ptr_file);
        // If no transactions left, break out of loop.
        if (access.address == 0)
            break;

        // for every loop 'populated' boolean is set to true and 'hit' boolean is set to false
        populated = 1;
        hit = 0;

        // extract the tag and index values from the current address
        tag = access.address >> (g_cache_index_bits + g_cache_offset_bits);
        idx = (access.address << g_num_cache_tag_bits) >> (g_num_cache_tag_bits + g_cache_offset_bits);

        // for loop that checks for a 'hit' or for allocation space in the set to which the index from the address corresponds to
        for (i = 0; i < associativity; ++i) {

            // if block is not in use, allocate data to that block and set 'populated' boolean to false
            if (cache[idx][i].valid == 0) {
                cache[idx][i] = (block) { 1, tag};
                populated = 0;

                // if LRU policy is being utilized, update LRU
                if (replacement_policy == LRU) {
                    
                    // find idx of LRU in LRU_queue
                    for (j = 0; j < associativity; ++j) {
                        if (LRU_queue[idx][j]->tag == tag) {
                            LRU_temp = LRU_queue[idx][j];
                            LRU_idx = j;
                            break; 
                        }
                    }

                    // update LRU_queue
                    for (j = LRU_idx; j < associativity - 1; ++j) {
                        LRU_queue[idx][j] = LRU_queue[idx][j+1];
                    }
                    LRU_queue[idx][associativity - 1] = LRU_temp;
                }
                break;
            }

            // if a 'hit' was found set 'hit' boolean to true
            if (cache[idx][i].valid == 1 && cache[idx][i].tag == tag) {
                hit = 1;

                // if LRU policy is being utilized, update LRU
                if (replacement_policy == LRU) {

                    // find idx of LRU in LRU_queue
                    for (j = 0; j < associativity; ++j) {
                        if (LRU_queue[idx][j]->tag == tag) {
                            LRU_temp = LRU_queue[idx][j];
                            LRU_idx = j;
                            break; 
                        }
                    }

                    // update LRU_queue
                    for (j = LRU_idx; j < associativity - 1; ++j) {
                        LRU_queue[idx][j] = LRU_queue[idx][j+1];
                    }
                    LRU_queue[idx][associativity - 1] = LRU_temp;
                }
                break;
            }
        }

        // update hit and miss statistics by passing 'hit' boolean to update_stats function
        update_stats(hit);

        // if current cache set is fully populated and a 'hit' was not found, evict a block in the set according to current replacement policy
        if (populated && !hit) {

            // if replacement policy is FIFO, evict block specified by the value at the current address index in the FIFO_queue array and update index accordingly
            if (replacement_policy == FIFO) {
                cache[idx][FIFO_queue[idx]].tag = tag;
                FIFO_queue[idx] = (FIFO_queue[idx] + 1) % associativity;
            }

            // if replacement policy is LRU, evict block pointed at by the block pointer at index 0 of the current address index of LRU_queue and update LRU_queue accordingly
            if (replacement_policy == LRU) {
                LRU_queue[idx][0]->tag = tag; 
                LRU_temp = LRU_queue[idx][0];

                for (i = 0; i < associativity - 1; ++i) {
                    LRU_queue[idx][i] = LRU_queue[idx][i+1];
                }
                LRU_queue[idx][associativity-1] = LRU_temp;
            }

            // if replacement polify is Random, evict block by randomly generating an index between 0 and the size of the set
            if (replacement_policy == Random) {
                cache[idx][rand() % associativity].tag = tag;
            }
        }
    }

    // free allocated memory after use
    for (i = 0; i < number_of_cache_blocks / associativity; ++i) {
        free(cache[i]);
    }
    free(cache);

    for (i = 0; i < number_of_cache_blocks / associativity; ++i) {
        free(LRU_queue[i]);
    }
    free(LRU_queue);
    
    free(FIFO_queue);
    
    /* Make sure that all the parameters are appropriately populated. */
    print_statistics(g_num_cache_tag_bits, g_cache_offset_bits, &g_result);

    /* Close the trace file. */
    fclose(ptr_file);
    return 0;
}