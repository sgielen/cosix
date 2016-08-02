.global getpid
.type getpid, @function
getpid:
	/* syscall num */
	mov $1, %eax
	int $0x80
	ret

.global putstring
.type putstring, @function
putstring:
	/* syscall num */
	mov $2, %eax
	/* string to write in ecx */
	mov 4(%esp), %ecx
	/* size of string in edx */
	mov 8(%esp), %edx
	/* save old value of ebx */
	push %ebx
	/* fd number */
	mov $0, %ebx
	int $0x80
	/* pop old value of ebx */
	pop %ebx
	ret
