
section .bss
;64 bit counter thread variable
[global counter_thread_var]
[global counter_thread_abort]
align 64
counter_thread_var: resq 1
counter_thread_abort: resq 1


section .text
%include "arch/x86/kvm/sev-step/libcpu_nasm_makros.S"


;%define GET_TIME m_cnt_thread
;%define GET_TIME m_rdtsc
%define GET_TIME m_rdpru


; Warm up loop
; Parameters:
; - RDI: (uint64_t) Number of iterations
[global cpu_warm_up]
cpu_warm_up:
	dec rdi
	jnz cpu_warm_up
	ret

; Idle for a few nops 
; Parameters:
[global cpu_idle]
cpu_idle:
	%rep 1000
		nop
	%endrep
	ret

; Flush cacheline
; Parameters:
; - RDI: (void *) Address to be flushed
[global cpu_clflush]
cpu_clflush:
	clflush [rdi]
	ret

; Read timestamp counter
; Return:
; - (uint64_t) Timestamp counter value
[global cpu_rdtsc]
cpu_rdtsc:
	m_rdtsc
	ret

; Read timestamp counter
; Return:
; - (uint64_t) Timestamp counter value
[global cpu_rdtscp]
cpu_rdtscp:
	m_rdtscp
	ret

; Read counting thread variable
; Return:
; - (uint64_t)
[global cpu_cnt_thread_value]
cpu_cnt_thread_value:
	m_cnt_thread
	ret

; Read counter thread variable
; Make sure to start the counter thread beforehand :)
; Return:
; - (uint64_t) value of counter_thread_var
[global cpu_get_cnt_thread_var]
	mov rax, qword [rel counter_thread_var]
	ret

; Memory access
; Parameters:
; - RDI: (void *) Address to be accessed
[global cpu_maccess]
cpu_maccess:
	mov rdi, [rdi]
	ret

; Time memory access
; Parameters:
; - RDI: (void *) Address to access
; Return:
; - (uint64_t) Access time in clock cycles
[global cpu_maccess_time]
cpu_maccess_time:
	GET_TIME rsi
	mov rdi, [rdi]
	GET_TIME
	sub rax, rsi
	ret

; Prime cache set by Pointer Chasing
; Parameters:
; - RDI: (void *) Start of the linked list
; - RSI: (size_t) Length of the linked list
[global cpu_prime_pointer_chasing_n]
cpu_prime_pointer_chasing_n:
	mov rdi, [rdi]
	dec rsi
	jne cpu_prime_pointer_chasing_n
	ret

; Prime cache set by Pointer Chasing
; Parameters:
; - RDI: (void *) Start of the linked list
[global cpu_prime_pointer_chasing]
cpu_prime_pointer_chasing:
	mov rdi, [rdi]
	test rdi, rdi
	jne cpu_prime_pointer_chasing
	ret

; Prime cache set by array accesses
; Parameters:
; - RDI: (void **) Start of array
; - RSI: (size_t) Lenght of array
[global cpu_prime_array]
cpu_prime_array:
	xor rdx, rdx
cpu_prime_array_loop:
	mov rax, [rdi+8*rdx] ; get address from array
	mov rax, [rax]       ; access address
	inc rdx
	cmp rsi, rdx
	jne cpu_prime_array_loop
	ret

; Probe cache set by Pointer Chasing
; Parameters:
; - RDI: (void *) Start of the linked list
; - RSI: (size_t) Length of the linked list
; Return:
; - (uint64_t) Probe time in clock cycles
[global cpu_probe_pointer_chasing_n]
cpu_probe_pointer_chasing_n:
	; get timestamp
	GET_TIME rcx
	; probe
cpu_probe_pointer_chasing_n_loop:
	mov rdi, [rdi]
	dec rsi
	jne cpu_probe_pointer_chasing_n_loop
	; get timestamp
	GET_TIME
	sub rax, rcx
	ret

; Probe cache set by Pointer Chasing
; Store result in 
; Parameters:
; - RDI: (void *) Start of the linked list
; Return:
; - (uint64_t) Total probe time in clock cycles
[global cpu_probe_pointer_chasing]
cpu_probe_pointer_chasing:
	; get timestamp
	GET_TIME rcx
	; probe
	mov rsi, rdi
cpu_probe_pointer_chasing_loop:
	mov rdi, rsi
	mov rsi, [rdi]
	test rsi, rsi
	jne cpu_probe_pointer_chasing_loop
	; get timestamp; 
	GET_TIME
	sub rax, rcx
	;mov qword [rdi+8], rax ; store access time in last used non null memory address shifted by 8 byte
	ret

; Probe cache set by Pointer Chasing
; Store result inplace, destroying the chase structure
; Parameters:
; - RDI: (void *) Start of the linked list
; Return: nothing. results are stored in the entries of the chase
[global cpu_probe_pointer_chasing_inplace]
cpu_probe_pointer_chasing_inplace:	
	; probe
	mov rsi, rdi
	
cpu_probe_pointer_chasing_inplace_loop:
	mov rdi, rsi
	;perf reading
	mfence ; serialize
	mov ecx, 0xc0010203 ;CTR_MSR_1 it assumed that this has been programmed beforehand
	rdmsr ; stores high in edx, low in eax; the high order bits of these registers are cleared
	shl rdx, 32 
	or rax, rdx ; due to shl we now have high in the low bits in rax
	mov r8, rax ; store result in r8
	; start time measurment
	GET_TIME r10
	;access memory location
	mov rsi, [rdi]
	;stop time measurment 
	GET_TIME
	;store diff in rax
	sub rax, r10
	mov r9, rax ; buffer in r9 to allow rdmsr (which clobbers rax and rdx) before mem access
	;perf reading, see first measurment for detailed comments
	lfence
	mov ecx, 0xc0010203 ;CTR_MSR_1 it assumed that this has been programmed beforehand
	rdmsr
	shl rdx, 32
	or rax, rdx
	sub rax, r8
	mfence; serialize to make sure memory access only happens after rdmsr stuff is complete
	;store r9 in memory, breaking the link, but next addr is already in rsi
	mov qword [rdi], r9 ; 
	mov qword [rdi+8], rax; store perfs in memory next to access time; we can use the 63 bytes following rdi wihtout introducing cache disturbance
	test rsi, rsi
	jne cpu_probe_pointer_chasing_inplace_loop	
	ret



; Probe cache set by Pointer Chasing
; Store result in external array passed in 
; Parameters:
; - RDI: (void *) Start of the linked list
; - RSI (uint64_t*) Caller allocated array twice the size of the linked list
; entry of the linked list.
; Return: nothing. results are stored in the entries of the chase
[global cpu_probe_pointer_chasing_remote]
cpu_probe_pointer_chasing_remote:
	; probe
	mov r11, rsi ; store array in r11
	mov rsi, rdi
	mov ecx, 0xc0010203 ;CTR_MSR_1 it assumed that this has been programmed beforehand
cpu_probe_pointer_chasing_remote_loop:
	mov rdi, rsi
	;perf reading
	mfence ; serialize
	rdmsr ; stores high in edx, low in eax; the high order bits of these registers are cleared
	shl rdx, 32 
	or rax, rdx ; due to shl we now have high in the low bits in rax
	mov r8, rax ; store result in r8
	; start time measurment
	GET_TIME r10
	;access memory location
	mov rsi, [rdi]
	;stop time measurment 
	GET_TIME
	;store diff in rax
	sub rax, r10
	mov r9, rax ; buffer in r9 to allow rdmsr (which clobbers rax and rdx) before mem access
	;perf reading, see first measurment for detailed comments
	lfence
	rdmsr
	shl rdx, 32
	or rax, rdx
	sub rax, r8
	mfence; serialize to make sure memory access only happens after rdmsr stuff is complete
	;store r9 in memory, breaking the link, but next addr is already in rsi
	mov qword [r11], r9 ; 
	mov qword [r11+8], rax; store perfs in memory next to access time; we can use the 63 bytes following rdi wihtout introducing cache disturbance
	add r11, 16 ; increment r11 to point entries for next iteration
	test rsi, rsi
	jne cpu_probe_pointer_chasing_remote_loop	
	ret

; Probe cache set by array accesses
; Parameters:
; - RDI: (void **) Start of array
; - RSI: (size_t) Length of array
; Return:
; - (uint64_t) Probe time in clock cycles
[global cpu_probe_array]
cpu_probe_array:
	; get timestamp
	GET_TIME rcx
	; probe
	xor rdx, rdx
cpu_probe_array_loop:
	mov rax, [rdi+8*rdx]
	mov rax, [rax]
	inc rdx
	cmp rsi, rdx
	jne cpu_probe_array_loop
	; get timestamp
	GET_TIME
	sub rax, rcx
	ret



;Optimized alignment + unrolling + abort
[global cpu_counter_loop]                                                                                                                                                                                                               
cpu_counter_loop:                                                                                                                                                                                                                       
   xor rax, rax ; clear shadow counting register                                                                                                                                                                                        
   align 64                                                                                                                                                                                                                             
   cpu_counter_loop_jb:                                                                                                                                                                                                                 
   ;inc mov take up 10 byte and jmp takes 2 bytes                                                                                                                                                                                       
   %rep 10                                                                                                                                                                                                                              
   inc rax                                                                                                                                                                                                                              
   mov qword [rel counter_thread_var], rax ;update global variable                                                                                                                                                                      
   %endrep                                                                                                                                                                                                                              
   mov rdi, qword [rel counter_thread_abort];update abort signal variable                                                                                                                                                               
   test rdi,rdi ;compute rdi&rdi and update flags                                                                                                                                                                                       
   jz cpu_counter_loop_jb ; if rdi was zero, ZF is set and we jump, else we do not jump                                                                                                                                                 
   ret                                                                                                                                                                                                                                  



 ; Parameters:
 ; -RDI :  Address to access
 ; Volatiles
 [global m_access_ret]
 m_access_ret:
   ;setup measurement
   mfence
   ;xor rdx, rdx
   ;xor rax, rax
   ;mov rdx, 0xe8
   ;wrmsr
  
   ;m_rdmsr rsi ; clobbers rax, rcx, rdx
   GET_TIME rsi
   ; instrutcions we want to measure
   mov rax, [rdi] ; memory access we want to measure
   ; second measurement
   ;lfence
   ;m_rdmsr ; clobbers rax, rcx, rdx. result is in rax
   GET_TIME
   sub rax, rsi ; diff in cylces in rax
   ret


; Probe each entry indivdual and store results in extra array
; Parameters:
; - RDI: (void **) Start of array
; - RSI: (size_t) Length of array
; - RDX: (uint64_t*) result array with twice the length of other array
; Return:
; - (uint64_t) Probe time in clock cycles
[global cpu_probe_array_individual]
cpu_probe_array_individual:
	push r14
	push r15

	
	mov r11, rdx ; store result array in r11 to free it up for rdmsr

	xor r14,r14 ; index in ptr array
cpu_probe_array_individual_loop:
	mov r15, [rdi+8*r14] ; load ptr for memory access


	;rdmsr + GET_TIME block clobbers rcx,rdx,rax and stores results in r8 and r10
	mfence ; serialize
	mov ecx, 0xc0010203 ;CTR_MSR_1 it assumed that this has been programmed beforehand
	rdmsr ; stores high in rdx, low in rax; the high order bits of these registers are cleared
	shl rdx, 32 
	or rax, rdx ; due to shl we now have high in the low bits in rax
	mov r8, rax ; store result in r8
	; start time measurment
	GET_TIME r10


	mov r15, [r15] ; memory access

	;stop time measurment 
	GET_TIME
	;store diff in rax
	sub rax, r10
	mov r9, rax ; buffer in r9 to allow rdmsr (which clobbers rax and rdx) before mem access
	;perf reading, see first measurment for detailed comments
	lfence
	mov ecx, 0xc0010203 ;CTR_MSR_1 it assumed that this has been programmed beforehand
	rdmsr
	shl rdx, 32
	or rax, rdx
	sub rax, r8
	mfence; serialize to make sure memory access only happens after rdmsr stuff is complete

	;sotre r9 and rax in result array. Need scratch register to compute the offset
	mov r8, r14
	shl r8, 4 ; *16
	mov qword [r11+r8], r9 ; time diff 
	mov qword [r11+r8+8], rax;perf value diff 

	inc r14 ; increment array index
	cmp rsi, r14 ; abort if we are at the end of the element
	jne cpu_probe_array_individual_loop
	; get timestamp
	GET_TIME
	sub rax, rcx

	pop r15
	pop r14
	ret