#ifndef LIBCACHE_H
#define LIBCACHE_H
#include <linux/types.h>
/*
 * Defines an entry of an address list.
 * Every entry contains a pointer to the previous and to 
 * the next element.
 * As this is a list of addresses, every entry also contains an address. 
 */
typedef struct addr_list_entry_s {
    struct addr_list_entry_s *next;
    struct addr_list_entry_s *prev;
    uint64_t addr;
} addr_list_entry_t;

/*
 * Defines a list of addresses with a first/ last element
 * and the length.
 */
typedef struct {
    addr_list_entry_t *first;
    addr_list_entry_t *last;
    int length;
} addr_list_t;

/**
 * @brief Get the utag for virtual address
 * 
 * @param addr virtual address
 * @return uint64_t  utag of virtual address
 */
uint64_t get_utag( uint64_t addr);


/**
 * @brief Takes the addresses from @eviction_set and builds a pointer chasing
structure in @start
 * 
 * @param start filled with first element of pointer chase
 * @param eviction_set fill the chase structures with the elements form this 
 * eviction set
 */
void cpu_fillEvSet(void **start, void **startReverse,
        addr_list_t *eviction_set);
/**
 * @brief Like cpu_fillEvSet but with randomized order
 * 
 * @param start 
 * @param eviction_set 
 */
void cpu_fillEvSetRandomized(void **start, void **startReverse, addr_list_t *eviction_set);


void initAddrList(addr_list_t *list);
void freeAddrListEntries(addr_list_t *list);
void deepCopyList(addr_list_t *from, addr_list_t *to);
void insert_end(addr_list_t *list, uint64_t addr);
void insert_front(addr_list_t *list, uint64_t addr);
void remove_middle(addr_list_t *list, addr_list_entry_t *e);
void remove_end(addr_list_t *list);



void start_counting_thread(void);
void stop_counting_thread(void);

//functions from libcpu.s



/// @brief Counter variable incremented by the counting thread
extern volatile int64_t counter_thread_var;

/// @brief Used to signal the counting thread to stop 
extern volatile uint64_t counter_thread_abort;

/**
 * @brief Get current value of couting thread counter
 * 
 * @return uint64_t value of counter thread counter
 */
extern uint64_t cpu_cnt_thread_value(void);


/**
 * @brief Access a given address
 * 
 * @param addr vaddr to access
 */
extern inline void cpu_maccess(uintptr_t addr);

/*  */
/**
 * @brief Time memory access to the given address
 * 
 * @param addr vaddr to access
 * @return uint64_t time for accessing addr
 */
extern uint64_t cpu_maccess_time(uintptr_t addr);


/**
 * @brief Access linked eviction set list/pointer chase until NULL address
 * 
 * @param ptr linked list / pointer chase structure
 */
extern void cpu_prime_pointer_chasing(volatile void *ptr);

/**
 * @brief Access and time linked eviction set list until NULL address
 * 
 * @param ptr linked list / pointer chase structure
 * @return uint64_t total access time for all entries
 */
extern uint64_t cpu_probe_pointer_chasing(volatile void *ptr);


/**
 * @brief Access and time linked eviction set list untill NULL address. DESTRUCTIVE!
 * On traversal the list links are replaced with
 * the access time (i.e. (*ptr) no longer points to the next element but stores measured access time to ptr).
 * Also reads CTR_MSR_1 performance before and after access to enable using a performance counter instead of
 * timing for the cache attack
 * 
 * @param ptr linked list / pointer chase structure
 */
extern inline void cpu_probe_pointer_chasing_inplace(volatile void *ptr);

/**
 * @brief Like cpu_probe_pointer_chasing_inplace but we keep the pointer chase structure intact and store
 * the results in measurement_results. Usefull if we cannot write to the addrs in the chase, i.e. in aliased mapping
 * attack scenario
 * 
 * @param ptr linked list / pointer chase structure
 * @param measurement_results Caller allocated array twice the size of the linked list. We use two entries per linked list list.
 * The first one stores the time, the second one stores the perf counter diff
 */
extern inline void cpu_probe_pointer_chasing_remote(volatile void *ptr,
    uint64_t* measurement_results);

/**
 * @brief Measure time and evaluate perf counter for each addr in addrs
 * 
 * @param addrs array with vaddrs to access
 * @param addrs_len length of @addrs array
 * @param results Caller allocated array twice the size of @addrs. We use two entries per linked list list.
 * The first one stores the time, the second one stores the perf counter diff
 */
extern inline void cpu_probe_array_individual(uint64_t* addrs, uint64_t addrs_len, uint64_t* results);

/**
 * @brief Access each element in @addrs
 * 
 * @param addrs array with vaddrs to access
 * @param len length of @addrs array
 */
extern inline void cpu_prime_array(uint64_t* addrs, uint64_t len);

/**
 * @brief Start counter loop. Blocks untill counter_thread_abort != 0, so start
this in a seperate thread
 * 
 */
extern void cpu_counter_loop(void);

/**
 * @brief Tries to build a collide+probe eviction set for the vaddrs in @victim_addrs
and puts them in a pointer chasing struct
 * 
 * @param victim_addrs addrs to build eviction set for
 * @param victim_addrs_length  length of @victim_addrs
 * @param chase  Filled with linked list / pointer chase for priming/probing
 * @return int 0 on success
 */
int kernel_eviction_chase(uint64_t * victim_addrs,unsigned victim_addrs_length, void** chase);


/**
 * @brief Tries to find vaddr that is in same cache set and has the same utag and saves it in result. 
 * 
 * @param victim_vaddr find conflicting vaddr for this vaddr
 * @param result Result param, that is filled with the conflicting vaddr3 Caller must call vfree on
 * result & ~0xfffULL 
 * @return int 1 on success else 0
 */
int kernel_get_colliding_vaddr(uint64_t victim_vaddr, uint64_t *result);

/**
 * @brief Runs "inc" in a tight loop
 * 
 * @param iterations number of times "inc" should be executed
 */
extern void cpu_warm_up(uint64_t iterations);


#endif