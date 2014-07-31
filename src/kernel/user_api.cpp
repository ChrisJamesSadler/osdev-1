#include "user_api.hpp"
#include "../include/btos_api.h"
#include "locks.hpp"

void userapi_handler(int, isr_regs*);
void userapi_syscall(uint16_t fn, isr_regs *regs);

#define USERAPI_HANDLE_CALL(name) \
 	case btos_api::name:\
 		name ## _call(regs);\
 		break;

#define USERAPI_HANDLER(name) static void name ## _call(isr_regs *regs)

void userapi_init(){
	dbgpf("UAPI: Init");
	int_handle(0x80, &userapi_handler);
}

void userapi_handler(int, isr_regs *regs){
	uint16_t *id=(uint16_t*)(&regs->eax);
	uint16_t ext=id[1], fn=id[0];
	dbgpf("UAPI: Extension: %x, Function: %x\n", (int)ext, (int)fn);
	if(ext==0){
		userapi_syscall(fn, regs);
	}else{
		regs->eax=-1;
		return;
	}
}

static bool is_safe_ptr(uint32_t ptr){
	return ptr>=VMM_USERSPACE_START;
}

USERAPI_HANDLER(BT_ALLOC_PAGES){
	regs->eax = (uint32_t)vmm_alloc(regs->ebx, false);
}

USERAPI_HANDLER(BT_FREE_PAGES){
	if(is_safe_ptr(regs->eax)){
		vmm_free((void*)regs->ebx, regs->ecx);
	}
}

USERAPI_HANDLER(BT_CREATE_LOCK){
	if(is_safe_ptr(regs->eax)){
		init_lock(*(lock*)regs->eax);
	}
}

USERAPI_HANDLER(BT_LOCK){
	if(is_safe_ptr(regs->eax)){
		lock(*(lock*)regs->eax);
	}
}

USERAPI_HANDLER(BT_TRY_LOCK){
	if(is_safe_ptr(regs->eax)){
    	regs->eax=try_lock(*(lock*)regs->eax);
    }
}



void userapi_syscall(uint16_t fn, isr_regs *regs){
	switch(fn){
		USERAPI_HANDLE_CALL(BT_ALLOC_PAGES);
		USERAPI_HANDLE_CALL(BT_FREE_PAGES);

		USERAPI_HANDLE_CALL(BT_CREATE_LOCK);
		USERAPI_HANDLE_CALL(BT_LOCK);

		default:
			regs->eax=-1;
			break;
	}
}
