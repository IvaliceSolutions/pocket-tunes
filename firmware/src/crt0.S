/* Pocket Tunes firmware entry — M7a: runs from external SRAM (0x50000000)
 * through the instruction cache. Sets the stack in BRAM, copies the fast
 * rodata+data image from SRAM to BRAM, zeros .bss, calls main. */
.section .text.init
.globl _start
_start:
    la      sp, _stack_top
    /* copy .rodata + .data: SRAM load image -> BRAM run addresses */
    la      t0, __fast_lma
    la      t1, __fast_start
    la      t2, __fast_end
1:  bge     t1, t2, 2f
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    j       1b
2:  /* zero .bss */
    la      t0, __bss_start
    la      t1, __bss_end
3:  bge     t0, t1, 4f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       3b
4:  call    main
5:  j       5b
