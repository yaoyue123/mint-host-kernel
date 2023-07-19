#ifndef _UAPI__SEV_STEP_H
#define _UAPI__SEV_STEP_H

#include <linux/types.h>

#ifdef __KERNEL__
#include <uapi/asm/kvm_page_track.h>
#else
#include <asm/kvm_page_track.h>
#include <stdbool.h>
#include <stdint.h>
#endif

#define SEV_STEP_SHARED_MEM_BYTES (20 * 4096)
/**
 * @brief struct for storing the performance counter config values
 */
typedef struct {
	uint64_t HostGuestOnly;
	uint64_t CntMask;
	uint64_t Inv;
	uint64_t En;
	uint64_t Int;
	uint64_t Edge;
	uint64_t OsUserMode;
	uint64_t UintMask;
	uint64_t EventSelect; //12 bits in total split in [11:8] and [7:0]
	char* descriptive_name;
} perf_ctl_config_t;

typedef struct {
	uint64_t lookup_table_index;
	bool apic_timer_value_valid;
	uint32_t custom_apic_timer_value;
} do_cache_attack_param_t;

typedef struct {
	/// @brief Input Parameter. We want the HPA for this
	uint64_t in_gpa;
	/// @brief Result Parameter.
	uint64_t out_hpa;
} gpa_to_hpa_param_t;

/**
 * @brief Describe lookup table that can be targeted by a cache attack
 * 
 */
typedef struct {
	/// @brief guest vaddr where the lookup table starts
	uint64_t base_vaddr_table;
	/// @brief length of the lookup table in bytes
	uint64_t table_bytes;
} lookup_table_t;

typedef struct {
	/// @brief we build and l1d way predictor eviction for each target
	lookup_table_t *attack_targets;
	uint64_t attack_targets_len;
	/// @brief  configures the perf counter evaluated for the cache attack
	perf_ctl_config_t cache_attack_perf;
} build_eviction_set_param_t;

typedef struct {
	/// @brief flattened 2D array with the evictions sets.
	/// Every @import_user_eviction_set_param_t.way_count elements form one eviction set
	/// for each cache set covered by the lookup_table
	uint64_t *eviction_sets;
	/// @brief length of eviction_sets
	uint64_t eviction_sets_len;
} lookup_table_eviction_set_t;

typedef struct {
	/// @brief we build and l1d way predictor eviction for each target
	lookup_table_t *attack_targets;
	/// @brief eviction sets for the supplied attack_targets
	lookup_table_eviction_set_t *eviction_sets;
	/// @brief len of both attack_targets and eviction_sets
	uint64_t len;
	/// @brief ways of the attacked cache
	uint64_t way_count;
	/// @brief  configures the perf counter evaluated for the cache attack
	perf_ctl_config_t cache_attack_perf;
} import_user_eviction_set_param_t;

typedef enum {
	VRN_RFLAGS,
	VRN_RIP,
	VRN_RSP,
	VRN_R10,
	VRN_R11,
	VRN_R12,
	VRN_R13,
	VRN_R8,
	VRN_R9,
	VRN_RBX,
	VRN_RCX,
	VRN_RDX,
	VRN_RSI,
	VRN_CR3,
	VRN_MAX, //not a register; used to size sev_step_partial_vmcb_save_area_t.register_values
} vmsa_register_name_t;

typedef struct {
	/// @brief indexed by vmsa_register_name_t
	uint64_t register_values[VRN_MAX];
	bool failed_to_get_data;
} sev_step_partial_vmcb_save_area_t;

/**
 * @brief struct for storing tracking parameters 
 * which are sent from userspace
 */
typedef struct {
	//guest physical address of page
	uint64_t gpa;
	//one of the track modes defined in enum kvm_page_track_mode
	int track_mode;
} track_page_param_t;

/**
 * @brief struct for storing tracking parameters 
 * for all pages which are sent from userspace
 */
typedef struct {
	//one of the track modes defined in enum kvm_page_track_mode
	int track_mode;
} track_all_pages_t;

/**
 * @brief enum for supported event types
 */
typedef enum {
	//used for page fault events
	PAGE_FAULT_EVENT,
	//used for single stepping events
	SEV_STEP_EVENT,
} usp_event_type_t;

/**
 * @brief Stores the structure of the shared memory 
 * region between kernelspace and userspace
 */
typedef struct {
	//lock for all of the other values in this struct
	int spinlock;
	//if true, we have a valid event stored
	int have_event;
	//if true, the receiver has acked the event
	int event_acked;
	//type of the stored event. Required to do the correct raw mem cast
	usp_event_type_t event_type;
	// buffer for the event
	uint8_t event_buffer[19 * 4096];
} shared_mem_region_t;

/**
 * @brief struct for storing parameters which are
 * needed for the initialization of the api
 */
typedef struct {
	//process id
	int pid;
	//the user defined shared memory address
	uint64_t user_vaddr_shared_mem;
	/// @brief if true, decrypt vmsa and send information with each event
	///only works if debug mode is active
	bool decrypt_vmsa;
} usp_init_poll_api_t;



/**
 * @brief struct for storing page fault parameters 
 * which are sent to userspace
 */
typedef struct {
	// gpa of the page fault
	uint64_t faulted_gpa;
	sev_step_partial_vmcb_save_area_t decrypted_vmsa_data;
	/// @brief if true, decrypted_vmsa_data contains valid data
	bool is_decrypted_vmsa_data_valid;
} usp_page_fault_event_t;

/**
 * @brief struct for storing sev-step event parameters
 * to send them to userspace
 */
typedef struct {
	uint32_t counted_instructions;
	sev_step_partial_vmcb_save_area_t decrypted_vmsa_data;
	/// @brief if true, decrypted_vmsa_data contains valid data
	bool is_decrypted_vmsa_data_valid;
	uint64_t *cache_attack_timings;
	uint64_t *cache_attack_perf_values;
	/// @brief length of both cache_attack_timings and
	/// cache_attack_perf_values
	uint64_t cache_attack_data_len;
	/************* NEMESIS ***************/
	uint64_t tsc_latency;
	/************************************/
} sev_step_event_t;

/**
 * @brief struct for storing sev-step config parameters
 * which are sent from userspace
 */
typedef struct {
	// apic timer value
	uint32_t tmict_value;
	/// @brief May be null. If set, we reset the ACCESS bits of these pages before vmentry
	/// which improves single stepping accuracy
	uint64_t *gpas_target_pages;
	uint64_t gpas_target_pages_len;
	bool do_tlb_flush_before_each_step;
} sev_step_param_t;

#endif