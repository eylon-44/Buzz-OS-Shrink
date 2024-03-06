// Interrupt Descriptor Table // ~ eylon

#include <cpu/interrupts/idt.h>
#include <libc/stdint.h>

static InterruptDescriptor idt[IDT_ENTRIES_COUNT];
static IDTR idtr;

// Set an interrupt descriptor in the IDT array
void set_interrupt_descriptor(uint8_t index, uint32_t handler_address)
{
    idt[index].offset_low       = low32(handler_address);
    idt[index].segment_selector = (uint16_t) 8; // [TODO] set a kernel segment
    idt[index].reserved_zero    = (uint8_t) 0b00000000;
    idt[index].type_attributes  = (uint8_t) 0b10001110;
    idt[index].offset_high      = high32(handler_address);
}

// Set the IDT descriptor and load it into the PIC
void load_idt()
{
    // set the IDT descriptor
    idtr.size   = (uint16_t) sizeof(idt) - 1;
    idtr.offset = (uint32_t) &idt;

    // load idtr
    __asm__ __volatile__("lidt (%0)" : : "r" (&idtr));
}