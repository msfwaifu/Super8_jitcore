#include "stdafx.h"

#include <cstdint>
#include <Windows.h>

#include "Headers\Globals.h"

#include "Headers\Chip8Globals\MainEngineGlobals.h"
#include "Headers\Chip8Globals\X86_STATE.h"
#include "Headers\Chip8Globals\C8_STATE.h"
#include "Headers\Chip8Engine\CacheHandler.h"
#include "Headers\Chip8Engine\JumpHandler.h"

using namespace Chip8Globals;
using namespace Chip8Globals::MainEngineGlobals;
using namespace Chip8Engine;

CacheHandler::CacheHandler()
{
	// Initialise values.
	// Cache list variables.
	selected_cache_index = -1;
	cache_list = new std::vector<CACHE_REGION>();
	cache_list->reserve(1024); // Do not remove this! This reserve is necessary so that the vector doesnt 'grow' and copy the caches somewhere else.

	// Setup CDECL cache variables.
	setup_cache_cdecl = nullptr;
	setup_cache_cdecl_return_address = nullptr;
	setup_cache_cdecl_eip_hack = nullptr;

	// Register this component in logger
	logger->registerComponent(this);
}

CacheHandler::~CacheHandler()
{
	// Deregister this component in logger
	logger->deregisterComponent(this);

	deallocAllCacheExit();
	delete cache_list;
}

std::string CacheHandler::getComponentName()
{
	return std::string("CacheHandler");
}

void CacheHandler::setupCache_CDECL()
{
	// A small cache which is used to handle the CDECL call convention before passing off to the main cache execution point.
	// Also contains the x86 EIP hack used to get the current EIP address and store it in the eax register.
	if (setup_cache_cdecl == NULL) {
		// Alloc cdecl setup cache for first time. Will not change after this.
		uint8_t setup_cache_cdecl_sz;
		uint8_t	setup_cache_cdecl_bytes[] = {
			// Below code is used to 1. start CDECL calling convention (create new stack frame), 2. goto emulation resume point, then 3. cleanup stack frame (return point).
			// 1.
			0x55,					//0x0 PUSH ebp
			0x89,					//0x1 (1) MOV ebp, esp
			0b11100101,				//0x2 (2, MODRM) MOV ebp, esp

			// 2.
			0xFF,					//0x3 (1) JMP r/m32
			0b00100101,				//0x4 (2, MODRM) JMP r/m32
			0x00,					//0x5 (3, DISP32) JMP r/m32
			0x00,					//0x6 (4, DISP32) JMP r/m32
			0x00,					//0x7 (5, DISP32) JMP r/m32
			0x00,					//0x8 (6, DISP32) JMP r/m32

			// 3.
			0x5D,					//0x9 POP ebp
			0xC3,					//0xA RET

			// HACK: ASM BELOW USED TO GET EIP ADDRESS AND RETURN IN EAX. SEE CodeEmitter_x86->DYNAREC_MOV_EAX_EIP.
			0x58,					//0xB POP eax
			0x50,					//0xC PUSH eax
			0xC3					//0xD RET
		};
		setup_cache_cdecl_sz = sizeof(setup_cache_cdecl_bytes) / sizeof(setup_cache_cdecl_bytes[0]);

		// WIN32 specific. Allocate cache memory with read, write and execute permissions.
#ifdef _WIN32
		setup_cache_cdecl = (uint8_t *)VirtualAlloc(0, setup_cache_cdecl_sz, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#endif

		// Check if memory was actually allocated.
		if (setup_cache_cdecl == NULL) {
			char buffer[1000];
			sprintf_s(buffer, 1000, "Could not allocate memory for a cache. Exiting.");
			logMessage(LOGLEVEL::L_FATAL, buffer);
			exit(2);
		}

		// Copy above raw x86 code into executable memory page.
		memcpy(setup_cache_cdecl, setup_cache_cdecl_bytes, setup_cache_cdecl_sz);

		// Update variables needed throughout program.
		setup_cache_cdecl_return_address = (setup_cache_cdecl + 0x9);
		setup_cache_cdecl_eip_hack = (setup_cache_cdecl + 0xB);

		// Update cdecl cache with location of x86_resume_address variable (will jump to address contained in x86_resume_address).
		*(uint32_t *)(setup_cache_cdecl + 0x5) = (uint32_t)&X86_STATE::x86_resume_address;

		// DEBUG
#ifdef USE_VERBOSE
		char buffer[1000];
		sprintf_s(buffer, 1000, "CDECL Cache allocated. Location and size: 0x%.8X, %d.", (uint32_t)setup_cache_cdecl, setup_cache_cdecl_sz);
		logMessage(LOGLEVEL::L_INFO, buffer);
		sprintf_s(buffer, 1000, " setup_cache_return_jmp_address @ location 0x%.8X.", (uint32_t)&setup_cache_return_jmp_address);
		logMessage(LOGLEVEL::L_INFO, buffer);
		sprintf_s(buffer, 1000, " setup_cache_eip_hack @ location 0x%.8X.", (uint32_t)&setup_cache_eip_hack);
		logMessage(LOGLEVEL::L_INFO, buffer);
		sprintf_s(buffer, 1000, " x86_resume_address @ location 0x%.8X.", (uint32_t)&X86_STATE::x86_resume_address);
		logMessage(LOGLEVEL::L_INFO, buffer);
#endif
	}
}

void CacheHandler::execCache_CDECL()
{
	// Old method, doesnt work with optimisations turned on (optimises to JMP instead of CALL).
	//void(__cdecl *exec)() = (void(__cdecl *)())setup_cache_cdecl;
	//exec();

	// New method, works with optimisations turned on. TODO: Look at why we cant direcly place value of setup_cdecl_cache into eax and call.. seems to put 14h instead of address.. probably something to do with stack.
	// CDECL calling convention, but there are no variables to push onto stack/remove from stack by changing esp.
	// TODO: not sure if this works outside of the MS compiler (__asm tag)
	uint32_t call_address = (uint32_t)&setup_cache_cdecl;
	__asm {
		mov eax, call_address
		call [eax]
	};
}

void CacheHandler::initFirstCache()
{
	allocAndSwitchNewCacheByC8PC(C8_STATE::cpu.pc);
	X86_STATE::x86_resume_address = getCacheInfoCurrent()->x86_mem_address;
}

int32_t CacheHandler::findCacheIndexByC8PC(uint16_t c8_pc_)
{
	int32_t index = -1;
	for (int32_t i = 0; i < (int32_t)cache_list->size(); i++) {
		if (c8_pc_ >= cache_list->at(i).c8_start_recompile_pc
			&& c8_pc_ <= cache_list->at(i).c8_end_recompile_pc
			&& cache_list->at(i).invalid_flag == 0) {
			index = i;
			break;
		}
	}
	return index;
}

int32_t CacheHandler::findCacheIndexByStartC8PC(uint16_t c8_pc_)
{
	int32_t index = -1;
	for (int32_t i = 0; i < (int32_t)cache_list->size(); i++) {
		if (c8_pc_ == cache_list->at(i).c8_start_recompile_pc
			&& cache_list->at(i).invalid_flag == 0) { // dont need to check for pc_alignement as its already defined to be aligned by checking against the starting pc
			index = i;
			break;
		}
	}
	return index;
}

int32_t CacheHandler::findCacheIndexByX86Address(uint8_t * x86_address)
{
	for (int32_t i = 0; i < (int32_t)cache_list->size(); i++) {
		uint8_t * x86_end = cache_list->at(i).x86_mem_address + cache_list->at(i).x86_pc;
		if (x86_address >= cache_list->at(i).x86_mem_address && x86_address <= x86_end) {
			return i;
		}
	}
	return -1;
}

int32_t CacheHandler::allocNewCacheByC8PC(uint16_t c8_start_pc_)
{
	// Attempt to allocate memory for cache first.
	uint8_t * cache_mem = NULL;

	// WIN32 specific. Allocate cache memory with read, write and execute permissions.
#ifdef _WIN32
	if ((cache_mem = (uint8_t *)VirtualAlloc(0, MAX_CACHE_SZ, MEM_COMMIT, PAGE_EXECUTE_READWRITE)) == NULL) exit(2);
#endif

	// Check if memory was actually allocated.
	if (setup_cache_cdecl == NULL) {
		char buffer[1000];
		sprintf_s(buffer, 1000, "Could not allocate memory for a cache. Exiting.");
		logMessage(LOGLEVEL::L_FATAL, buffer);
		exit(2);
	}

	// Set cache to NOP 0x90. Allows for OUT_OF_CODE to be reached without changing anything.
	memset(cache_mem, 0x90, MAX_CACHE_SZ);

	// set last memory bytes to OUT_OF_CODE interrupt
	// Emits change x86_status_code to 2 (out of code) & x86_interrupt_x86_param1 = cache_mem address then jump back to cdecl return address
	uint8_t bytes[] = {
		0xC6,		// (0) MOV m, Imm8
		0b00000101, // (1) MOV m, Imm8
		0x00,		// (2) PTR 32
		0x00,		// (3) PTR 32
		0x00,		// (4) PTR 32
		0x00,		// (5) PTR 32
		0x02,		// (6) X86_STATUS_CODE = 2 (OUT_OF_CODE)
		//-----------------------------------------------------
		0xC7,		// (7) MOV m, Imm32
		0b00000101, // (8) MOV m, Imm32
		0x00,		// (9) PTR 32
		0x00,		// (10) PTR 32
		0x00,		// (11) PTR 32
		0x00,		// (12) PTR 32
		0x00,		// (13) cache_mem address
		0x00,		// (14) cache_mem address
		0x00,		// (15) cache_mem address
		0x00,		// (16) cache_mem address
		//-----------------------------------------------------
		0xFF,		// (17) JMP PTR 32
		0b00100101, // (18) JMP PTR 32
		0x00,		// (19) PTR 32
		0x00,		// (20) PTR 32
		0x00,		// (21) PTR 32
		0x00		// (22) PTR 32
	};
	uint32_t x86_status_code_address = (uint32_t)&(X86_STATE::x86_interrupt_status_code);
	uint32_t x86_param1_address = (uint32_t)&(X86_STATE::x86_interrupt_x86_param1);
	uint32_t cdecl_return_address = (uint32_t)&setup_cache_cdecl_return_address;
	*((uint32_t*)(bytes + 2)) = x86_status_code_address;
	*((uint32_t*)(bytes + 9)) = x86_param1_address;
	*((uint32_t*)(bytes + 13)) = (uint32_t)cache_mem;
	*((uint32_t*)(bytes + 19)) = cdecl_return_address;
	uint8_t sz = sizeof(bytes) / sizeof(bytes[0]);
	memcpy(cache_mem + MAX_CACHE_SZ - sz, bytes, sz); // Write this to last bytes of cache

	// cache end pc is unknown at allocation, so set to start pc too
	CACHE_REGION memoryblock = { c8_start_pc_, c8_start_pc_, cache_mem, 0, 0 };
	cache_list->push_back(memoryblock);

	// DEBUG
#ifdef USE_VERBOSE
	char buffer[1000];
	sprintf_s(buffer, 1000, "Cache[%d] allocated. Location and size: %p, %d, C8 Start PC = 0x%.4X.", cache_list->size() - 1, cache_mem, MAX_CACHE_SZ, c8_start_pc_);
	logMessage(LOGLEVEL::L_INFO, buffer);
#endif
	return (cache_list->size() - 1);
}

int32_t CacheHandler::getCacheWritableByStartC8PC(uint16_t c8_jump_pc)
{
	int32_t index = findCacheIndexByStartC8PC(c8_jump_pc);
	if (index == -1) {
		// No cache was found at all, so allocate a completely new cache
		index = allocNewCacheByC8PC(c8_jump_pc);
#ifdef USE_DEBUG
		char buffer[1000];
		sprintf_s(buffer, 1000, "Jump Cache Path Result = NEW CACHE(%d).", index);
		logMessage(LOGLEVEL::L_DEBUG, buffer);
#endif
	}
	else {
		// dont need to allocate/invalidate anything here, as jump will be to the start C8 PC requested
#ifdef USE_DEBUG
		char buffer[1000];
		sprintf_s(buffer, 1000, "Jump Cache Path Result = FOUND CACHE(%d).", index);
		logMessage(LOGLEVEL::L_DEBUG, buffer);
#endif
	}
	return index;
}

int32_t CacheHandler::allocAndSwitchNewCacheByC8PC(uint16_t c8_start_pc_)
{
	uint32_t index = allocNewCacheByC8PC(c8_start_pc_);
	switchCacheByIndex(index);
	return index;
}

void CacheHandler::deallocAllCacheExit()
{
	for (int32_t i = 0; i < (int32_t)cache_list->size(); i++) {
#ifdef USE_VERBOSE
		char buffer[1000];
		sprintf_s(buffer, 1000, "Cache[%d] invalidated. C8 Start PC = 0x%.4X, C8 End PC = 0x%.4X.", i, cache_list->at(i).c8_start_recompile_pc, cache_list->at(i).c8_end_recompile_pc);
		logMessage(LOGLEVEL::L_INFO, buffer);
#endif
		VirtualFree(cache_list->at(i).x86_mem_address, 0, MEM_RELEASE);
	}
}

void CacheHandler::invalidateCacheByFlag()
{
	for (int32_t i = 0; i < (int32_t)cache_list->size(); i++) {
		if (cache_list->at(i).invalid_flag == 1) {
			if (!(X86_STATE::x86_resume_address >= cache_list->at(i).x86_mem_address 
				&& X86_STATE::x86_resume_address <= (cache_list->at(i).x86_mem_address + cache_list->at(i).x86_pc))) { // check to make sure the resume address is not currently inside this cache
				
				// First remove any jump references to this cache
				jumphandler->clearFilledFlagByC8PC(cache_list->at(i).c8_start_recompile_pc);

				// Delete cache here
#ifdef USE_VERBOSE
				char buffer[1000];
				sprintf_s(buffer, 1000, "Cache[%d] invalidated. C8 Start PC = 0x%.4X, C8 End PC = 0x%.4X.", i, cache_list->at(i).c8_start_recompile_pc, cache_list->at(i).c8_end_recompile_pc);
				logMessage(LOGLEVEL::L_INFO, buffer);
#endif
				VirtualFree(cache_list->at(i).x86_mem_address, 0, MEM_RELEASE);
				cache_list->erase(cache_list->begin() + i);

				// Handle selected_cache_index changes
				if (selected_cache_index > i) {
					// Need to decrease selected_cache by 1
					selected_cache_index -= 1;
				}
				//else if (selected_cache_index < i) {
				// Do nothing, we are ok
				//}
				else if (selected_cache_index == i) {
					// Set to -1 (need to reselect later)
					selected_cache_index = -1;
				}
#ifdef USE_VERBOSE
				sprintf_s(buffer, 1000, " New selected_cache_index = %d.", selected_cache_index);
				logMessage(LOGLEVEL::L_INFO, buffer);
#endif
				i -= 1; // decrease i by 1 so it rechecks the current i'th value in the list (which would have been i+1 if there was no remove).
			}
		}
	}
}

void CacheHandler::setInvalidFlagByIndex(int32_t index)
{
	(*cache_list)[index].invalid_flag = 1;
}

void CacheHandler::setInvalidFlagByC8PC(uint16_t c8_pc_)
{
	for (int32_t i = 0; i < (int32_t)cache_list->size(); i++) {
		if (c8_pc_ >= cache_list->at(i).c8_start_recompile_pc
			&& c8_pc_ <= cache_list->at(i).c8_end_recompile_pc
			&& cache_list->at(i).invalid_flag == 0) {
			setInvalidFlagByIndex(i);
		}
	}
}

uint8_t CacheHandler::getInvalidFlagByIndex(int32_t index)
{
	return (*cache_list)[index].invalid_flag;
}

void CacheHandler::switchCacheByC8PC(uint16_t c8_pc_)
{
	for (int32_t i = 0; i < (int32_t)cache_list->size(); i++) {
		if (c8_pc_ >= cache_list->at(i).c8_start_recompile_pc
			&& c8_pc_ <= cache_list->at(i).c8_end_recompile_pc
			&& cache_list->at(i).invalid_flag == 0) {
			selected_cache_index = i;
			// set C8 pc to end of memory region
			C8_STATE::cpu.pc = cache_list->at(i).c8_end_recompile_pc;
			break;
		}
	}
}

int32_t CacheHandler::findCacheIndexCurrent()
{
	return selected_cache_index;
}

void CacheHandler::switchCacheByIndex(int32_t index)
{
#ifdef USE_DEBUG_EXTRA
	char buffer[1000];
	sprintf_s(buffer, 1000, "Old s_c_i = cache[%d]. New s_c_i = cache[%d]", selected_cache_index, index);
	logMessage(LOGLEVEL::L_INFO, buffer);
#endif
	selected_cache_index = index;
}

void CacheHandler::setCacheEndC8PCCurrent(uint16_t c8_end_pc_)
{
	if ((*cache_list)[selected_cache_index].c8_start_recompile_pc == 0xFFFF) (*cache_list)[selected_cache_index].c8_start_recompile_pc = c8_end_pc_;
	(*cache_list)[selected_cache_index].c8_end_recompile_pc = c8_end_pc_;
}

void CacheHandler::setCacheEndC8PCByIndex(int32_t index, uint16_t c8_end_pc_)
{
	if ((*cache_list)[selected_cache_index].c8_start_recompile_pc == 0xFFFF) (*cache_list)[selected_cache_index].c8_start_recompile_pc = c8_end_pc_;
	(*cache_list)[selected_cache_index].c8_end_recompile_pc = c8_end_pc_;
}

uint16_t CacheHandler::getEndC8PCCurrent()
{
	return (*cache_list)[selected_cache_index].c8_end_recompile_pc;
}

uint8_t * CacheHandler::getEndX86AddressCurrent()
{
	uint8_t* cache_mem_current = (*cache_list)[selected_cache_index].x86_mem_address + (*cache_list)[selected_cache_index].x86_pc;
	return cache_mem_current;
}

CACHE_REGION * CacheHandler::getCacheInfoCurrent()
{
	return &(*cache_list)[selected_cache_index];
}

CACHE_REGION * CacheHandler::getCacheInfoByIndex(int32_t index)
{
	return &(*cache_list)[index];
}

#ifdef USE_DEBUG_EXTRA
void Chip8Engine_CacheHandler::DEBUG_printCacheByIndex(int32_t index)
{
	char buffer[1000];
	sprintf_s(buffer, 1000, "Cache[%d]: C8_start_pc = 0x%.4X, C8_end_pc = 0x%.4X, X86_mem_address = 0x%.8X, X86_pc = 0x%.8X.", index, (*cache_list)[index].c8_start_recompile_pc, (*cache_list)[index].c8_end_recompile_pc, (uint32_t)(*cache_list)[index].x86_mem_address, (*cache_list)[index].x86_pc);
	logMessage(LOGLEVEL::L_DEBUG, buffer);
	sprintf_s(buffer, 1000, " invalid_flag = %d.", (*cache_list)[index].invalid_flag);
	logMessage(LOGLEVEL::L_DEBUG, buffer);
}

void Chip8Engine_CacheHandler::DEBUG_printCacheList()
{
	for (int32_t i = 0; i < (int32_t)cache_list->size(); i++) {
		char buffer[1000];
		sprintf_s(buffer, 1000, "Cache[%d]: C8_start_pc = 0x%.4X, C8_end_pc = 0x%.4X, X86_mem_address = 0x%.8X, X86_pc = 0x%.8X.", i, cache_list->at(i).c8_start_recompile_pc, cache_list->at(i).c8_end_recompile_pc, (uint32_t)cache_list->at(i).x86_mem_address, cache_list->at(i).x86_pc);
		logMessage(LOGLEVEL::L_DEBUG, buffer);
		sprintf_s(buffer, 1000, " invalid_flag = %d.", cache_list->at(i).invalid_flag);
		logMessage(LOGLEVEL::L_DEBUG, buffer);
	}
}
#endif

void CacheHandler::incrementCacheX86PC(uint8_t count)
{
	(*cache_list)[selected_cache_index].x86_pc += count;
}

void CacheHandler::write8(uint8_t byte_)
{
	*((*cache_list)[selected_cache_index].x86_mem_address + (*cache_list)[selected_cache_index].x86_pc) = byte_;
#ifdef USE_DEBUG_EXTRA
	char buffer[1000];
	sprintf_s(buffer, 1000, "Byte written: cache[%d] @ %.8X and value: 0x%.2X.", selected_cache_index, (*cache_list)[selected_cache_index].x86_pc, byte_);
	logMessage(LOGLEVEL::L_DEBUG, buffer);
#endif 
	incrementCacheX86PC(1); // 1 byte
}

void CacheHandler::write16(uint16_t word_)
{
	uint8_t* cache_mem_current = (*cache_list)[selected_cache_index].x86_mem_address + (*cache_list)[selected_cache_index].x86_pc;
	*((uint16_t*)cache_mem_current) = word_;
#ifdef USE_DEBUG_EXTRA
	char buffer[1000];
	sprintf_s(buffer, 1000, "Word written: cache[%d] @ %.8X and value: 0x%.4X.", selected_cache_index, (*cache_list)[selected_cache_index].x86_pc, word_);
	logMessage(LOGLEVEL::L_DEBUG, buffer);
#endif
	incrementCacheX86PC(2); // 2 bytes
}

void CacheHandler::write32(uint32_t dword_)
{
	uint8_t* cache_mem_current = (*cache_list)[selected_cache_index].x86_mem_address + (*cache_list)[selected_cache_index].x86_pc;
	*((uint32_t*)cache_mem_current) = dword_;
#ifdef USE_DEBUG_EXTRA
	char buffer[1000];
	sprintf_s(buffer, 1000, "Dword written: cache[%d] @ %.8X and value: 0x%.8X.", selected_cache_index, (*cache_list)[selected_cache_index].x86_pc, dword_);
	logMessage(LOGLEVEL::L_DEBUG, buffer);
#endif
	incrementCacheX86PC(4); // 4 bytes
}