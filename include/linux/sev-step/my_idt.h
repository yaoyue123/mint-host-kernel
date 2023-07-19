#ifndef MY_IDT_H
#define MY_IDT_H
#include <linux/sev-step/sev-step.h>
#include "svm/svm.h"

void my_idt_init_idt( sev_step_config_t *config );

/**
 * @brief Install the new handler on cpu
 * 
 * @param cpu cpu id
 */
void install_handler_on_cpu(int cpu);

/**
 * @brief Restore the old apic config
 * 
 * @param config global sev step config 
 */
void apic_restore(sev_step_config_t *config);

/**
 * @brief Backup the current apic config
 * 
 * @param config global sev step config
 */
void apic_backup(sev_step_config_t *config);

/**
 * @brief Start the apic timer for single stepping
 * 
 * @param config global sev step config
 * @param svm svm data struct
 */
void my_idt_prepare_apic_timer(sev_step_config_t *config, struct vcpu_svm *svm);

/**
 * @brief Install new idt handler
 * 
 * @param config global sev step config
 */
void my_idt_install_handler(sev_step_config_t *config);
#endif
