/* Pocket Tunes firmware entry — M7e: runs from external SRAM (0x50000000)
 * through the instruction cache. Sets the stack in BRAM, copies the fast
 * rodata+data image from SRAM to BRAM, zeros .bss, calls main. The instruction
 * TCM (0x60000000) is NOT filled here — codec_start() copies the active
 * codec's overlay into it (see itcm_load_* in codec.c). */
.section .text.init
.globl _start
_start:
    la      sp, _stack_top
2:  /* copy .rodata + .data: SRAM load image -> BRAM run addresses */
    la      t0, __fast_lma
    la      t1, __fast_start
    la      t2, __fast_end
3:  bge     t1, t2, 4f
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    j       3b
4:  /* zero .bss */
    la      t0, __bss_start
    la      t1, __bss_end
5:  bge     t0, t1, 6f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       5b
6:  call    main
7:  j       7b
