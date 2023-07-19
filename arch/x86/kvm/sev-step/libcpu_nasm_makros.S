%macro m_rdtsc 0
  lfence
  rdtsc
  shl rdx, 32
  or rax, rdx
%endmacro

%macro m_rdtsc 1
  m_rdtsc
  mov %1, rax
%endmacro


%macro m_rdpru 0
  lfence
  mov ecx,0x1
  db 0x0f, 0x01, 0xfd
  shl rdx, 32
  or rax, rdx
  lfence
%endmacro

%macro m_rdpru 1
  m_rdpru
  mov %1, rax
%endmacro

%macro m_rdtscp 0
  rdtscp
  shl rdx, 32
  or rax, rdx
%endmacro

%macro m_rdtscp 1
  m_rdtscp
  mov %1, rax
%endmacro

%macro m_cnt_thread 0
  lfence
  mov rax, qword [rel counter_thread_var];
  lfence
%endmacro

%macro m_cnt_thread 1
  lfence
  mov %1, qword [rel counter_thread_var]
  lfence
%endmacro

