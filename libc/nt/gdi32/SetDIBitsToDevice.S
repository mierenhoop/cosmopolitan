#include "libc/nt/codegen.h"
.imp	gdi32,__imp_SetDIBitsToDevice,SetDIBitsToDevice

	.text.windows
	.ftrace1
SetDIBitsToDevice:
	.ftrace2
#ifdef __x86_64__
	push	%rbp
	mov	%rsp,%rbp
	mov	__imp_SetDIBitsToDevice(%rip),%rax
	jmp	__sysv2nt12
#elif defined(__aarch64__)
	mov	x0,#0
	ret
#endif
	.endfn	SetDIBitsToDevice,globl
	.previous
