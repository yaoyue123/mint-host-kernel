#include <linux/smp.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <asm/svm.h> //struct vmcb_save_area
#include <linux/psp-sev.h>

#include "../mmu.h"
#include "mmu/mmu_internal.h"

#include <linux/sev-step/sev-step.h>
#include <linux/sev-step/libcache.h>
#include "svm/svm.h"

DEFINE_MUTEX(sev_step_config_mutex);
EXPORT_SYMBOL(sev_step_config_mutex);

sev_step_config_t global_sev_step_config = {
	.tmict_value = 0,
	.single_stepping_status = SEV_STEP_STEPPING_STATUS_DISABLED,
    .counted_instructions = 0,
    .decrypt_vmsa = false,
    .main_vm = NULL,
    .waitingForTimer = false,
    .got_idt_on_cpu = -1,
	.perf_init = false,
	.entry_counter = 0,
    .tsc_latency = 0,
	.idt = {0},
    .old_apic_lvtt = 0,
    .old_apic_tdcr = 0,
    .old_apic_tmict = 0,
	.old_idt_gate = {0},

	.cache_attack_config = NULL,
};
EXPORT_SYMBOL(global_sev_step_config);

bool sev_step_is_single_stepping_active(sev_step_config_t* cfg) {
	return (cfg->single_stepping_status == SEV_STEP_STEPPING_STATUS_ENABLED) ||
		(cfg->single_stepping_status == SEV_STEP_STEPPING_STATUS_ENABLED_WANT_DISABLE);
}
EXPORT_SYMBOL(sev_step_is_single_stepping_active);

//used to store performance counter values; 6 counters, 2 readings per counter
uint64_t perf_reads[6][2];
perf_ctl_config_t perf_configs[6];
int perf_cpu;

uint64_t perf_ctl_to_u64(perf_ctl_config_t * config) {

	uint64_t result = 0;
	result |= (  config->EventSelect & 0xffULL); //[7:0] in result and  [7:0] in EventSelect
	result |= ( (config->UintMask & 0xffULL) << 8 ); //[15:8]
	result |= ( (config->OsUserMode & 0x3ULL) << 16); //[17:16]
	result |= ( (config->Edge & 0x1ULL ) << 18 ); // 18
	result |= ( (config->Int & 0x1ULL ) << 20 ); // 20
	result |= ( (config->En & 0x1ULL ) << 22 ); //22
	result |= ( (config->Inv & 0x1ULL ) << 23); //23
	result |= ( (config->CntMask & 0xffULL) << 24); //[31:24]
	result |= ( ( (config->EventSelect & 0xf00ULL) >> 8 ) << 32); //[35:32] in result and [11:8] in EventSelect
	result |= ( (config->HostGuestOnly & 0x3ULL) << 40); // [41:40]

	return result;

}

/* Need to be done this way, if moved to sev-step.h there are many building errors */
//uint64_t sev_step_get_rip(struct vcpu_svm* svm); prototyp is in svm.c

/**
 * @brief Tell sev to decrypt the data for debug purposes
 * 
 * @param kvm kvm struct to get the info from
 * @param src source address for the data
 * @param dst destination address for the data
 * @param size size of the data
 * @param error error code
 * @return int result from the execution of the command
 */
static int __my_sev_issue_dbg_cmd(struct kvm *kvm, unsigned long src,
			       unsigned long dst, int size,
			       int *error)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_dbg *data;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL_ACCOUNT);
	if (!data)
		return -ENOMEM;

	data->handle = sev->handle;
	data->dst_addr = dst;
	data->src_addr = src;
	data->len = size;

	/*ret = sev_issue_cmd(kvm,
			     SEV_CMD_DBG_DECRYPT,
			    data, error);*/
	ret = sev_do_cmd(SEV_CMD_DBG_DECRYPT, data, error);
	kfree(data);
	return ret;
}

/**
 * @brief Decrypt the vmcb_save_area
 * 
 * @param svm svm for getting the vmsa and vmcb
 * @param save_area the save area
 * @return int 0 on success
 */
int decrypt_vmsa(struct vcpu_svm* svm, struct vmcb_save_area* save_area) {

	uint64_t src_paddr, dst_paddr;
	void * dst_vaddr;
	void * src_vaddr;
	struct page * dst_page;
	int call_res,api_res;
	uint64_t payload_bytes;
	uint64_t decrypt_bytes;
	call_res = 1337;
	api_res = 1337;
	
	src_vaddr = svm->sev_es.vmsa;
	src_paddr = svm->vmcb->control.vmsa_pa;

	if( src_paddr % 16 != 0) {
		printk("decrypt_vmsa: src_paddr 0x%llx is not 16b aligned\n",(uint64_t)src_paddr);
		return -1;
	}

	payload_bytes = sizeof( struct vmcb_save_area);
	decrypt_bytes = payload_bytes;
	if( decrypt_bytes % 16 != 0 ) {
		printk("decrypt_vmsa: warning size of decrypt_bytes (0x%llx)is not 16 b aligned. Rounding up\n", decrypt_bytes);
		decrypt_bytes += (16 - (decrypt_bytes % 16));
		printk("new value is 0x%llx\n",decrypt_bytes);
	}
	if(decrypt_bytes > 4096 ) {
		printk("decrypt_vmsa: decrypt_bytes (0x%llx) large than page size, this is currently not supported\n",decrypt_bytes);
		return -1;
	}

	dst_page = alloc_page(GFP_KERNEL);
	dst_vaddr =  vmap(&dst_page, 1, 0, PAGE_KERNEL);
	dst_paddr = page_to_pfn(dst_page) << PAGE_SHIFT;
	memset(dst_vaddr,0,PAGE_SIZE);

	

	if( dst_paddr % 16 != 0 ) {
		printk("decrypt_vmsa: dst_paddr 0x%llx ist not 16 byte aligned\n", (uint64_t)dst_paddr);
		return -1;
	}

	//printk("src_paddr = 0x%llx dst_paddr = 0x%llx\n", __sme_clr(src_paddr), __sme_clr(dst_paddr));
	//printk("Sizeof vmcb_save_area is: 0x%lx\n", sizeof( struct vmcb_save_area) );


	call_res = __my_sev_issue_dbg_cmd(svm->vcpu.kvm, __sme_set(src_paddr), __sme_set(dst_paddr), decrypt_bytes, &api_res);


	printk("decrypt_vmsa: result of call was %d, result of api command was %d\n",call_res, api_res);

	//todo error handling
	if( api_res != 0 ) {
		__free_page(dst_page);
		return -1;
	}

	memcpy(save_area, dst_vaddr, payload_bytes );


	__free_page(dst_page);

	return 0;


}

/**
 * @brief Decrypt the rip of sev
 * 
 * @param svm svm for getting the info
 * @return uint64_t the decrypted rip
 */
uint64_t sev_step_get_rip(struct vcpu_svm* svm) {
	struct vmcb_save_area* save_area;
	struct kvm * kvm;
	struct kvm_sev_info *sev;
	uint64_t rip;

	printk("sev_step_get_rip: got called\n");
	kvm = svm->vcpu.kvm;
	sev = &to_kvm_svm(kvm)->sev_info;

	printk("sev-active: %d, sev->es_active :%d, sev->snp_active: %d\n",sev->active,
		sev->es_active,sev->snp_active);
	//for sev-es and sev-snp we need to use the debug api, to decrypt the vmsa
	if( sev->active && (sev->es_active || sev->snp_active)) {
		int res;
		save_area = vmalloc(sizeof(struct vmcb_save_area) );
		memset(save_area,0, sizeof(struct vmcb_save_area));

		res = decrypt_vmsa(svm, save_area);
		if( res != 0) {
			printk("sev_step_get_rip failed to decrypt\n");
			return 0;
		}

		rip =  save_area->rip;

		vfree(save_area);
	} else { //otherwise we can just access as plaintexts
		rip = svm->vmcb->save.rip;
	}
	return rip;

}
EXPORT_SYMBOL(sev_step_get_rip);

void write_ctl(perf_ctl_config_t * config, int cpu, uint64_t ctl_msr){
	wrmsrl( ctl_msr, perf_ctl_to_u64(config)); //always returns zero
}

void read_ctr(uint64_t ctr_msr, int cpu, uint64_t* result) {
    uint64_t tmp;
	rdmsrl( ctr_msr, tmp); //always returns zero
	*result = tmp & ( (0x1ULL << 48) - 1);
}

void setup_perfs(sev_step_config_t* config) {
    int i;
    
    //perf_cpu = smp_processor_id();
    
    for( i = 0; i < 6; i++) {
        perf_configs[i].HostGuestOnly = 0x1; //0x1 means: count only guest
        perf_configs[i].CntMask = 0x0;
        perf_configs[i].Inv = 0x0;
        perf_configs[i].En = 0x0;
        perf_configs[i].Int = 0x0;
        perf_configs[i].Edge = 0x0;
        perf_configs[i].OsUserMode = 0x3; //0x3 means: count userland and kernel events
    }
    
    //remember to set .En to enable the individual counter
    perf_configs[0].EventSelect = 0x0c0;
	perf_configs[0].UintMask = 0x0;
    perf_configs[0].En = 0x1;
	write_ctl(&perf_configs[0],perf_cpu, CTL_MSR_0);

	if( config->cache_attack_config != NULL ) {
		perf_configs[1] = config->cache_attack_config->cache_attack_perf;
		if( perf_configs[1].En == 0 ) {
			printk("%s:%d : %s Warning Cache Attack Perf not enabled!\n",
				__FILE__,
				__LINE__,
				__FUNCTION__
			);
		}
	}
	write_ctl(&perf_configs[1],perf_cpu,CTL_MSR_1);
    
}
EXPORT_SYMBOL(setup_perfs);

void calculate_steps(sev_step_config_t *config) {
   if(!config->perf_init) {
        read_ctr(CTR_MSR_0, perf_cpu, &perf_reads[0][0]);        
        config->perf_init = true;
    } else {
        read_ctr(CTR_MSR_0, perf_cpu, &perf_reads[0][1] );
		//TODO: fix case where readings are identical. underflow is very confusing!
        config->counted_instructions = perf_reads[0][1] - perf_reads[0][0] -1;
        config->perf_init = false;
    }
}
EXPORT_SYMBOL(calculate_steps);

bool __untrack_single_page(struct kvm_vcpu *vcpu, gfn_t gfn,
                           enum kvm_page_track_mode mode) {
  int idx;
  bool ret;
  struct kvm_memory_slot *slot;

  ret = false;
  idx = srcu_read_lock(&vcpu->kvm->srcu);
  slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);

  if (slot != NULL && kvm_page_track_is_active(vcpu, gfn, mode)) {

    write_lock(&vcpu->kvm->mmu_lock);
    kvm_slot_page_track_remove_page(vcpu->kvm, slot, gfn, mode);
    write_unlock(&vcpu->kvm->mmu_lock);
    ret = true;

  } else {

    printk("Failed to untrack %016llx because ", gfn);
    if (slot == NULL) {
      printk(KERN_CONT "slot was  null");
    } else if (!kvm_page_track_is_active(vcpu, gfn, mode)) {
      printk(KERN_CONT "page track was not active");
    }
    printk(KERN_CONT "\n");
  }
  srcu_read_unlock(&vcpu->kvm->srcu, idx);
  return ret;
}
EXPORT_SYMBOL(__untrack_single_page);

bool __track_single_page(struct kvm_vcpu *vcpu, gfn_t gfn,
                         enum kvm_page_track_mode mode) {
  int idx;
  bool ret;
  struct kvm_memory_slot *slot;

  ret = false;
  idx = srcu_read_lock(&vcpu->kvm->srcu);
  slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
  if (slot != NULL && !kvm_page_track_is_active(vcpu, gfn, mode)) {

    write_lock(&vcpu->kvm->mmu_lock);
    kvm_slot_page_track_add_page(vcpu->kvm, slot, gfn, mode);
    write_unlock(&vcpu->kvm->mmu_lock);
    ret = true;

  } else {

    printk("Failed to track %016llx because ", gfn);
    if (slot == NULL) {
      printk(KERN_CONT "slot was  null");
    }
    if (kvm_page_track_is_active(vcpu, gfn, mode)) {
      printk(KERN_CONT "page is already tracked");
    }
    printk(KERN_CONT "\n");
  }
  srcu_read_unlock(&vcpu->kvm->srcu, idx);
  return ret;
}
EXPORT_SYMBOL(__track_single_page);

//track all pages; taken from severed repo
long kvm_start_tracking(struct kvm_vcpu *vcpu,enum kvm_page_track_mode mode ) {
        long count = 0;
        u64 iterator, iterat_max;
        struct kvm_memory_slot *slot;
        int idx;

		//TODO: merge: luca: double previously we did select by index, ignoring the id
		//double check that this is equal
		slot = id_to_memslot(vcpu->kvm->memslots[0],0);
    	iterat_max = slot->base_gfn + slot->npages;
		printk("%s:%d [%s] base_gfn = %llu, npages=%lu",__FILE__, __LINE__, __FUNCTION__, slot->base_gfn, slot->npages);
		idx = srcu_read_lock(&vcpu->kvm->srcu);
		write_lock(&vcpu->kvm->mmu_lock);
        for (iterator=0; iterator < iterat_max; iterator++)
        {
			slot = kvm_vcpu_gfn_to_memslot(vcpu, iterator);
			if ( slot != NULL ) {
					if( !kvm_page_track_is_active(vcpu, iterator, mode)) {
						kvm_slot_page_track_add_page_no_flush(vcpu->kvm, slot, iterator, mode);
					}
					count++;
			}
        }
		
		if( count > 0 ) {
			kvm_flush_remote_tlbs(vcpu->kvm);
		}
		write_unlock(&vcpu->kvm->mmu_lock);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);

        return count;
}
EXPORT_SYMBOL(kvm_start_tracking);

//untrack all pages; taken from severed repo
long kvm_stop_tracking(struct kvm_vcpu *vcpu,enum kvm_page_track_mode mode ) {
		long count = 0;
		u64 iterator, iterat_max;
		struct kvm_memory_slot *slot;
		int idx;

		//TODO: merge: luca: double previously we did select by index, ignoring the id
		//double check that this is equal
    	slot = id_to_memslot(vcpu->kvm->memslots[0],0);
    	iterat_max = slot->base_gfn + slot->npages;
		printk("%s:%d [%s] base_gfn = %llu, npages=%lu",__FILE__, __LINE__, __FUNCTION__, slot->base_gfn, slot->npages);
		idx = srcu_read_lock(&vcpu->kvm->srcu);
		write_lock(&vcpu->kvm->mmu_lock);
        for (iterator=0; iterator < iterat_max; iterator++)
        {
			slot = kvm_vcpu_gfn_to_memslot(vcpu, iterator);
			if ( slot != NULL &&
				kvm_page_track_is_active(vcpu, iterator,  mode)) {
				kvm_slot_page_track_remove_page(vcpu->kvm, 
								slot, 
								iterator, 
								mode);
				
				count++;
            	}
			if( need_resched() || rwlock_needbreak(&vcpu->kvm->mmu_lock))  {
				cond_resched_rwlock_write(&vcpu->kvm->mmu_lock);
			}
        }
		write_unlock(&vcpu->kvm->mmu_lock);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);
        return count;
}
EXPORT_SYMBOL(kvm_stop_tracking);

bool __clear_nx_on_page(struct kvm_vcpu *vcpu, gfn_t gfn) {
	int idx;
	bool ret;
	struct kvm_memory_slot *slot;

	ret = false;
	idx = srcu_read_lock(&vcpu->kvm->srcu);
	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	if( slot != NULL ) {
		write_lock(&vcpu->kvm->mmu_lock);
		kvm_mmu_slot_gfn_protect(vcpu->kvm,slot,gfn,PG_LEVEL_4K,KVM_PAGE_TRACK_RESET_EXEC);
		write_unlock(&vcpu->kvm->mmu_lock);
		ret = true;
	}
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	return ret;
}
EXPORT_SYMBOL(__clear_nx_on_page);

bool sev_step_reset_access_bit(struct kvm_vcpu *vcpu, gfn_t gfn) {
	int idx;
	bool ret;
	struct kvm_memory_slot *slot;

	ret = false;
	idx = srcu_read_lock(&vcpu->kvm->srcu);
	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	if( slot != NULL ) {
		write_lock(&vcpu->kvm->mmu_lock);
		kvm_mmu_slot_gfn_protect(vcpu->kvm,slot,gfn,PG_LEVEL_4K,KVM_PAGE_TRACK_RESET_ACCESSED);
		write_unlock(&vcpu->kvm->mmu_lock);
		ret = true;
	}
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	return ret;
}
EXPORT_SYMBOL(sev_step_reset_access_bit);




void free_sev_step_cache_attack_config_t(sev_step_cache_attack_config_t* config) {
	kfree(config->lookup_tables);
	free_eviction_sets(config->eviction_sets,config->lookup_tables_len);
	kfree(config);
}
EXPORT_SYMBOL(free_sev_step_cache_attack_config_t);


int sev_step_get_vmcb_save_area(struct kvm_vcpu *vcpu, struct vmcb_save_area* vmcb_result,
	struct sev_es_save_area *vmsa_result) {
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_save_area *save = &svm->vmcb->save;
	struct vmcb_save_area *save01 = &svm->vmcb01.ptr->save;



	if (vcpu->arch.guest_state_protected && sev_snp_guest(vcpu->kvm) ) {
		struct sev_es_save_area *vmsa;
		struct kvm_sev_info *sev = &to_kvm_svm(vcpu->kvm)->sev_info;
		struct page *save_page;
		int ret, error;

		save_page = alloc_page(GFP_KERNEL);
		if (!save_page)
			return 1;

		save = page_address(save_page);
		save01 = save;


		ret = snp_guest_dbg_decrypt_page(__pa(sev->snp_context) >> PAGE_SHIFT,
						 svm->vmcb->control.vmsa_pa >> PAGE_SHIFT,
						 __pa(save) >> PAGE_SHIFT,
						 &error);
		if (ret) {
			pr_err("%s: sev_snp_guest : failed to decrypt vmsa %d\n", __func__, error);
			return 1;
		}

		memcpy(vmcb_result,save,sizeof(struct vmcb_save_area));

		vmsa = (struct sev_es_save_area *)save;
		memcpy(vmsa_result,vmsa,sizeof(struct sev_es_save_area));

		__free_page(virt_to_page(save));
	} else if( vcpu->arch.guest_state_protected && sev_es_guest(vcpu->kvm)) {
		struct sev_es_save_area *vmsa;
		int error;


		error = decrypt_vmsa(svm, vmcb_result);
		if( error != 0) {
			pr_err("%s: sev_es_guest : failed to decrypt vmsa %d\n", __func__, error);
			return 1;
		}
		vmsa = (struct sev_es_save_area *)vmcb_result;
		memcpy(vmsa_result,vmsa,sizeof(struct sev_es_save_area));

	} else if (vcpu->arch.guest_state_protected) {
		printk("sev_step_get_vmcb_save_area: guest state protected but don't know how to decrypt\n");
		return 1;
	} else {
		memcpy(vmcb_result,save,sizeof(struct vmcb_save_area));
	}

	return 0;
}
EXPORT_SYMBOL(sev_step_get_vmcb_save_area);