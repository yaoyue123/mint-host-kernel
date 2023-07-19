#include <linux/sev-step/libcache.h>

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/random.h>

EXPORT_SYMBOL(cpu_probe_pointer_chasing);
EXPORT_SYMBOL(cpu_probe_pointer_chasing_inplace);
EXPORT_SYMBOL(cpu_probe_pointer_chasing_remote);
EXPORT_SYMBOL(cpu_prime_pointer_chasing);
EXPORT_SYMBOL(cpu_fillEvSet);
EXPORT_SYMBOL(start_counting_thread);
EXPORT_SYMBOL(stop_counting_thread);
EXPORT_SYMBOL(cpu_maccess);
EXPORT_SYMBOL(cpu_warm_up);
EXPORT_SYMBOL(cpu_probe_array_individual);
EXPORT_SYMBOL(cpu_prime_array);
EXPORT_SYMBOL(cpu_fillEvSetRandomized);


int cnt_thread_cpu = 73;
struct task_struct *cnt_thread = NULL;

static int cpu_counter_loop_thread_wrapper(void* context) {
    cpu_counter_loop();
    printk("counting thread terminated gracefully\n");
    return 0;
}

void start_counting_thread() {
    if(cnt_thread) {
        printk("cnt_thread already running.\n");
        return;
    }
    counter_thread_var = 0;
    cnt_thread = kthread_create(cpu_counter_loop_thread_wrapper, NULL, "cnt thread");
    if( cnt_thread == NULL) {
        printk("kthread_create for counting thread failed!\n");
        return;
    }
    kthread_bind(cnt_thread, cnt_thread_cpu);
    
    if ( cnt_thread ) {
        counter_thread_abort = 0; //reset variable used for stopping this thread
        printk("starting counting thread!\n");
        wake_up_process(cnt_thread);
        //paranoid: wait until counter has started
        while( cpu_cnt_thread_value() == 0 ) {;}
    } else {
        printk("Failed to wake up counting thread\n");
    }
}

void stop_counting_thread() {
    counter_thread_abort = 1; //periodically checked by the counting thread
    //as the thread only reads we should be fine without a mutex
    cnt_thread = NULL;
    printk("signaled counting thread to stop\n");
}


#define get_bit(x, i)  (((size_t)(x) & (1 << (i))) >> (i))

uint64_t get_utag( uint64_t addr)
{
  return ((get_bit(addr, 15) ^ get_bit(addr, 20)) << 0) |
         ((get_bit(addr, 16) ^ get_bit(addr, 21)) << 1) |
         ((get_bit(addr, 17) ^ get_bit(addr, 22)) << 2) |
         ((get_bit(addr, 18) ^ get_bit(addr, 23)) << 3) |
         ((get_bit(addr, 19) ^ get_bit(addr, 24)) << 4) |
         ((get_bit(addr, 14) ^ get_bit(addr, 25)) << 5) |
         ((get_bit(addr, 13) ^ get_bit(addr, 26)) << 6) |
         ((get_bit(addr, 12) ^ get_bit(addr, 27)) << 7);
}


void cpu_fillEvSetRandomized(void **start, void **startReverse, addr_list_t *eviction_set) {
    
    int i,j;
    uintptr_t *p;

    if (eviction_set->length > 0) {
        addr_list_entry_t *e;
        uintptr_t *addresses = (uintptr_t*) kmalloc(
            eviction_set->length * sizeof(uintptr_t),
            GFP_KERNEL
        );
        memset(addresses, 0, eviction_set->length *sizeof(uintptr_t));

        i = 0;
        for (e = eviction_set->first; e != 0; e = e->next) {
            unsigned int index = get_random_int() % eviction_set->length;
            if (addresses[index] == 0) {
                addresses[index] = e->addr;
            } else {
                while (addresses[i] != 0) {
                    i++;
                }
                addresses[i] = e->addr;
            }
        }

        // forward
        *start = (uintptr_t*) addresses[0];
        p = *start;

        for (j = 1; j < eviction_set->length; j++) {
            *p = addresses[j];
            p = (uintptr_t*) *p;
        }
        *p = 0;
        //backward
        if (startReverse != 0) {
            uintptr_t *pReverse;
            int j;
            *startReverse = ((uintptr_t*) addresses[eviction_set->length - 1])
                    + 1;
            pReverse = *startReverse;

            for (j = eviction_set->length - 2; j >= 0; j--) {
                *pReverse = addresses[j] + sizeof(uintptr_t);
                pReverse = (uintptr_t*) *pReverse;
            }
            *pReverse = 0;
        }

        kfree(addresses);
    } else {
        *start = 0;
        if (startReverse != 0) {
            *startReverse = 0;
        }

    }
}



void cpu_fillEvSet(void **start, void **startReverse,
        addr_list_t *eviction_set) {

    if (eviction_set->length > 0) {
        addr_list_entry_t *e;
        uintptr_t *p;
        // forward
        *start = (uintptr_t*) eviction_set->first->addr;
        p = *start;

        for (e = eviction_set->first; e->next != NULL;
                e = e->next) {

            *p = e->next->addr;
            p = (uintptr_t*) *p;
        }
        *p = 0;

        // backward
        if (startReverse != 0) {
            uintptr_t *pReverse;
            //+1 becaus +0 already contains the ptr from the forward direction
            *startReverse = ((uintptr_t*) eviction_set->last->addr) + 1;
            pReverse = *startReverse;

            for (e = eviction_set->last; e->prev != NULL; e =
                    e->prev) {

                *pReverse = e->prev->addr + sizeof(uintptr_t); //equivalent to + 1
                pReverse = (uintptr_t*) *pReverse;
            }
            *pReverse = 0;
        }
    } else {
        *start = 0;
        if (startReverse != 0) {
            *startReverse = 0;
        }
    }
}



void initAddrList(addr_list_t *list) {
    list->length = 0;
    list->first = 0;
    list->last = 0;
}

void freeAddrListEntries(addr_list_t *list) {
    while (list->length > 0) {
        remove_end(list);
    }
}

void deepCopyList(addr_list_t *from, addr_list_t *to) {
    addr_list_entry_t *e;
    for (e = from->first; e != 0; e = e->next) {
        insert_end(to, e->addr);
    }
}

void insert_end(addr_list_t *list, uint64_t addr) {
    addr_list_entry_t *entry = (addr_list_entry_t*) kmalloc(
            sizeof(addr_list_entry_t),GFP_KERNEL);
    entry->addr = addr;
    entry->next = 0;
    entry->prev = list->last;
    list->last = entry;
    if (entry->prev)
        entry->prev->next = entry;
    if (!list->first)
        list->first = entry;
    list->length++;
}
EXPORT_SYMBOL(insert_end);

void insert_front(addr_list_t *list, uint64_t addr) {
    addr_list_entry_t *entry = (addr_list_entry_t*) kmalloc(
            sizeof(addr_list_entry_t),GFP_KERNEL);
    entry->addr = addr;
    entry->next = list->first;
    entry->prev = 0;
    list->first = entry;
    if (entry->next)
        entry->next->prev = entry;
    if (!list->last)
        list->last = entry;
    list->length++;
}

void remove_middle(addr_list_t *list, addr_list_entry_t *e) {
    if (!e->prev)
        list->first = e->next;
    else
        e->prev->next = e->next;

    if (!e->next)
        list->last = e->prev;
    else
        e->next->prev = e->prev;
    kfree(e);
    list->length--;
}

void remove_end(addr_list_t *list) {
    if (list->last)
        remove_middle(list, list->last);
}





int kernel_get_colliding_vaddr(uint64_t victim_vaddr, uint64_t *result) {
    int tries,i,offset;
    uint8_t victim_utag;
    uint64_t cache_set_mask;
    cache_set_mask = (0x1ULL << 6) | (0x1ULL << 7) | (0x1ULL << 8) | (0x1ULL << 9) | (0x1ULL << 10) | (0x1ULL << 11);
    victim_utag = get_utag(victim_vaddr);

    tries = 100000;
    for( i = 0; i < tries; i++ ) {
        uint8_t * candidate;
        cond_resched();

        candidate = vmalloc(4096);
        if( candidate == NULL) {
            printk("kernel_get_colliding_vadr, failed to alloc\n");
            return 0;
        }
        if( i % 1000 == 0 ) {
            printk("At try %d of %d\n",i,tries);
        }
        //first filter by cache set, then by utag
        for( offset =0; offset < 4096 - 64; offset += 64) {
            uint8_t candidate_utag;
            if( ((victim_vaddr) & cache_set_mask) == ( (uint64_t)(candidate+offset) & cache_set_mask )  ) {
                 candidate_utag = get_utag((uint64_t)(candidate+offset));
                 if( candidate_utag == victim_utag ) {
                    *result = (uint64_t)(candidate+offset);
                    //let the memory leaks begin \o/
                    return 1;
                }
            }
           
        }
        vfree(candidate);
    }
    //if we come here we did not find valid
    return 0; 
}

void kernel_free_colliding_vaddr(uint64_t colliding_vaddr) {
    //addr can be an offset in a unique, page aligned buffer, so free
    //from page aligned start
    vfree((void*)(colliding_vaddr & ~(0xfffULL)));
}

void free_eviction_sets(addr_list_t* eviction_sets,uint64_t len) {
	int attack_target_idx;
	addr_list_entry_t *e;
	for( attack_target_idx = 0; attack_target_idx < len; attack_target_idx++) {
		for( e = eviction_sets[attack_target_idx].first; e != NULL; e = e->next) {
			kernel_free_colliding_vaddr(e->addr);
		}
		freeAddrListEntries(&eviction_sets[attack_target_idx]);
	}
	kfree(eviction_sets);
}


int kernel_eviction_chase(uint64_t * victim_addrs,unsigned victim_addrs_length, void** chase) {
    unsigned i;
    uint64_t colliding_vaddr;
    addr_list_t list;
    initAddrList(&list);

    for( i = 0; i < victim_addrs_length; i++ ) {
        printk("calling kernel_get_colliding_vaddr for 0x%llx\n",victim_addrs[i]);
        if( kernel_get_colliding_vaddr(victim_addrs[i],&colliding_vaddr)) {
            insert_front(&list,colliding_vaddr);
            printk("success\n");
        } else {//failed to get colliding address; abort
            printk("failure\n");
            freeAddrListEntries(&list);
            return 0;
        }
    }
    cpu_fillEvSet(chase,NULL,&list);
    freeAddrListEntries(&list);
    return 1;
}