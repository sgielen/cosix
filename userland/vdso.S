#define ENTRY(name)      \
  .text;                 \
  .p2align 2, 0x90;      \
  .global name;          \
  .type name, @function; \
name:

#define END(name) .size name, . - name

ENTRY(cloudabi_sys_clock_res_get)
  mov $26, %eax
  int $0x80
  jc 1f
  mov 8(%esp), %ecx
  mov %eax, 0(%ecx)
  mov %edx, 4(%ecx)
  xor %eax, %eax
1:
  ret
END(cloudabi_sys_clock_res_get)

ENTRY(cloudabi_sys_clock_time_get)
  mov $27, %eax
  int $0x80
  jc 1f
  mov 16(%esp), %ecx
  mov %eax, 0(%ecx)
  mov %edx, 4(%ecx)
  xor %eax, %eax
1:
  ret
END(cloudabi_sys_clock_time_get)
