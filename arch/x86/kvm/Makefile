# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I $(srctree)/arch/x86/kvm
ccflags-$(CONFIG_KVM_WERROR) += -Werror

ifeq ($(CONFIG_FRAME_POINTER),y)
OBJECT_FILES_NON_STANDARD_vmenter.o := y
endif

include $(srctree)/virt/kvm/Makefile.kvm

$(obj)/sev-step/raw_spinlock.o:
	nasm -f elf64 -o $(obj)/sev-step/raw_spinlock.o $(src)/sev-step/raw_spinlock.asm
	touch $(src)/sev-step/.raw_spinlock.o.cmd

$(obj)/sev-step/libcpu_nasm_makros.o:
	nasm -f elf64 -o $(obj)/sev-step/libcpu_nasm_makros.o $(src)/sev-step/libcpu_nasm_makros.S
	touch $(src)/sev-step/.libcpu_nasm_makros.o.cmd

$(obj)/sev-step/libcpu.o: $(obj)/sev-step/libcpu_nasm_makros.o
	nasm -f elf64 -o $(obj)/sev-step/libcpu.o $(src)/sev-step/libcpu.S
	touch $(src)/sev-step/.libcpu.o.cmd


kvm-y			+= x86.o emulate.o i8259.o irq.o lapic.o \
			   i8254.o ioapic.o irq_comm.o cpuid.o pmu.o mtrr.o \
			   hyperv.o debugfs.o mmu/mmu.o mmu/page_track.o \
			   mmu/spte.o sev-step/userspace_page_track_api.o \
			   sev-step/raw_spinlock.o sev-step/my_idt.o sev-step/sev-step.o \
			   sev-step/isr_wrapper.o sev-step/libcpu_nasm_makros.o \
			   sev-step/libcpu.o sev-step/libcache.o

ifdef CONFIG_HYPERV
kvm-y			+= kvm_onhyperv.o
endif

kvm-$(CONFIG_X86_64) += mmu/tdp_iter.o mmu/tdp_mmu.o
kvm-$(CONFIG_KVM_XEN)	+= xen.o

kvm-intel-y		+= vmx/vmx.o vmx/vmenter.o vmx/pmu_intel.o vmx/vmcs12.o \
			   vmx/evmcs.o vmx/nested.o vmx/posted_intr.o
kvm-intel-$(CONFIG_X86_SGX_KVM)	+= vmx/sgx.o

kvm-amd-y		+= svm/svm.o svm/vmenter.o svm/pmu.o svm/nested.o svm/avic.o svm/sev.o

ifdef CONFIG_HYPERV
kvm-amd-y		+= svm/svm_onhyperv.o
endif

obj-$(CONFIG_KVM)	+= kvm.o
obj-$(CONFIG_KVM_INTEL)	+= kvm-intel.o
obj-$(CONFIG_KVM_AMD)	+= kvm-amd.o
