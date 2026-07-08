#include <stdint.h>
#define UART ((volatile uint8_t *)0x10000000)
static void putc_(char c){ UART[0]=(uint8_t)c; }
static void puts_(const char*s){ while(*s) putc_(*s++); }
int main(void){
  puts_("HELLO_UART_OK\n");
  uint32_t lo; __asm__ volatile("csrr %0, minstret":"=r"(lo));
  puts_("CSR_READ_OK\n");
  return 0;
}
