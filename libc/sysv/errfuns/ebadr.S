#include "libc/macros.h"
.text.unlikely

	.ftrace1
ebadr:
	.ftrace2
#ifdef __x86_64__
	.leafprologue
	mov	EBADR(%rip),%ecx
	jmp	__errfun
#elif defined(__aarch64__)
	adrp	x1,EBADR
	ldrsw	x0,[x1,#:lo12:EBADR]
	b	__errfun
#else
#error "unsupported architecture"
#endif
	.endfn	ebadr,globl,hidden
