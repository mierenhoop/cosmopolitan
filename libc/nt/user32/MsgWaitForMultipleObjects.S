#include "libc/nt/codegen.h"
.imp	user32,__imp_MsgWaitForMultipleObjects,MsgWaitForMultipleObjects

	.text.windows
	.ftrace1
MsgWaitForMultipleObjects:
	.ftrace2
#ifdef __x86_64__
	push	%rbp
	mov	%rsp,%rbp
	mov	__imp_MsgWaitForMultipleObjects(%rip),%rax
	jmp	__sysv2nt6
#elif defined(__aarch64__)
	mov	x0,#0
	ret
#endif
	.endfn	MsgWaitForMultipleObjects,globl
	.previous
