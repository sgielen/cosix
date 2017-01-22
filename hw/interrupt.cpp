#include <hw/interrupt.hpp>
#include <hw/cpu_io.hpp>
#include <hw/device.hpp>
#include <global.hpp>
#include <fd/scheduler.hpp>
#include <fd/process_fd.hpp>

using namespace cloudos;

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
	get_interrupt_handler()->handle(regs);
}

const char *int_num_to_name(int int_no, bool *err_code) {
	bool errcode = false;
	const char *str = nullptr;
	switch(int_no) {
	case 0:  str = "Divide-by-zero (#DE)"; break;
	case 1:  str = "Debug (#DB)"; break;
	case 2:  str = "Non-maskable interrupt"; break;
	case 3:  str = "Breakpoint (#BP)"; break;
	case 4:  str = "Overflow (#OF)"; break;
	case 5:  str = "Bound Range Exceeded (#BR)"; break;
	case 6:  str = "Invalid Opcode (#UD)"; break;
	case 7:  str = "Device Not Available (#NM)"; break;
	case 8:  str = "Double Fault (#DF)"; break;
	case 9:  str = "Coprocessor Segment Overrun"; break;
	case 10: str = "Invalid TSS (#TS)"; errcode = true; break;
	case 11: str = "Segment Not Present (#NP)"; errcode = true; break;
	case 12: str = "Stack-Segment Fault (#SS)"; errcode = true; break;
	case 13: str = "General Protection Fault (#GP)"; errcode = true; break;
	case 14: str = "Page Fault (#PF)"; errcode = true; break;

	case 16: str = "x87 Floating-Point Exception (#MF)"; break;
	case 17: str = "Alignment Check (#AC)"; errcode = true; break;
	case 18: str = "Machine Check (#MC)"; break;
	case 19: str = "SIMD Floating-Point Exception (#XM/#XF)"; break;
	case 20: str = "Virtualization Exception (#VE)"; break;

	case 30: str = "Security Exception (#SX)"; errcode = true; break;

	default: str = "Unknown exception"; break;
	}
	if(err_code) *err_code = errcode;
	return str;
}

static void dump_stack(uintptr_t *ebp, void *eip) {
	auto &stream = get_vga_stream();
	stream << dec;
	int frame = 0;
	while(ebp && eip) {
		stream << "frame " << frame << " at " << eip << "\n";
		eip = reinterpret_cast<void*>(*(ebp + 1));
		ebp = reinterpret_cast<uintptr_t*>(*ebp);
		frame++;
	}
}

__attribute__((noreturn)) static void fatal_exception(int int_no, int err_code, interrupt_state_t *regs) {
	auto &stream = get_vga_stream();
	static int fatal_exception_occurred = 0;
	fatal_exception_occurred++;
	if(fatal_exception_occurred == 2) {
		stream << "\nFatal exception occurred during handling of another fatal exception, aborting!\n";
	}
	if(fatal_exception_occurred >= 2) {
		asm volatile("cli; halted2: hlt; jmp halted2;");
	}
	stream << "\nFatal exception during processing in kernel\n";
	bool errcode;
	stream << "Interrupt number: " << dec << int_no << " - " << int_num_to_name(int_no, &errcode);
	if(errcode) {
		stream << " - Error Code: 0x" << hex << err_code << dec;
	}
	stream << "\n";

	auto thread = get_scheduler()->get_running_thread();
	if(thread) {
		stream << "Active process: " << thread->get_process() << " (\"" << thread->get_process()->name << "\")\n";
	}

	stream << hex;
	stream << "EAX " << regs->eax << "   EBX " << regs->ebx << "\nECX " << regs->ecx << "   EDX " << regs->edx << "\n";
	stream << "ESP " << regs->esp << "   EBP " << regs->ebp << "\nESI " << regs->esi << "   EDI " << regs->edi << "\n";
	stream << "EIP " << regs->eip << "   EFLAGS " << regs->eflags << "\n";
	stream << "UESP " << regs->useresp << "  CS " << regs->cs << "  SS " << regs->ss << "\n";
	stream << dec << "\n";

	if(int_no == 0x0e /* Page fault */) {
		if(err_code & 0x01) {
			stream << "Caused by a page-protection violation during page ";
		} else {
			stream << "Caused by a non-present page during page ";
		}
		stream << ((err_code & 0x02) ? "write" : "read");
		stream << ((err_code & 0x04) ? " in unprivileged mode" : " in kernel mode");
		if(err_code & 0x08) {
			stream << " as a result of reading a reserved field";
		}
		if(err_code & 0x10) {
			stream << " as a result of an instruction fetch";
		}
		stream << "\n";
		uint32_t address;
		asm volatile("mov %%cr2, %0" : "=a"(address));
		stream << "Virtual address accessed: 0x" << hex << address << dec << "\n";
	}

	dump_stack(reinterpret_cast<uintptr_t*>(regs->ebp), reinterpret_cast<void*>(regs->eip));

	stream << "\n";
	kernel_panic("A fatal exception occurred.");
}

void interrupt_handler::setup(interrupt_table &table) {
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

void interrupt_handler::handle_irq(uint8_t irq) {
	// IRQ caused this interrupt, so ACK it with the PICs
	outb(master_pic_cmd, 0x20);
	if(irq >= 8) {
		outb(slave_pic_cmd, 0x20);
	}

	auto handler = irq_handlers[irq];
	if(handler != 0) {
		handler->handle_irq(irq);
	} else {
		get_vga_stream() << "Got unknown hardware interrupt " << irq << "\n";
	}
}

void interrupt_handler::handle(interrupt_state_t *regs) {
	int int_no = regs->int_no;
	int err_code = regs->err_code;

	if(regs->cs != 27 && regs->cs != 8) {
		get_vga_stream() << "!!!! Interrupt occurred, but unexpected code segment value !!!!\n";
		fatal_exception(int_no, err_code, regs);
	}

	bool in_kernel = regs->cs == 8;
	auto running_thread = get_scheduler()->get_running_thread();
	if(running_thread) {
		running_thread->set_return_state(regs);
	}

	if(!in_kernel && !running_thread) {
		get_vga_stream() << "!!!! Interrupt occurred in userland, but without an active thread !!!!\n";
		fatal_exception(int_no, err_code, regs);
	}

	// TODO: handle page fault as a special case, because it can be
	// solved by the running_thread

	// Any exceptions in the userland are handled by the thread
	if(!in_kernel && (int_no < 0x20 || int_no >= 0x30)) {
		// During the handling of this interrupt, we might exit this thread and
		// switch to another, then clean the thread. To ensure this is possible,
		// ensure we don't have a shared ptr to the thread on the stack.
		assert(running_thread.use_count() > 1);
		thread *thr = running_thread.get();
		running_thread.reset();
		thr->interrupt(int_no, err_code);
		// interrupt returned, so this thread survived
		running_thread = get_scheduler()->get_running_thread();
		assert(running_thread);
		assert(running_thread.get() == thr);
	}
	// Any exceptions in the kernel lead to immediate kernel_panic
	else if(int_no < 0x20 || int_no >= 0x30) {
		fatal_exception(int_no, err_code, regs);
	}
	// Hardware interrupts are handled normally
	else {
		handle_irq(int_no - 0x20);
	}

	if(running_thread) {
		assert(!running_thread->is_exited());
		running_thread->get_return_state(regs);
	}
}

void interrupt_handler::enable_interrupts() {
	asm volatile("sti");
}

void interrupt_handler::disable_interrupts() {
	asm volatile("cli");
}

void interrupt_handler::reprogram_pic() {
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

irq_handler::~irq_handler() {}

void irq_handler::register_irq(uint8_t irq) {
	get_interrupt_handler()->register_irq_handler(irq, this);
}

interrupt_handler::interrupt_handler() {
	for(size_t i = 0; i < NUM_ELEMENTS(irq_handlers); ++i) {
		irq_handlers[i] = nullptr;
	}
}

void interrupt_handler::register_irq_handler(uint8_t irq, irq_handler *handler) {
	assert(irq < NUM_ELEMENTS(irq_handlers));
	assert(irq_handlers[irq] == nullptr);

	irq_handlers[irq] = handler;
}

