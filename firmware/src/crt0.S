/* Pocket Tunes firmware entry — M7d: runs from external SRAM (0x50000000)
 * through the instruction cache. Sets the stack in BRAM, copies the hot Opus
 * code (.itcm) from SRAM to the instruction TCM (0x60000000), copies the fast
 * rodata+data image from SRAM to BRAM, zeros .bss, calls main. */
.section .text.init
.globl _start
_start:
    la      sp, _stack_top
    /* copy .itcm: SRAM load image -> instruction TCM (region 6) */
    la      t0, __itcm_lma
    la      t1, __itcm_start
    la      t2, __itcm_end
1:  bge     t1, t2, 2f
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    j       1b
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
