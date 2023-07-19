#include <asm/cacheflush.h>
#include <linux/types.h>
#include  <asm/pgtable_types.h>
#include <linux/vmalloc.h>
#include <asm/io.h> //virt_to_phys
#include <asm/cacheflush.h>

//#include <linux/kvm_host.h> //struct kvm
#include <asm/svm.h> //struct vmcb_save_area
#include <asm-generic/set_memory.h>

#include <linux/sev-step/my_idt.h>
#include <linux/sev-step/sev-step.h>

#include <asm/tlbflush.h>
#include <asm/special_insns.h>



idt_t idt;

struct vcpu_svm* main_vcpu_svm = NULL;
EXPORT_SYMBOL(main_vcpu_svm);

//set this in intr handler (as we cannot sleep and wait for sev api there)
//we check if this is true in intr_interception and call the print code from there
bool decrypt_rip = false;
EXPORT_SYMBOL(decrypt_rip);

bool waitingForTimer = false;
EXPORT_SYMBOL(waitingForTimer);

#define IRQ_NUMBER 45






extern void isr_wrapper(void);

/**
 * @brief Dumping a gate to dmesg log
 * 
 * @param gate the gate to dump
 * @param idx vector
 */
/*static void dump_gate(gate_desc_t *gate, int idx)
{
    printk("IDT[%3d] @%016llx = %016lx (seg sel 0x%x); p=%d; dpl=%d; type=%02d; ist=%d",
        idx, (unsigned long long) gate,  gate_offset(gate), gate->segment, gate->p, gate->dpl, gate->type, gate->ist);
}*/

/**
 * @brief Get the current idt. This is core specific and must be called with interrupts enabled
 * 
 * @param config The retrieved idt is stored here together with the core for which it is valid
 */
void my_idt_init_idt( sev_step_config_t *config ) {
	dtr_t idtr = {0};
	int entries;

	if ( config->got_idt_on_cpu == smp_processor_id() ) {
		return;
	}
	//printk("my_idt_init_idt running on core: %d\n", smp_processor_id());

	asm volatile ("sidt %0\n\t" :"=m"(idtr) :: );

	entries = (idtr.size+1)/sizeof(gate_desc_t);

	set_memory_rw(idtr.base,1);

	config->idt.base = (gate_desc_t*) idtr.base;
	config->idt.entries = entries;
	config->got_idt_on_cpu = smp_processor_id();
}
EXPORT_SYMBOL(my_idt_init_idt);

/**
 * @brief Install new irq handler
 * 
 * @param config global sev step config
 * @param asm_handler custom handler from asm
 * @param vector vector for handler
 */
void install_kernel_irq_handler(sev_step_config_t *config, void *asm_handler, int vector) {
	gate_desc_t *gate;

	BUG_ON(config->got_idt_on_cpu != smp_processor_id());

    gate = gate_ptr(config->idt.base, vector);
	//printk("old gate:\n");
	//dump_gate(gate,vector);
	//store old entry
	memcpy(&config->old_idt_gate, gate, sizeof(gate_desc_t));


    gate->offset_low    = PTR_LOW(asm_handler);
    gate->offset_middle = PTR_MIDDLE(asm_handler);
    gate->offset_high   = PTR_HIGH(asm_handler);
    
    gate->p = 1; //segment present flag
    gate->segment = KERNEL_CS; //segment selector
    gate->dpl = KERNEL_DPL; //descriptor privilege lvel
    gate->type = GATE_INTERRUPT; //type
    gate->ist = 0; // new stack for interrupt handling?
}

/**
 * @brief Restore the old irq handler
 * 
 * @param config global sev step config
 * @param vector vector for handler
 */
static void restore_kernel_irq_handler(sev_step_config_t *config, int vector) {
	gate_desc_t *gate;

	BUG_ON(config->got_idt_on_cpu != smp_processor_id());


    gate = gate_ptr(config->idt.base, vector);
	memcpy(gate,&config->old_idt_gate, sizeof(gate_desc_t));
}

//this function is called from asm
void my_handler(void) {
	//luca: this caused "bad: scheduling from the idle thread"
	//TODO: return here later and determine whether the get_cpu/put_cpu or the spinlock was at fault for this
	//get_cpu();
	apic_write(APIC_EOI, 0x0); //aknowledge interrupt
	//printk("my handler is running on: %d\n", smp_processor_id());
	//mutex_lock(&sev_step_config_mutex);
	global_sev_step_config.waitingForTimer = false;
	//mutex_unlock(&sev_step_config_mutex);
	//put_cpu();
}

void apic_restore(sev_step_config_t *config) {
	get_cpu();
	//printk("restoring old irq handler\n");
	restore_kernel_irq_handler(config, IRQ_NUMBER);
	apic_write(APIC_LVTT, config->old_apic_lvtt); //0x320
	apic_write(APIC_TDCR, config->old_apic_tdcr); //0x3e0
	//without this write, the timer does not seem to be actually startet
	apic_write(APIC_TMICT, config->old_apic_tmict);
	put_cpu();
	config->waitingForTimer = false;

}
EXPORT_SYMBOL(apic_restore);

void apic_backup(sev_step_config_t *config) {
 	get_cpu(); //disable preemption => cannot be moved to antoher cpu
	//printk("apic_backup is running on: %d\n", smp_processor_id());
	//start apic_timer_oneshot
	config->old_apic_lvtt = apic_read(APIC_LVTT);
    config->old_apic_tdcr = apic_read(APIC_TDCR);
	config->old_apic_tmict = apic_read(APIC_TMICT);
	put_cpu();
}
EXPORT_SYMBOL(apic_backup);

void my_idt_install_handler(sev_step_config_t *config) {
	install_kernel_irq_handler(config, &isr_wrapper,IRQ_NUMBER);
}
EXPORT_SYMBOL(my_idt_install_handler);

void my_idt_prepare_apic_timer(sev_step_config_t *config, struct vcpu_svm *svm) {
	//get_cpu();
	/*waitingFOrTimer == true means that the interrupt from the previous
	timmer programming has not yet been processed
	*/

	mutex_lock(&sev_step_config_mutex);
	if( !config->waitingForTimer && sev_step_is_single_stepping_active(config) ) {

		mutex_unlock(&sev_step_config_mutex);
		//it's assumed that the old timer config has been backed up
		 apic_write(APIC_LVTT, IRQ_NUMBER | APIC_LVT_TIMER_ONESHOT);
   		 apic_write(APIC_TDCR, APIC_TDR_DIV_2);
		//start apic_timer_irq
		config->waitingForTimer = true;
		//this is now in vmenter, to move it behind the cache priming code
		//apic_write(APIC_TMICT, timer_value); 
	} else {
		mutex_unlock(&sev_step_config_mutex);
	}
	
	//put_cpu();

}
EXPORT_SYMBOL(my_idt_prepare_apic_timer);
