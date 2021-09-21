#include <stddef.h>
#include <stdint.h>
#include <sys/idt.h>
#include <sys/cpu.h>
#include <sys/pic.h>
#include <sys/lapic.h>
#include <lib/blib.h>
#include <mm/pmm.h>

#if bios == 1

static struct idt_entry idt_entries[32];

__attribute__((section(".realmode")))
struct idtr idt = {
    sizeof(idt_entries) - 1,
    (uintptr_t)idt_entries
};

static void register_interrupt_handler(size_t vec, void *handler, uint8_t type) {
    uint32_t p = (uint32_t)handler;

    idt_entries[vec].offset_lo = (uint16_t)p;
    idt_entries[vec].selector = 0x18;
    idt_entries[vec].unused = 0;
    idt_entries[vec].type_attr = type;
    idt_entries[vec].offset_hi = (uint16_t)(p >> 16);
}

extern void *exceptions[];

void init_idt(void) {
    for (size_t i = 0; i < SIZEOF_ARRAY(idt_entries); i++)
        register_interrupt_handler(i, exceptions[i], 0x8e);

    asm volatile ("lidt %0" :: "m"(idt) : "memory");
}

#endif

static struct idt_entry *dummy_idt = NULL;

__attribute__((interrupt))
static void dummy_isr(void *p) {
    (void)p;
    lapic_eoi();
}

void init_flush_irqs(void) {
    dummy_idt = ext_mem_alloc(256 * sizeof(struct idt_entry));

    for (size_t i = 0; i < 256; i++) {
        dummy_idt[i].offset_lo = (uint16_t)(uintptr_t)dummy_isr;
        dummy_idt[i].type_attr = 0x8e;
#if defined (__i386__)
        dummy_idt[i].selector = 0x18;
        dummy_idt[i].offset_hi = (uint16_t)((uintptr_t)dummy_isr >> 16);
#elif defined (__x86_64__)
        dummy_idt[i].selector = 0x28;
        dummy_idt[i].offset_mid = (uint16_t)((uintptr_t)dummy_isr >> 16);
        dummy_idt[i].offset_hi = (uint32_t)((uintptr_t)dummy_isr >> 32);
#endif
    }
}

void flush_irqs(void) {
    struct idtr old_idt;
    asm volatile ("sidt %0" : "=m"(old_idt) :: "memory");

    struct idtr new_idt = {
        256 * sizeof(struct idt_entry) - 1,
        (uintptr_t)dummy_idt
    };
    asm volatile ("lidt %0" :: "m"(new_idt) : "memory");

    // Flush the legacy PIC so we know the remaining ints come from the LAPIC
    pic_flush();

    asm volatile ("sti" ::: "memory");

    // Delay a while to make sure we catch ALL pending IRQs
    delay(10000000);

    asm volatile ("cli" ::: "memory");

    asm volatile ("lidt %0" :: "m"(old_idt) : "memory");
}
