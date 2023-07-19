    .text
    .global isr_wrapper
    .type isr_wrapper,@function
isr_wrapper:
    //rdtsc
    //mov %eax, nemesis_tsc_aex(%rip)

    /* HACK: restore edx:eax from AEX synthetic register state */
    //mov $3, %rax
    //mov $0, %rdx

    pushq %rax
    movq %rsp, %rax
    add $8,%rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11

    movq %rax, %rdi
    /* TODO support 256 possible IRQ handlers via asm macro */
    //movb sgx_step_vector_hack(%rip), %sil
    call my_handler

    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq