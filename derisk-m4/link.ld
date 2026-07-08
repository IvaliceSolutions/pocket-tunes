OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 256M }
SECTIONS {
  . = 0x80000000;
  .text : { *(.text.init) *(.text*) } > RAM
  .rodata : { *(.rodata*) *(.srodata*) } > RAM
  .data : { *(.data*) *(.sdata*) } > RAM
  .bss : { __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) ; __bss_end = .; } > RAM
  . = ALIGN(16);
  _heap_start = .;
  . += 8M;
  _heap_end = .;
  . = ALIGN(16);
  . += 1M;
  _stack_top = .;
}
