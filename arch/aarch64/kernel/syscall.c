#include "common.h"
#include "init.h"
#include "task.h"
#include "syscall.h"
#include "signal.h"
#include "page.h"
#include "string.h"
#include "tools.h"

extern int aarch64_irq_number(void);
extern void aarch64_irq_eoi(void);
extern void aarch64_error_dispatch(struct trap_frame *frame);

static void frame_from_registers(struct trap_frame *frame)
{
    frame->ebx = frame->x[0];
    frame->ecx = frame->x[1];
    frame->edx = frame->x[2];
    frame->esi = frame->x[3];
    frame->edi = frame->x[4];
    frame->ebp = frame->x[5];
    frame->eax = frame->x[0];
}

static void registers_from_frame(struct trap_frame *frame)
{
    frame->x[0] = frame->eax;
    frame->x[1] = frame->ecx;
    frame->x[2] = frame->edx;
    frame->x[3] = frame->esi;
    frame->x[4] = frame->edi;
    frame->x[5] = frame->ebp;
}

reg_t arch_linux_syscall_number(reg_t nr)
{
    switch (nr) {
    case 17: return 183;
    case 23: return 41;
    case 24: return 330;
    case 25: return 55;
    case 29: return 54;
    case 34: return 296;
    case 35: return 301;
    case 36: return 304;
    case 38: return 302;
    case 48: return 307;
    case 49: return 12;
    case 50: return 133;
    case 52: return 94;
    case 53: return 306;
    case 54: return 309;
    case 55: return 181;
    case 56: return 295;
    case 57: return 6;
    case 59: return 331;
    case 61: return 220;
    case 62: return 19;
    case 63: return 3;
    case 64: return 4;
    case 65: return 145;
    case 66: return 146;
    case 78: return 305;
    case 79: return 762;
    case 80: return 505;
    case 88: return 300;
    case 93: return 1;
    case 94: return 252;
    case 95: return 114;
    case 96: return 258;
    case 98: return 240;
    case 101: return 535;
    case 113: return 728;
    case 124: return 158;
    case 129: return 37;
    case 131: return 270;
    case 132: return 686;
    case 134: return 513;
    case 135: return 175;
    case 139: return 173;
    case 144: return 46;
    case 146: return 23;
    case 147: return 164;
    case 149: return 170;
    case 158: return 205;
    case 159: return 81;
    case 153: return 543;
    case 154: return 57;
    case 155: return 132;
    case 157: return 66;
    case 163: return 597;
    case 165: return 598;
    case 169: return 578;
    case 172: return 20;
    case 173: return 64;
    case 174: return 24;
    case 175: return 49;
    case 176: return 47;
    case 177: return 50;
    case 178: return 224;
    case 179: return 599;
    case 214: return 45;
    case 215: return 91;
    case 220: return 2;
    case 221: return 11;
    case 222: return 192;
    case 226: return 125;
    case 233: return 311;
    case 260: return 114;
    case 261: return 340;
    case 278: return 355;
    case 280: return 299;
    case 291: return 383;
    default: return nr;
    }
}

void aarch64_sync_dispatch(struct trap_frame *frame)
{
    uaddr esr;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    frame->err_code = esr;
    if ((frame->cs & 3) && ((esr >> 26) & 0x3f) == 0x15) {
        frame->int_no = 0x80;
        frame_from_registers(frame);
        frame->eax = frame->x[8];
        syscall_handler(frame);
        registers_from_frame(frame);
        signal_deliver(frame);
        return;
    }
    if (((esr >> 26) & 0x3f) == 0x20 || ((esr >> 26) & 0x3f) == 0x21 ||
            ((esr >> 26) & 0x3f) == 0x24 || ((esr >> 26) & 0x3f) == 0x25) {
        page_fault(frame);
        signal_deliver(frame);
        return;
    }
    aarch64_error_dispatch(frame);
}

void aarch64_irq_dispatch(struct trap_frame *frame)
{
    frame_from_registers(frame);
    frame->int_no = aarch64_irq_number();
    aarch64_irq_eoi();
    int_handler(frame);
    registers_from_frame(frame);
}
