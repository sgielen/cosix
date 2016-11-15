.global switch_thread
switch_thread:
	/* void switch_thread(void **old_sp, void *sp) */
	mov 4(%esp), %eax
	mov 8(%esp), %ecx
	/* store callee saved registers */
	push %ebx
	push %edi
	push %esi
	push %ebp
	/* store my stack ptr in *eax */
	mov %esp, (%eax)
	/* load stack ptr from ecx */
	mov %ecx, %esp
	/* load callee saved registers */
	pop %ebp
	pop %esi
	pop %edi
	pop %ebx
	/* return to thread */
	ret

.global initial_kernel_stack
initial_kernel_stack:
	.long 0
	.long 0
	.long 0
	.long 0
	.long do_iret
1:

.global initial_kernel_stack_size
initial_kernel_stack_size:
	.int 1b - initial_kernel_stack
