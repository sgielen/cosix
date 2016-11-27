#include "interrupt.hpp"
#include "cpu_io.hpp"

using namespace cloudos;

interrupt_global *global_interrupt = 0;

static const uint8_t master_pic_cmd  = 0x20;
static const uint8_t slave_pic_cmd   = 0xa0;
static const uint8_t master_pic_data = 0x21;
static const uint8_t slave_pic_data  = 0xa1;

#define ISR_NUM(x) extern "C" void isr ## x()
/* Interrupts thrown by the processor */
ISR_NUM(0); ISR_NUM(1); ISR_NUM(2); ISR_NUM(3); ISR_NUM(4); ISR_NUM(5);
ISR_NUM(6); ISR_NUM(7); ISR_NUM(8); ISR_NUM(9); ISR_NUM(10); ISR_NUM(11);
ISR_NUM(12); ISR_NUM(13); ISR_NUM(14); ISR_NUM(15); ISR_NUM(16); ISR_NUM(17);
ISR_NUM(18); ISR_NUM(19); ISR_NUM(20); ISR_NUM(21); ISR_NUM(22); ISR_NUM(23);
ISR_NUM(24); ISR_NUM(25); ISR_NUM(26); ISR_NUM(27); ISR_NUM(28); ISR_NUM(29);
ISR_NUM(30); ISR_NUM(31);
/* Interrupts thrown by the PIC */
ISR_NUM(32); ISR_NUM(33); ISR_NUM(34); ISR_NUM(35); ISR_NUM(36); ISR_NUM(37);
ISR_NUM(38); ISR_NUM(39); ISR_NUM(40); ISR_NUM(41); ISR_NUM(42); ISR_NUM(43);
ISR_NUM(44); ISR_NUM(45); ISR_NUM(46); ISR_NUM(47);
/* Software interrupts */
ISR_NUM(128);
#undef ISR_NUM

extern "C"
void isr_handler(interrupt_state_t *regs) {
	if(global_interrupt != 0) {
		global_interrupt->call(regs);
	}
}

interrupt_global::interrupt_global(interrupt_functor *f)
: functor(f) {
	global_interrupt = this;
}

interrupt_global::~interrupt_global() {
	global_interrupt = nullptr;
}

void interrupt_global::setup(interrupt_table &table) {
	/* use reinterpret_cast<uint64_t> for testing builds on 8-byte ptr archs. */
#define INT(NUM) \
	table.set_entry(NUM, reinterpret_cast<uint64_t>(isr ## NUM), 0x08, 0xee);
	/* Interrupts thrown by the processor */
	INT(0); INT(1); INT(2); INT(3); INT(4); INT(5); INT(6); INT(7); INT(8);
	INT(9); INT(10); INT(11); INT(12); INT(13); INT(14); INT(15); INT(16);
	INT(17); INT(18); INT(19); INT(20); INT(21); INT(22); INT(23); INT(24);
	INT(25); INT(26); INT(27); INT(28); INT(29); INT(30); INT(31);
	/* Interrupts thrown by the PIC */
	INT(32); INT(33); INT(34); INT(35); INT(36); INT(37); INT(38); INT(39);
	INT(40); INT(41); INT(42); INT(43); INT(44); INT(45); INT(46); INT(47);
	/* Software interrupts */
	INT(128);
#undef INTERRUPT

	table.load();
}

void interrupt_global::call(interrupt_state_t *regs) {
	interrupt_functor &f = *functor;
	int int_no = regs->int_no;

	if(int_no >= 0x20 && int_no < 0x30) {
		// IRQ caused this interrupt, so ACK it with the PICs
		outb(master_pic_cmd, 0x20);
		if(int_no >= 0x28) {
			outb(slave_pic_cmd, 0x20);
		}
	}

	f(regs);
}

void interrupt_global::enable_interrupts() {
	asm volatile("sti");
}

void interrupt_global::disable_interrupts() {
	asm volatile("cli");
}

void interrupt_global::reprogram_pic() {
	uint8_t master_pic_mask = inb(master_pic_data);
	uint8_t slave_pic_mask  = inb(slave_pic_data);

	// TODO(sjors): we do not io_wait() after every outb() call, even
	// though this is necessary on older machines. I'll need to add this
	// if I see interrupts fail on such machines.

	// start initialization of the PICs
	outb(master_pic_cmd, 0x11);
	outb(slave_pic_cmd, 0x11);

	// set PIC vector offset for interrupts to start at 32 and 40 respectively
	outb(master_pic_data, 0x20);
	outb(slave_pic_data, 0x28);

	// tell the PICs their orientation to each other
	outb(master_pic_data, 4);
	outb(slave_pic_data, 2);

	// set 8086 mode
	outb(master_pic_data, 0x01);
	outb(slave_pic_data, 0x01);

	// restore masks
	outb(master_pic_data, master_pic_mask);
	outb(slave_pic_data, slave_pic_mask);
}
