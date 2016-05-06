#pragma once

#include <cstdint>

namespace Chip8Globals {
	namespace X86_STATE {
		// Interrupt status codes used in the x86 state. When an interrupt is raised it will have one of these values.
		enum X86_INT_STATUS_CODE {
			PREPARE_FOR_JUMP = 0,
			USE_INTERPRETER = 1,
			OUT_OF_CODE = 2,
			PREPARE_FOR_INDIRECT_JUMP = 3,
			SELF_MODIFYING_CODE = 4,
			DEBUG = 5,
			WAIT_FOR_KEYPRESS = 6,
			PREPARE_FOR_STACK_JUMP = 7
		};

		extern uint8_t * x86_resume_address; // Used as the entry point into the recompiled code (the CDECL cache jumps to this address when run).

		// When the CodeEmitter_x86::DYNAREC_EMIT_INTERRUPT (or otherwise) function code is executed inside a cache, it will write values to these parameters so the dispatcher loop can handle the interrupt.
		// Not all are used when an interrupt is raised, but the x86_interrupt_status_code will always be written to.
		extern X86_INT_STATUS_CODE x86_interrupt_status_code; // Used to determine which type of interrupt happened.
		extern uint16_t x86_interrupt_c8_param1; // Used with many interrupts.
		extern uint16_t x86_interrupt_c8_param2; // Used with PREPARE_FOR_STACK_JUMP interrupts.
		extern uint8_t * x86_interrupt_x86_param1; // Used with OUT_OF_CODE interrupts.

#ifdef USE_DEBUG
		extern char * x86_int_status_code_strings[];
		extern void DEBUG_printX86_STATE();
#endif

	}
}