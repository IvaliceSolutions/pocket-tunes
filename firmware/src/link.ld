OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 64K }
SECTIONS {
  . = 0x00000000;
  .text : { *(.text.init) *(.text*) } > RAM
  .rodata : { *(.rodata*) *(.srodata*) } > RAM
  .data : { *(.data*) *(.sdata*) } > RAM
  .bss : { __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) ; __bss_end = .; } > RAM
  /* stack grows down from the top of RAM (also set by the core's STACKADDR) */
  _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
