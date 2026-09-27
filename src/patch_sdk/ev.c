/********************************************************************************************************
 * @file    ev.c
 *
 * @brief   Patched event dispatcher with diagnostic logging for TLSR8258.
 *******************************************************************************************************/

#include "tl_common.h"
#include "app_config.h"
#include "proj/os/ev.h"
#include "debug_uart.h"

sys_exception_cb_t g_sysExceptCallbak = NULL;

volatile u16 T_evtExcept[4] = {0};

u8 sys_exceptionPost(u16 line, u8 evt)
{
	T_evtExcept[0] = line;
	T_evtExcept[1] = evt;
    DEBUG_LOG("EXCEPT", "sys_exceptionPost: line=%u, evt=0x%02X", (unsigned int)line, (unsigned int)evt);
    debug_uart_flush();

	if(g_sysExceptCallbak){
		g_sysExceptCallbak();
	}

	return 0;
}

void sys_stackStatusCheck(void)
{
	extern u32 _end_bss_;
	u8 *stackEnd = (u8*)&_end_bss_;
	u8 stackOverflown = 0;
	for(s32 i = 0; i < 4; i++){
		if(stackEnd[i] != 0xff){
			stackOverflown = 1;
			break;
		}
	}

	if(stackOverflown){
		ZB_EXCEPTION_POST(SYS_EXCEPTTION_COMMON_STACK_OVERFLOWN);
	}
}

void sys_exceptHandlerRegister(sys_exception_cb_t cb)
{
	g_sysExceptCallbak = cb;
}

void ev_main(void)
{
	ev_timer_process(0);
	ev_poll_process();
}
