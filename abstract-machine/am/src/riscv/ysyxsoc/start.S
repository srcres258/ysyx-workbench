.section entry, "ax"
.globl _start
.type _start, @function

_start:
    mv s0, x0
    la sp, _stack_pointer
    call _trm_init

.size _start, . - _start
