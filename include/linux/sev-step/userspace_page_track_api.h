#ifndef USERSPACE_PAGE_TRACK_API
#define USERSPACE_PAGE_TRACK_API

#include <uapi/linux/sev-step/sev-step.h>
typedef struct {
	//process id
	int pid;
	//memory region which is shared by kernelspace and userspace
	shared_mem_region_t *shared_mem_region;
	//next id
	uint64_t next_id;
	//if true, the api will be forced to reset
	int force_reset;

	//just for internal use. Used to remember get_user_pages_unlocked
	//pages to be able to unpinn it on ctx destruction
	struct page **_pages_for_shared_mem;
	int _pages_for_shared_mem_len;
} usp_poll_api_ctx_t;
extern usp_poll_api_ctx_t *uspt_ctx;

/* SEV-STEP API FUNCTIONS */

/**
 * @brief Initializes a usp_poll_api_ctx_t which is required for all API calls. Assumes that the shared_mem_region
 * is still a user space pointer/vaddr.
 * 
 * @param pid pid of calling user space process
 * @param shared_mem_region user space pointer pointer to shared memory region
 * @param ctx caller allocated result param that is initialized in this function
 * @return int 0 on success
 */
int usp_poll_init_user_vaddr(int pid,uint64_t user_vaddr_shared_mem,usp_poll_api_ctx_t* ctx);

/**
 * @brief Initializes a usp_poll_api_ctx_t which is required for all API calls. Assumes
 * kernel readable pointer to shared mem. See usp_poll_init_user_vaddr if you only have a user space
 * vaddr.
 * @param pid pid of calling user space process
 * @param shared_mem_region pointer to shared memory region. Pointer must be accessible from kernel space
 * @param ctx caller allocated result param that is initialized in this function
 * @return int 0 on success
 */
int usp_poll_init_kern_vaddr(int pid, shared_mem_region_t* shared_mem_region ,usp_poll_api_ctx_t* ctx);

/**
 * @brief Frees resources hold by the ctx
 * 
 * @param ctx ctx to clean up
 * @return int 0 on success
 */
int usp_poll_close_api(usp_poll_api_ctx_t* ctx);

/**
 * @brief Signal availability of the supplied event and block untill receiver has send ack for it
 * 
 * @param ctx ctx to operate on
 * @param event_type type of event
 * @param event event struct matching the supplied type
 * @return int 0 if event was ack'ed by receiver
 */
int usp_send_and_block(usp_poll_api_ctx_t* ctx, usp_event_type_t event_type, void* event);

/**
 * @brief Determine the size of a supported event
 * 
 * @param event_type type of event
 * @param size pointer to save the size
 * @return int 0 on success
 */
int get_size_for_event(usp_event_type_t event_type, uint64_t *size);

/**
 * @brief Check if the usp_poll_api_ctx_t is initialized
 * 
 * @return int 1 if initialized
 */
int ctx_initialized(void);

#endif