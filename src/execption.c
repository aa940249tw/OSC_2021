#include "uart.h"

char *exception_type[] = {
    "synchronous_sp_el0",
    "irq_sp_el0        ",
    "fiq_sp_el0        ",
    "serror_sp_el0     ",
    "synchronous_sp_elx",
    "irq_sp_elx        ",
    "fiq_sp_elx        ",
    "serror_sp_elx     ",
    "synchronous_el64  ",
    "irq_el64          ",
    "fiq_el64          ",
    "serror_el64       ",
    "synchronous_el32  ",
    "irq_el32          ",
    "fiq_el32          ",
    "serror_el32       "
};

void exception_entry(int type, unsigned long esr, unsigned long elr)
{
    printf("Exception eccurs!!!\n");
    printf("Excpetion type:  %s\tESR:  0x%x\tELR:  0x%x\n", exception_type[type], esr, elr);
}