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
	/* fd number */
	push %ebx
	mov $0, %ebx
	int $0x80
	/* pop old value of ebx */
	pop %ebx
	ret

.global getchar
.type getchar, @function
getchar:
	/* syscall num */
	mov $3, %eax
	/* offset of char in fd */
	mov 8(%esp), %ecx
	/* fd number */
	push %ebx
	mov 8(%esp), %ebx
	int $0x80
	/* pop old value of ebx */
	pop %ebx
	ret

.global openat
.type openat, @function
openat:
	/* syscall num */
	mov $4, %eax
	/* pathname */
	mov 8(%esp), %ecx
	/* directory */
	mov 4(%esp), %edx
	/* fd number */
	push %ebx
	mov 16(%esp), %ebx
	int $0x80
	/* pop old value of ebx */
	pop %ebx
	ret
