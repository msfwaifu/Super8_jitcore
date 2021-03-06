#include "stdafx.h"

#include <cstdint>

#include "Headers\Globals.h"

#include "Headers\Chip8Globals\Chip8Globals.h"
#include "Headers\Chip8Engine\Chip8Engine_Dynarec.h"
#include "Headers\Chip8Engine\Chip8Engine_CodeEmitter_x86.h"
#include "Headers\Chip8Engine\Chip8Engine_CacheHandler.h"
#include "Headers\Chip8Engine\Chip8Engine_JumpHandler.h"
#include "Headers\Chip8Engine\Chip8Engine_Key.h"
#include "Headers\Chip8Engine\Chip8Engine_Timers.h"
#include "Headers\Chip8Engine\Chip8Engine_StackHandler.h"

using namespace Chip8Globals;

Chip8Engine_Dynarec::Chip8Engine_Dynarec()
{
	// Register this component in logger
	logger->registerComponent(this);
}

Chip8Engine_Dynarec::~Chip8Engine_Dynarec()
{
	// Deregister this component in logger
	logger->deregisterComponent(this);
}

std::string Chip8Engine_Dynarec::getComponentName()
{
	return std::string("Dynarec");
}

void Chip8Engine_Dynarec::emulateTranslatorCycle() {
	// Decode Opcode
	// Initially work out what type of opcode it is by AND with 0xF000 and branch from that (looks at MSB)
	switch (C8_STATE::opcode & 0xF000) {
	case 0x0000:
		handleOpcodeMSN_0();
		break;
	case 0x1000:
		handleOpcodeMSN_1();
		break;
	case 0x2000:
		handleOpcodeMSN_2();
		break;
	case 0x3000:
		handleOpcodeMSN_3();
		break;
	case 0x4000:
		handleOpcodeMSN_4();
		break;
	case 0x5000:
		handleOpcodeMSN_5();
		break;
	case 0x6000:
		handleOpcodeMSN_6();
		break;
	case 0x7000:
		handleOpcodeMSN_7();
		break;
	case 0x8000:
		handleOpcodeMSN_8();
		break;
	case 0x9000:
		handleOpcodeMSN_9();
		break;
	case 0xA000:
		handleOpcodeMSN_A();
		break;
	case 0xB000:
		handleOpcodeMSN_B();
		break;
	case 0xC000:
		handleOpcodeMSN_C();
		break;
	case 0xD000:
		handleOpcodeMSN_D();
		break;
	case 0xE000:
		handleOpcodeMSN_E();
		break;
	case 0xF000:
		handleOpcodeMSN_F();
		break;
	default:
		// Unknown opcode encountered
#ifdef USE_VERBOSE
		//printf("Dynarec:	Unknown Opcode detected! Skipping (0x%.4X)\n", C8_STATE::opcode);
#endif
		break;
	}
}

void Chip8Engine_Dynarec::handleOpcodeMSN_0() {
	switch (C8_STATE::opcode) {
	case 0x00E0:
	{
		// 0x00E0: Clears the screen
		// Uses interpreter
		emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::USE_INTERPRETER, C8_STATE::opcode);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);
		C8_STATE::C8_incrementPC();
		break;
	}
	case 0x00EE:
	{
		// 0x00EE: Returns from a subroutine - direct jump (uses stack)!
		// TODO: Check if correct.

		// Emit jump
		emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::PREPARE_FOR_STACK_JUMP, C8_STATE::opcode);
		emitter->JMP_M_PTR_32((uint32_t*)&stack->x86_address_to);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		// Stop translating for this block
		Dynarec::block_finished = true;
		break;
	}
	default:
	{
		// 0x0NNN: Calls RCA 1802 program at address 0xNNN. (?)
		// TODO: Implement?. Skips instruction for now.
#ifdef USE_VERBOSE
		char buffer[1000];
		sprintf_s(buffer, 1000, "RCA call happened but no code! Skipping (0x%.4X).", C8_STATE::opcode);
		logMessage(LOGLEVEL::L_WARNING, buffer);
#endif
		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);
		C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
		break;
	}
	}
}

void Chip8Engine_Dynarec::handleOpcodeMSN_1() {
	// Only one subtype of opcode in this branch
	// 0x1NNN jumps to address 0xNNN (set PC) - direct jump!
	// TODO: check if correct

	// Get values
	uint16_t jump_c8_pc = C8_STATE::opcode & 0x0FFF;

	// Get jump table entry
	int32_t tblindex = jumptbl->getJumpIndexByC8PC(jump_c8_pc);

	// Emit jump
	emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::PREPARE_FOR_JUMP, jump_c8_pc);
	emitter->JMP_M_PTR_32((uint32_t*)&jumptbl->getJumpInfoByIndex(tblindex)->x86_address_to);

	// Need to change pc to jump address as it will cause problems if its not pc-aligned to 0 throughout the whole program
	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

	// Stop translating for this block
	Dynarec::block_finished = true;
}

void Chip8Engine_Dynarec::handleOpcodeMSN_2() {
	// Only one subtype of opcode in this branch
	// 0x2NNN calls the subroutine at address 0xNNN - direct jump, however handle using stack

	// Emit jump
	emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::PREPARE_FOR_STACK_JUMP, C8_STATE::opcode, C8_STATE::cpu.pc + 2);
	emitter->JMP_M_PTR_32((uint32_t*)&stack->x86_address_to);

	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

	// Stop translating for this block
	Dynarec::block_finished = true;
}

void Chip8Engine_Dynarec::handleOpcodeMSN_3() {
	// Only one subtype of opcode in this branch
	// 0x3XNN skips next instruction if VX equals NN.
	// TODO: check if correct

	// First determine values
	uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8;
	uint8_t num = (C8_STATE::opcode & 0x00FF);

	// Emit conditional code
	emitter->MOV_MtoR_8(al, &C8_STATE::cpu.V[vx]);
	emitter->CMP_RwithImm_8(al, num);
	emitter->JE_32(0x00000000); // to fill in by jump table

	// Record cond jump in table (so it will get updated on every translator loop)
	jumptbl->recordConditionalJumpEntry(C8_STATE::cpu.pc, C8_STATE::cpu.pc + 4, 2, (uint32_t *)(cache->getEndX86AddressCurrent() - 4)); // address is located at current x86 pc - 4 (relative takes up 4 bytes)

	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);
	// Change C8 PC
	C8_STATE::C8_incrementPC();
}

void Chip8Engine_Dynarec::handleOpcodeMSN_4() {
	// Only one subtype of opcode in this branch
	// 0x4XNN skips next instruction if VX does not equal NN
	// TODO: check if correct
	// First determine values
	uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8;
	uint8_t num = (C8_STATE::opcode & 0x00FF);

	// Emit conditional code
	emitter->MOV_MtoR_8(al, &C8_STATE::cpu.V[vx]);
	emitter->CMP_RwithImm_8(al, num);
	emitter->JNE_32(0x00000000); // to fill in by jump table

	// Record cond jump in table (so it will get updated on every translator loop)
	jumptbl->recordConditionalJumpEntry(C8_STATE::cpu.pc, C8_STATE::cpu.pc + 4, 2, (uint32_t *)(cache->getEndX86AddressCurrent() - 4)); // address is located at current x86 pc - 4 (relative takes up 4 bytes)

	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

	// Change C8 PC
	C8_STATE::C8_incrementPC();
}

void Chip8Engine_Dynarec::handleOpcodeMSN_5() {
	// Only one subtype of opcode in this branch
	// 0x5XY0 skips next instruction if VX equals XY
	// TODO: check if correct
	// First determine values
	uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8;
	uint8_t vy = (C8_STATE::opcode & 0x00F0) >> 4;

	// Emit conditional code
	emitter->MOV_MtoR_8(al, &C8_STATE::cpu.V[vx]);
	emitter->MOV_MtoR_8(cl, &C8_STATE::cpu.V[vy]);
	emitter->CMP_RwithR_8(al, cl);
	emitter->JE_32(0x00000000); // to fill in by jump table

	// Record cond jump in table (so it will get updated on every translator loop)
	jumptbl->recordConditionalJumpEntry(C8_STATE::cpu.pc, C8_STATE::cpu.pc + 4, 2, (uint32_t *)(cache->getEndX86AddressCurrent() - 4)); // address is located at current x86 pc - 4 (relative takes up 4 bytes)

	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

	// Change C8 PC
	C8_STATE::C8_incrementPC();
}

void Chip8Engine_Dynarec::handleOpcodeMSN_6() {
	// Only one subtype of opcode in this branch
	// 0x6XNN sets VX to NN
	// TODO: check if correct
	uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8;
	uint8_t num = (C8_STATE::opcode & 0x00FF);
	emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + vx, num);

	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

	C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
}

void Chip8Engine_Dynarec::handleOpcodeMSN_7() {
	// Only one subtype of opcode in this branch
	// 0x7XNN adds NN to Vx
	// TODO: check if correct
	uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8;
	uint8_t num = (C8_STATE::opcode & 0x00FF);
	emitter->ADD_ImmtoM_8(C8_STATE::cpu.V + vx, num);

	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

	C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
}

void Chip8Engine_Dynarec::handleOpcodeMSN_8() {
	switch (C8_STATE::opcode & 0x000F) {
	case 0x0000:
	{
		// 0x8XY0: Sets VX to the value of VY
		// TODO: Check if correct
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 16 to get to a single base16 digit.
		uint8_t vy = (C8_STATE::opcode & 0x00F0) >> 4; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vy);
		emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Goto next opcode
		break;
	}
	case 0x0001:
	{
		// 0x8XY1: Sets VX to VX OR VY
		// TODO: Check if correct
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 16 to get to a single base16 digit.
		uint8_t vy = (C8_STATE::opcode & 0x00F0) >> 4; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vx);
		emitter->OR_RwithM_8(al, C8_STATE::cpu.V + vy);
		emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Goto next opcode
		break;
	}
	case 0x0002:
	{
		// 0x8XY2: Sets VX to VX AND VY
		// TODO: Check if correct
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 16 to get to a single base16 digit.
		uint8_t vy = (C8_STATE::opcode & 0x00F0) >> 4; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vx);
		emitter->AND_RwithM_8(al, C8_STATE::cpu.V + vy);
		emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Goto next opcode
		break;
	}
	case 0x0003:
	{
		// 0x8XY3: Sets VX to VX XOR VY
		// TODO: Check if correct
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 16 to get to a single base16 digit.
		uint8_t vy = (C8_STATE::opcode & 0x00F0) >> 4; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vx);
		emitter->XOR_RwithM_8(al, C8_STATE::cpu.V + vy);
		emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Goto next opcode
		break;
	}
	case 0x0004:
	{
		// 0x8XY4: Adds Vy to Vx, setting VF to 1 when there is a carry and 0 when theres not.
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 16 to get to a single base16 digit.
		uint8_t vy = (C8_STATE::opcode & 0x00F0) >> 4; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + 0xF, 0);
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vx);
		emitter->ADD_MtoR_8(al, C8_STATE::cpu.V + vy);
		emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);
		emitter->JNC_8(7); // Next instruction is 7 bytes long
		emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + 0xF, 1);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Goto next opcode
		break;
	}
	case 0x0005:
	{
		// 0x8XY5: Vy is subtracted from Vx. VF set to 0 when theres a borrow, and 1 when there isnt.
		// TODO: Check if correct
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 16 to get to a single base16 digit.
		uint8_t vy = (C8_STATE::opcode & 0x00F0) >> 4; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + 0xF, 1);
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vx);
		emitter->SUB_MfromR_8(al, C8_STATE::cpu.V + vy);
		emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);
		emitter->JNC_8(7); // Next instruction is 7 bytes long
		emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + 0xF, 0);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Goto next opcode
		break;
	}
	case 0x0006:
	{
		// 0x8XY6: Shifts Vx right by one. VF is set to the LSB of Vx before the shift.
		// TODO: Check if correct
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 16 to get to a single base16 digit.
		emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + 0xF, 0);
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vx);
		emitter->SHR_R_8(al, 1);
		emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);
		emitter->JNC_8(7);
		emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + 0xF, 1);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Goto next opcode
		break;
	}
	case 0x0007:
	{
		// 0x8XY7: Sets Vx to Vy minus Vx. VF is set to 0 when theres a borrow, and 1 where there isnt.
		// TODO: Check if correct
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 16 to get to a single base16 digit.
		uint8_t vy = (C8_STATE::opcode & 0x00F0) >> 4; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + 0xF, 1);
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vy);
		emitter->SUB_MfromR_8(al, C8_STATE::cpu.V + vx);
		emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);
		emitter->JNC_8(7); // Next instruction is 7 bytes long
		emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + 0xF, 0);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Goto next opcode
		break;
	}
	case 0x000E:
	{
		// 0x8XYE: Shifts Vx left by one. VF is set to the value of the MSB of Vx before the shift.
		// TODO: Check if correct
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 16 to get to a single base16 digit.
		emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + 0xF, 0);
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vx);
		emitter->SHL_R_8(al, 1);
		emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);
		emitter->JNC_8(7);
		emitter->MOV_ImmtoM_8(C8_STATE::cpu.V + 0xF, 1);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Goto next opcode
		break;
	}
	default:
	{
#ifdef USE_VERBOSE
		char buffer[1000];
		sprintf_s(buffer, 1000, "Unknown Opcode detected! Skipping (0x%.4X).", C8_STATE::opcode);
		logMessage(LOGLEVEL::L_WARNING, buffer);
#endif
		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
		break;
	}
	}
}

void Chip8Engine_Dynarec::handleOpcodeMSN_9() {
	switch (C8_STATE::opcode & 0x000F) {
	case 0x0000:
	{
		// 0x9XY0: Skips next instruction if register VX does not equal register VY
		// TODO: Check if correct
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8;
		uint8_t vy = (C8_STATE::opcode & 0x00F0) >> 4;

		// Emit conditional code
		emitter->MOV_MtoR_8(al, &C8_STATE::cpu.V[vx]);
		emitter->MOV_MtoR_8(cl, &C8_STATE::cpu.V[vy]);
		emitter->CMP_RwithR_8(al, cl);
		emitter->JNE_32(0x00000000); // to fill in by jump table

		// Record cond jump in table (so it will get updated on every translator loop)
		jumptbl->recordConditionalJumpEntry(C8_STATE::cpu.pc, C8_STATE::cpu.pc + 4, 2, (uint32_t *)(cache->getEndX86AddressCurrent() - 4)); // address is located at current x86 pc - 4 (relative takes up 4 bytes)

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		// Change C8 PC
		C8_STATE::C8_incrementPC();
		break;
	}
	default:
	{
#ifdef USE_VERBOSE
		char buffer[1000];
		sprintf_s(buffer, 1000, "Unknown Opcode detected! Skipping (0x%.4X).", C8_STATE::opcode);
		logMessage(LOGLEVEL::L_WARNING, buffer);
#endif
		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
		break;
	}
	}
}

void Chip8Engine_Dynarec::handleOpcodeMSN_A() {
	// Only one subtype of opcode in this branch
	// 0xANNN: Sets I to the address NNN
	// TODO: Check if correct
	emitter->MOV_ImmtoM_16(&C8_STATE::cpu.I, (C8_STATE::opcode & 0x0FFF));

	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

	C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
}

void Chip8Engine_Dynarec::handleOpcodeMSN_B() {
	// Only one subtype of opcode in this branch
	// 0xBNNN: Sets PC to the address (NNN + V0) aka INDIRECT JUMP!

	// Emit jump
	// Need to determine jump location - move the num to register, then add v0 to it, then write back to the jump table.
	// Need to also interrupt so we can determine the cache where the jump should lead to.
	emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::PREPARE_FOR_INDIRECT_JUMP, C8_STATE::opcode);
	emitter->JMP_M_PTR_32((uint32_t*)&jumptbl->x86_indirect_jump_address);

	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

	// Stop translating for this block
	Dynarec::block_finished = true;
}

void Chip8Engine_Dynarec::handleOpcodeMSN_C() {
	// Only one subtype of opcode in this branch
	// 0xCXNN: Sets Vx to the result of 0xNN & (random number)
	// TODO: Check if correct.
	uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 8 to get to a single base16 digit.
	uint8_t opcodenum = C8_STATE::opcode & 0xFF; // Number from opcode.
	emitter->RDTSC(); // eax will contain the lower 32 bits of the timestamp, which is random enough (pseudo-random)
	emitter->AND_RwithImm_8(al, opcodenum);
	emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);

	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

	// Change PC
	C8_STATE::C8_incrementPC();
}

void Chip8Engine_Dynarec::handleOpcodeMSN_D() {
	// Only one subtype of opcode in this branch
	/* 0xDXYN:	Draws a sprite at coordinate (VX, VY) that has a width of 8 pixels and a height of N pixels.
				Each row of 8 pixels is read as bit-coded starting from memory location I;
				I value doesn�t change after the execution of this instruction.
				As described above, VF is set to 1 if any screen pixels are flipped from
				set to unset when the sprite is drawn, and to 0 if that doesn�t happen */
	// TODO: check if correct.
	emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::USE_INTERPRETER, C8_STATE::opcode);

	// Set region pc to current c8 pc
	cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

	// Change PC
	C8_STATE::C8_incrementPC();
}

void Chip8Engine_Dynarec::handleOpcodeMSN_E() {
	switch (C8_STATE::opcode & 0x00FF) {
	case 0x009E:
	{
		// 0xEX9E: Skips the next instruction if the key stored in Vx is pressed. (indirect jump)
		// TODO: Check if correct.

		// Get values.
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8;

		// Emit conditional code
		emitter->MOV_MtoR_8(cl, C8_STATE::cpu.V + vx);
		emitter->MOV_ImmtoR_32(eax, (uint32_t)key->key);
		emitter->ADD_RtoR_8(al, cl); // CAREFUL! No bounds checking, so cl must be less than 16 (dec) in order to stay in array bounds
		emitter->MOV_PTRtoR_8(dl, eax);
		emitter->CMP_RwithImm_8(dl, 1);
		emitter->JE_32(0x00000000); // to fill in by jump table

		// Record cond jump in table (so it will get updated on every translator loop)
		jumptbl->recordConditionalJumpEntry(C8_STATE::cpu.pc, C8_STATE::cpu.pc + 4, 2, (uint32_t *)(cache->getEndX86AddressCurrent() - 4)); // address is located at current x86 pc - 4 (relative takes up 4 bytes)

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		// Change C8 PC
		C8_STATE::C8_incrementPC();
		break;
	}
	case 0x00A1:
	{
		// 0xEXA1: Skips the next instruction if the key stored in Vx isnt pressed.
		// TODO: Check if correct.
		// Get values.
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8;

		// Emit conditional code
		emitter->MOV_MtoR_8(cl, C8_STATE::cpu.V + vx);
		emitter->MOV_ImmtoR_32(eax, (uint32_t)key->key);
		emitter->ADD_RtoR_8(al, cl); // CAREFUL! No bounds checking, so cl must be less than 16 (dec) in order to stay in array bounds
		emitter->MOV_PTRtoR_8(dl, eax);
		emitter->CMP_RwithImm_8(dl, 0);
		emitter->JE_32(0x00000000); // to fill in by jump table

		// Record cond jump in table (so it will get updated on every translator loop)
		jumptbl->recordConditionalJumpEntry(C8_STATE::cpu.pc, C8_STATE::cpu.pc + 4, 2, (uint32_t *)(cache->getEndX86AddressCurrent() - 4)); // address is located at current x86 pc - 4 (relative takes up 4 bytes)

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		// Change C8 PC
		C8_STATE::C8_incrementPC();
		break;
	}
	default:
	{
#ifdef USE_VERBOSE
		char buffer[1000];
		sprintf_s(buffer, 1000, "Unknown Opcode detected! Skipping (0x%.4X).", C8_STATE::opcode);
		logMessage(LOGLEVEL::L_WARNING, buffer);
#endif
		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
		break;
	}
	}
}

void Chip8Engine_Dynarec::handleOpcodeMSN_F() {
	switch (C8_STATE::opcode & 0x00FF) {
	case 0x0007:
	{
		// 0xFX07: Sets Vx to the value of the delay timer.
		// TODO: check if correct.
		emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::UPDATE_TIMERS, C8_STATE::opcode);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
		break;
	}
	case 0x000A:
	{
		// 0xFX0A: A key press is awaited, then stored in Vx.
		// TODO: Check if correct.
		// check if in sync
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::WAIT_FOR_KEYPRESS, C8_STATE::opcode); // This will put the key (single value from 0x0 to 0xF) in key->x86_key_pressed
		emitter->MOV_MtoR_8(al, &key->X86_KEY_PRESSED);
		emitter->MOV_RtoM_8(C8_STATE::cpu.V + vx, al);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC();
		break;
	}
	case 0x0015:
	{
		// 0xFX15: Sets the delay timer to Vx.
		// TODO: check if correct.
		emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::UPDATE_TIMERS, C8_STATE::opcode);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
		break;
	}
	case 0x0018:
	{
		// 0xFX18: Sets the sound timer to Vx.
		// TODO: check if correct.
		emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::UPDATE_TIMERS, C8_STATE::opcode);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
		break;
	}
	case 0x001E:
	{
		// 0xFX1E: Adds Vx to I.
		// TODO: check if correct.
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->MOV_MtoR_16(ax, &C8_STATE::cpu.I);
		// Cant just use add_MtoR_8 here as if it goes over 0xFF in al, the carry is not added on to the ax register.
		emitter->XOR_RwithR_32(ecx, ecx); // clear ecx register
		emitter->MOV_MtoR_8(cl, C8_STATE::cpu.V + vx);
		emitter->ADD_RtoR_16(ax, cx);
		emitter->MOV_RtoM_16(&C8_STATE::cpu.I, ax);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
		break;
	}
	case 0x0029:
	{
		// 0xFX29: Sets I to the location of the sprite for the character in Vx. Chars 0-F (in hex) are represented by a 4x5 font.
		//         ie: if V[x] = 0xA, set I to location of A in font sheet. Note that sprites are 8-bits wide, while fonts are 4-bits, so
		//             the 4-bits at the end are padded (0's).
		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vx);
		emitter->MOV_ImmtoR_8(cl, 5); // 5 = width constant of fonts
		emitter->MUL_RwithR_8(cl);
		emitter->MOV_RtoM_16(&C8_STATE::cpu.I, ax);

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
		break;
	}
	case 0x0033:
	{
		// 0xFX33: Splits the decimal representation of Vx into 3 locations: hundreds stored in address I, tens in address I+1, and ones in I+2.
		// Check: dont know if this apply's to signed numbers as well, but I'm assuming this is just for unsigned numbers (no documentation)
		//emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::USE_INTERPRETER, C8_STATE::opcode);
		//emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::DEBUG, C8_STATE::opcode);

		// This is self modifying code! Invalidate cache that the memory writes to
		emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::SELF_MODIFYING_CODE, C8_STATE::opcode);

		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 8 to get to a single base16 digit.
		emitter->XOR_RwithR_32(eax, eax); // clear eax register
		// Move the address from I into register edx (= starting address of memory array + offset from I)
		emitter->MOV_ImmtoR_32(edx, (uint32_t)C8_STATE::memory);
		emitter->ADD_MtoR_16(dx, &C8_STATE::cpu.I);
		emitter->MOV_MtoR_8(al, C8_STATE::cpu.V + vx); // Move Vx value into al
		// Start with 100's
		emitter->MOV_ImmtoR_8(cl, 100); // Move 100 value into cl
		emitter->DIV_RwithR_8(cl); // Result is in AX register. AX = al/cl, quotient in al, remainder in ah
		emitter->MOV_RtoPTR_8(edx, al); // Move result to Mem+I (ptr in edx)
		// 10's
		emitter->ADD_ImmtoR_8(edx, 1); // point edx to Mem+I+1
		emitter->SHR_R_32(eax, 8); // Shift (e)ax right by 7, get the remainder into al
		emitter->MOV_ImmtoR_8(cl, 10); // Move 10 into cl
		emitter->DIV_RwithR_8(cl); // Result is in AX register. AX = al/cl, quotient in al, remainder in ah
		emitter->MOV_RtoPTR_8(edx, al); // Move result to Mem+I+1 (ptr in edx)
		// 1's
		emitter->ADD_ImmtoR_8(edx, 1); // point edx to Mem+I+2
		emitter->SHR_R_32(eax, 8); // Shift (e)ax right by 7, get the remainder into al
		// Dont need to divide, as its by 1!
		emitter->MOV_RtoPTR_8(edx, al); // Move result to Mem+I+2 (ptr in edx)

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		// Change PC
		C8_STATE::C8_incrementPC();
		break;
	}
	case 0x0055:
	{
		// 0xFX55: Copies all current values in registers V0 -> Vx to memory starting at address I.

		// This is self modifying code! Invalidate cache that the memory writes to
		emitter->DYNAREC_EMIT_INTERRUPT(X86_STATE::SELF_MODIFYING_CODE, C8_STATE::opcode);

		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 8 to get to a single base16 digit.
		// Setup loop
		// Move the address from I into register eax (= starting address of memory array + offset from I)
		emitter->MOV_ImmtoR_32(eax, (uint32_t)C8_STATE::memory);
		emitter->ADD_MtoR_16(ax, &C8_STATE::cpu.I);
		// Move address of V[0] into edx register
		emitter->MOV_ImmtoR_32(edx, (uint32_t)C8_STATE::cpu.V);
		// Start loop
		emitter->MOV_PTRtoR_8(cl, edx); // Move 8bit value from PTR @ edx (c8 V address + loop number) into cl register
		emitter->MOV_RtoPTR_8(eax, cl); // Move 8bit value from cl register into PTR @ eax (c8 memory + I register + loop number)
		// Add one to each PTR to increment loop
		emitter->ADD_ImmtoR_8(eax, 1);
		emitter->ADD_ImmtoR_8(edx, 1);
		// Compare edx with final V address (to stop loop)
		emitter->CMP_RwithImm_32(edx, (uint32_t)(C8_STATE::cpu.V + vx + 1)); // "less than" compare
		emitter->JNE_8(-18); // jump to start of loop -(2+6+3+3+2+2) = -18

		 // Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		// Change PC
		C8_STATE::C8_incrementPC();
		break;
	}
	case 0x0065:
	{
		// 0xFX65: Copies memory starting from address I to all registers V0 -> Vx.
		// TODO: check if correct.

		uint8_t vx = (C8_STATE::opcode & 0x0F00) >> 8; // Need to bit shift by 8 to get to a single base16 digit.
		// Setup loop
		// Move the address from I into register eax (= starting address of memory array + offset from I)
		emitter->MOV_ImmtoR_32(eax, (uint32_t)C8_STATE::memory);
		emitter->ADD_MtoR_16(ax, &C8_STATE::cpu.I);
		// Move address of V[0] into edx register
		emitter->MOV_ImmtoR_32(edx, (uint32_t)C8_STATE::cpu.V);
		// Start loop
		emitter->MOV_PTRtoR_8(cl, eax); // Move 8bit value from PTR @ eax (c8 memory + I register + loop number) into cl register
		emitter->MOV_RtoPTR_8(edx, cl); // Move 8bit value from cl register into PTR @ edx (c8 V address + loop number)
		// Add one to each PTR to increment address
		emitter->ADD_ImmtoR_8(eax, 1);
		emitter->ADD_ImmtoR_8(edx, 1);
		// Compare edx with final V address (to stop loop)
		emitter->CMP_RwithImm_32(edx, (uint32_t)(C8_STATE::cpu.V + vx + 1)); // "less than" compare
		emitter->JNE_8(-18); // jump to start of loop -(2+6+3+3+2+2) = -18

		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);

		// Increment PC
		C8_STATE::C8_incrementPC();
		break;
	}
	default:
	{
#ifdef USE_VERBOSE
		char buffer[1000];
		sprintf_s(buffer, 1000, "Unknown Opcode detected! Skipping (0x%.4X).", C8_STATE::opcode);
		logMessage(LOGLEVEL::L_WARNING, buffer);
#endif
		// Set region pc to current c8 pc
		cache->setCacheEndC8PCCurrent(C8_STATE::cpu.pc);
		C8_STATE::C8_incrementPC(); // Update PC by 2 bytes
		break;
	}
	}
}