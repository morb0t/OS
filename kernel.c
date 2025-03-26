#include <stdlib.h>
#include "kernel.h"
#include "list.h"
#include <stdio.h>

#ifndef NULL
#define NULL 0
#endif


/*****************************************************************************
 * Global variables
 *****************************************************************************/

static uint32_t id=1;
Task * tsk_running = NULL;	/* pointer to ready task list : the first
                                     node is the running task descriptor */
Task * tsk_prev = NULL;
Task * tsk_sleeping = NULL;	/* pointer to sleeping task list */
/*****************************************************************************
 * SVC dispatch
 *****************************************************************************/
/* sys_add
 *   test function
 */
int sys_add(int a, int b)
{
    return a+b;
}

/* syscall_dispatch
 *   dispatch syscalls
 *   n      : syscall number
 *   args[] : array of the parameters (4 max)
 */
int32_t svc_dispatch(uint32_t n, uint32_t args[])
{
	int32_t result=-1;

    switch(n) {
      case 0:
          result = sys_add((int)args[0], (int)args[1]);
          break;
      case 1:
    	  result = (void *)malloc(args[0]);
    	  break;
      case 2:
    	  free((void*)args[0]);
          break;
      case 3:
    	  result = sys_os_start();
          break;
      case 4:
    	  result = sys_task_new((TaskCode)args[0], (uint32_t)args[1]);
    	  break;
      case 5:
    	  result = sys_task_id();
          break;
      case 6:
    	  result = sys_task_wait((uint32_t)args[0]);
          break;
      case 7:
    	  result = sys_task_kill();
          break;
      case 8:
    	  result = sys_sem_new((int32_t)args[0]);
    	  break;
      case 9:
    	  result = sys_sem_p((Semaphore*)args[0]);
          break;
      case 10:
    	  result = sys_sem_v((Semaphore*)args[0]);
          break;

    }
    return result;
}

void sys_switch_ctx()
{
	SCB->ICSR |= 1<<28; // set PendSV to pending
}
/*****************************************************************************
 * Round robin algorithm
 *****************************************************************************/
#define SYS_TICK  10	// system tick in ms

uint32_t sys_tick_cnt=0;

/* tick_cb
 *   system tick callback: task switching, ...
 */
void sys_tick_cb()
{
	 	 if (tsk_running == NULL) {
	        return;
	    }

	 	tsk_running->status = TASK_SLEEPING;
	 	tsk_prev = tsk_running;
	    tsk_running = tsk_prev->next;
	    tsk_running->status = TASK_RUNNING;
	    sys_switch_ctx();

//	    Task *temp=tsk_sleeping;
//	    do{
//	    	temp->delay -= SYS_TICK;
//	    	if(temp->delay <= 0){
//	    		Task* tsk;
//	    		temp->delay = 0;
//	    		tsk_sleeping = list_remove_head(tsk_sleeping, &tsk);
//	    		tsk->status = TASK_READY;
//	    		tsk_running = list_insert_tail(tsk_running, tsk);
//	    	}
//	    	temp = temp->next;
//	    }while(temp!=tsk_sleeping);
	    for(int i=0; i<list_size(tsk_sleeping); i++){
	    	tsk_sleeping->delay -= SYS_TICK;
	    	if(tsk_sleeping->delay <=0){
	    		Task* tsk;
	    		tsk_sleeping->delay = 0;
	    		tsk_sleeping = list_remove_head(tsk_sleeping, &tsk);
	    		tsk->status = TASK_READY;
	    		tsk_running = list_insert_tail(tsk_running, tsk);
	    	}else{
	    		tsk_sleeping=tsk_sleeping->next;
	    	}

	    }

	    // Switch context
	    sys_switch_ctx();
}

void SysTick_Handler(void)
{
	sys_tick_cnt++;

	if (sys_tick_cnt == SYS_TICK) {
		sys_tick_cnt = 0;
		sys_tick_cb();
	}
}

/*****************************************************************************
 * General OS handling functions
 *****************************************************************************/

/* sys_os_start
 *   start the first created task
 */
int32_t sys_os_start()
{
	/* A COMPLETER */
	if(!tsk_running){
		return -1;
	}

    // Reset BASEPRI
    __set_BASEPRI(0);

    __set_PSP((uint32_t)tsk_running->sp);

    __set_CONTROL(__get_CONTROL() | 0x03);

    NVIC_SetPriority(PendSV_IRQn, 255);
    NVIC_SetPriority(SysTick_IRQn, 1);


    sys_switch_ctx();


	// Set systick reload value to generate 1ms interrupt
    SysTick_Config(SystemCoreClock / 1000U);
    return 0;
}

/*****************************************************************************
 * Task handling functions
 *****************************************************************************/
void task_kill();

/* sys_task_new
 *   create a new task :
 *   func      : task code to be run
 *   stacksize : task stack size
 *
 *   Stack frame:
 *      |    xPSR    |
 *      |     PC     |
 *      |     LR     |
 *      |     R12    |    ^
 *      |     R3     |    ^
 *      |     R2     |    | @++
 *      |     R1     |
 *      |     R0     |
 *      +------------+
 *      |     R11    |
 *      |     R10    |
 *      |     R9     |
 *      |     R8     |
 *      |     R7     |
 *      |     R6     |
 *      |     R5     |
 *      |     R4     |
 *      +------------+
 *      | EXC_RETURN |
 *      |   CONTROL  | <- sp
 *      +------------+
 */
int32_t sys_task_new(TaskCode func, uint32_t stacksize)
{
	Task* task;
	// get a stack with size multiple of 8 bytes
	uint32_t size = stacksize>96 ? 8*(((stacksize-1)/8)+1) : 96;
	
	task = (Task*)malloc(sizeof(Task) + size);

	if(!task){
	    return -1;
	}

	task->splim = (uint32_t*)(task+sizeof(task));
	task->sp = (uint32_t*)(task->splim + size);

	task->id=id++;
	task->delay = 0;
	task->status = TASK_READY;

	tsk_running=list_insert_tail(tsk_running, task);

	// initialisation
	*(task->sp) = (uint32_t)(1<<24); // xPSR, mise a 1 du bit T
	*(--task->sp) = (uint32_t)func; // PC <= @func
	*(--task->sp) = (uint32_t)task_kill; // LR <= valeur quelconque
	*(--task->sp) = 0x00000000; // R12
	*(--task->sp) = 0x00000000; // R3
	*(--task->sp) = 0x00000000; // R2
	*(--task->sp) = 0x00000000; // R1
	*(--task->sp) = 0x00000000; // R0
	*(--task->sp) = 0x00000000; // R11
	*(--task->sp) = 0x00000000; // R10
	*(--task->sp) = 0x00000000; // R9
	*(--task->sp) = 0x00000000; // R8
	*(--task->sp) = 0x00000000; // R7
	*(--task->sp) = 0x00000000; // R6
	*(--task->sp) = 0x00000000; // R5
	*(--task->sp) = 0x00000000; // R4
	*(--task->sp) = 0xFFFFFFFD; // EXC_RETURN Mode PSP
	*(--task->sp) = 0x03; // CONTROL


	return task->id;

}

/* sys_task_kill
 *   kill oneself
 */
int32_t sys_task_kill()
{
	if(!tsk_running){
		return -1;
	}
	Task* tsk;
	tsk_running = list_remove_head(tsk_running, &tsk);
	tsk_running->status=TASK_RUNNING;
	sys_switch_ctx();
	free(tsk);

	return 0;
}

/* sys_task_id
 *   returns id of task
 */
int32_t sys_task_id()
{

    return tsk_running->id;
}


/* sys_task_yield
 *   run scheduler to switch to another task
 */
int32_t sys_task_yield()
{

    return -1;
}

/* task_wait
 *   suspend the current task until timeout
 */
int32_t sys_task_wait(uint32_t ms)
{
	if(!tsk_running){
	    return -1;
	}

	Task *tsk;
	tsk_running->delay = ms;
	tsk_prev=tsk_running;
	tsk_running = list_remove_head(tsk_running, &tsk);
	tsk->status = TASK_SLEEPING;
	tsk_sleeping = list_insert_tail(tsk_sleeping, tsk);

	sys_switch_ctx();


}


/*****************************************************************************
 * Semaphore handling functions
 *****************************************************************************/

/* sys_sem_new
 *   create a semaphore
 *   init    : initial value
 */
Semaphore * sys_sem_new(int32_t init)
{
	Semaphore *newSem;
	newSem = (Semaphore*)malloc(sizeof(Semaphore));

	if(newSem == NULL){
		return NULL;
	}

	newSem->count = init;
	newSem->waiting = NULL;

	return newSem;
}

/* sys_sem_p
 *   take a token
 */
int32_t sys_sem_p(Semaphore * sem)
{
	Task* tsk;
	if(sem == NULL || tsk_running == NULL){
		return -1;
	}
	sem->count--;
	if(sem->count < 0){
		tsk_prev = tsk_running;
		tsk_running = list_remove_head(tsk_running, &tsk);
		tsk->status = TASK_WAITING;
		sem->waiting = list_insert_tail(sem->waiting, tsk);
		sys_switch_ctx();
	}


	return 0;

}

/* sys_sem_v
 *   release a token
 */
int32_t sys_sem_v(Semaphore * sem)
{
	Task* tsk;
	if(sem == NULL){
		return -1;
	}
	sem->count = sem->count + 1;
	if(sem->waiting != NULL){
		tsk_prev=tsk_running;
		sem->waiting = list_remove_head(sem->waiting, &tsk);
		tsk->status = TASK_READY;
		tsk_running = list_insert_tail(tsk_running, tsk);
		sys_switch_ctx();
	}


	return 0;
}
