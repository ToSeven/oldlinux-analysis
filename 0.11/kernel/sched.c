/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sys.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <signal.h>

// 该宏取信号 nr 在信号位图中对应位的二进制数值。信号编号 1-32。
// 例如，信号 5 的位图数值等于 1<<(5-1) = 16 = 00010000b。
// 另外，除了 SIGKILL 和 SIGSTOP 信号以外其他信号都是可阻塞的(...1011,1111,1110,1111,1111b)
#define _S(nr) (1<<((nr)-1))
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

// 内核调试函数。显示任务号 nr 的进程号、进程状态、内核堆栈空闲字节数(大约)及其相关的 
// 子进程和父进程信息。
// 因为任务结构的数据和任务的内核态栈在同一内存页面上，且任务内核态栈从页面末端开始， // 因此，28 行上的 j 即表示最大内核栈容量，或内核栈最低顶端位置。
// 参数:
// nr-任务号; p-任务结构指针。
void show_task(int nr,struct task_struct * p)
{
	int i,j = 4096-sizeof(struct task_struct);

	printk("%d: pid=%d, state=%d, ",nr,p->pid,p->state);
	i=0;
	while (i<j && !((char *)(p+1))[i])
		i++;
	printk("%d (of %d) chars free in kernel stack\n\r",i,j);
}

// 显示系统中所有任务的状态信息。
// NR_TASKS 是系统能容纳的最大任务数量(64 个)，定义在 include/linux/sched.h 第 6 行。
void show_stat(void)
{
	int i;

	for (i=0;i<NR_TASKS;i++)
		if (task[i])
			show_task(i,task[i]);
}

// PC 机 8253 计数/定时芯片的输入时钟频率约为 1.193180MHz。Linux 内核希望定时器中断频率是
// 100Hz，也即每 10ms 发出一次时钟中断。因此这里 LATCH 是设置 8253 芯片的初值，参见 438 行。
#define LATCH (1193180/HZ)

extern void mem_use(void);  //无用函数，没有任何定义和引用

extern int timer_interrupt(void);  //定时中断函数
extern int system_call(void);      //系统调用中断程序

// 每个任务(进程)在内核态运行时都有自己的内核态堆栈。这里定义了任务的内核态堆栈结构。 // 这里定义任务联合(任务结构成员和 stack 字符数组成员)。因为一个任务的数据结构与其内核 // 态堆栈放在同一内存页中，所以从堆栈段寄存器 ss 可以获得其数据段选择符。
// 67 行上设置初始任务的数据。初始数据在 include/linux/sched.h 中，第 156 行开始。
 
union task_union {
	struct task_struct task;
	char stack[PAGE_SIZE];
};

static union task_union init_task = {INIT_TASK,};

// 从开机算起的滴答数(10ms/滴答)。系统时钟中断每发生一次即一个滴答。
// 前面的限定符 volatile，英文解释意思是易变的、不稳定的。这个限定词的含义是向编译器指明变量的内容可能会因被其他程序修改而变化。通常在程序中申明一个变量时， 编译器会尽量把它存放在通用寄存器中，例如 EBX，以提高访问效率。此后编译器就不会再关心该变量在对应内存位置中原来的内容。若此时其他程序(例如内核程序或中断过程)修改了该变量在对应内存位置处的值，EBX 中的值并不会随之更新。为了解决这种情况就创建了 volatile限定符，让代码在引用该变量时一定要从指定内存位置中取得其值。这里即要求 gcc 不要对jiffies 进行优化处理，也不要挪动位置，并且需要从内存中取其值。因为时钟中断处理过程等程序会修改它的值。

long volatile jiffies = 0;
long startup_time=0;

struct task_struct *current = &(init_task.task); // 当前任务指针(初始化指向任务 0)。
struct task_struct *last_task_used_math = NULL;	 // 使用过协处理器任务的指针。

// 定义任务指针数组。第 1 项被初始化指向初始任务(任务 0)的任务数据结构。
struct task_struct * task[NR_TASKS] = {&(init_task.task), };

// 定义用户堆栈(数组)，共 1K 项，容量 4K 字节。在刚开始内核初始化操作过程中被用作内核栈， // 初始化操作完成后将被用作任务 0 的用户态堆栈。在运行任务 0 之前它是内核栈，以后用作任务 0 // 和任务 1 的用户态栈。下面结构用于设置堆栈 SS:ESP(数据段选择符，偏移)，见 head.s，23 行。 // SS 被设置为内核数据段选择符(0x10)，ESP 被设置为指向 user_stack 数组最后一项后面。这是
// 因为 Intel CPU 执行堆栈操作时总是先递减堆栈指针 ESP 值，然后在 ESP 指针处保存入栈内容。
long user_stack [ PAGE_SIZE>>2 ] ;

struct {
	long * a;
	short b;
	} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };

/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 */
void math_state_restore()
{
	if (last_task_used_math == current)
		return;
	__asm__("fwait");
	if (last_task_used_math) {
		__asm__("fnsave %0"::"m" (last_task_used_math->tss.i387));
	}
	last_task_used_math=current;
	if (current->used_math) {
		__asm__("frstor %0"::"m" (current->tss.i387));
	} else {
		__asm__("fninit"::);
		current->used_math=1;
	}
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
void schedule(void)
{
	int i,next,c;
	struct task_struct ** p;

/* check alarm, wake up any interruptible tasks that have got a signal */
	/* 检测 alarm(进程的报警定时值)，唤醒任何已得到信号的可中断任务 */

	// 从任务数组中最后一个任务开始循环检测 alarm。在循环时跳过空指针项。
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
		if (*p) {
			// 如果设置过任务超时定时值 timeout，并且已经超时，则复位超时定时值，并且如果任务处于可中断睡眠状态 TASK_INTERRUPTIBLE下，将其置为就绪状态(TASK_RUNNING)。
			// if ((*p)->timeout && (*p)->timeout < jiffies)
			// {
			// 	(*p)->timeout = 0;
			// 	if ((*p)->state == TASK_INTERRUPTIBLE)
			// 		(*p)->state = TASK_RUNNING;
			// }
			// 如果设置过任务的 SIGALRM 信号超时定时器值 alarm，并且已经过期(alarm<jiffies),则在信号位图中置 SIGALRM 信号，即向任务发送 SIGALRM 信号。然后清 alarm。该信号的默认操作是终止进程。jiffies 是系统从开机开始算起的滴答数(10ms/滴答)。定义在 sched.h 第 139 行。
			if ((*p)->alarm && (*p)->alarm < jiffies) {
					(*p)->signal |= (1<<(SIGALRM-1));
					(*p)->alarm = 0;
				}
		// 如果信号位图中除被阻塞的信号外还有其他信号，并且任务处于可中断状态，则置任务为就绪状态。其中'~(_BLOCKABLE & (*p)->blocked)'用于忽略被阻塞的信号，但 SIGKILL 和 SIGSTOP 不能被阻塞。
				if (((*p)->signal & (_BLOCKABLE & ~(*p)->blocked)) &&
					(*p)->state == TASK_INTERRUPTIBLE)
					(*p)->state = TASK_RUNNING;
		}

/* this is the scheduler proper: */

	while (1) {
		c = -1;
		next = 0;
		i = NR_TASKS;
		p = &task[NR_TASKS];
		// 这段代码也是从任务数组的最后一个任务开始循环处理，并跳过不含任务的数组槽。它比较每个就绪状态任务的 counter(任务运行时间的递减滴答计数)值，哪一个值大，就表示相应任务的运行时间还有很多，next就指向哪个的任务号。
		while (--i) {
			if (!*--p)
				continue;
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
				c = (*p)->counter, next = i;
		}
		// 如果比较得出有 counter 值不等于 0 的结果，或者系统中没有一个可运行的任务存在(此时 c 仍然为-1，next=0)，则退出外层 while 循环(144 行)，并执行后面的任务切换操作(161 行)。否则就根据每个任务的优先权值，更新每一个任务的 counter 值，然后再回到 144 行重新比较。counter 值的计算方式为 counter = counter/2 + priority
		// 注意，这里计算过程不考虑进程的状态。
		if (c) break;
		for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
			if (*p)
				(*p)->counter = ((*p)->counter >> 1) +
						(*p)->priority;
	}
	switch_to(next);
}

int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return 0;
}

void sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	if (!p)
		return;
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");
	tmp = *p;
	*p = current;
	current->state = TASK_UNINTERRUPTIBLE;
	schedule();
	*p = tmp;
	if (tmp)
		tmp->state=TASK_RUNNING;
}

void interruptible_sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	if (!p)
		return;
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");
	tmp=*p;
	*p=current;
repeat:	current->state = TASK_INTERRUPTIBLE;
	schedule();
	if (*p && *p != current) {
		(*p)->state = TASK_RUNNING;
		goto repeat;
	}
	*p = tmp;
	if (tmp)
		tmp->state = TASK_RUNNING;
}

void wake_up(struct task_struct **p)
{
	if (p && *p) {
		(*p)->state = TASK_RUNNING;
		*p = NULL;
	}
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
static struct task_struct * wait_motor[4] = {NULL,NULL,NULL,NULL};
static int  mon_timer[4]={0,0,0,0};
static int moff_timer[4]={0,0,0,0};
unsigned char current_DOR = 0x0C;

int ticks_to_floppy_on(unsigned int nr)
{
	extern unsigned char selected;
	unsigned char mask = 0x10 << nr;

	if (nr>3)
		panic("floppy_on: nr>3");
	moff_timer[nr]=10000;		/* 100 s = very big :-) */
	cli();				/* use floppy_off to turn it off */
	mask |= current_DOR;
	if (!selected) {
		mask &= 0xFC;
		mask |= nr;
	}
	if (mask != current_DOR) {
		outb(mask,FD_DOR);
		if ((mask ^ current_DOR) & 0xf0)
			mon_timer[nr] = HZ/2;
		else if (mon_timer[nr] < 2)
			mon_timer[nr] = 2;
		current_DOR = mask;
	}
	sti();
	return mon_timer[nr];
}

void floppy_on(unsigned int nr)
{
	cli();
	while (ticks_to_floppy_on(nr))
		sleep_on(nr+wait_motor);
	sti();
}

void floppy_off(unsigned int nr)
{
	moff_timer[nr]=3*HZ;
}

void do_floppy_timer(void)
{
	int i;
	unsigned char mask = 0x10;

	for (i=0 ; i<4 ; i++,mask <<= 1) {
		if (!(mask & current_DOR))
			continue;
		if (mon_timer[i]) {
			if (!--mon_timer[i])
				wake_up(i+wait_motor);
		} else if (!moff_timer[i]) {
			current_DOR &= ~mask;
			outb(current_DOR,FD_DOR);
		} else
			moff_timer[i]--;
	}
}

#define TIME_REQUESTS 64

static struct timer_list {
	long jiffies;
	void (*fn)();
	struct timer_list * next;
} timer_list[TIME_REQUESTS], * next_timer = NULL;

// 添加定时器。输入参数为指定的定时值(滴答数)和相应的处理程序指针。
// 软盘驱动程序(floppy.c)利用该函数执行启动或关闭马达的延时操作。
// 参数 jiffies – 以 10 毫秒计的滴答数;*fn()- 定时时间到时执行的函数。
void add_timer(long jiffies, void (*fn)(void))
{
	struct timer_list * p;

	if (!fn)
		return;
	cli();
	if (jiffies <= 0)
		(fn)();
	else {
		for (p = timer_list ; p < timer_list + TIME_REQUESTS ; p++)
			if (!p->fn)
				break;
		if (p >= timer_list + TIME_REQUESTS)
			panic("No more time requests free");
		p->fn = fn;
		p->jiffies = jiffies;
		p->next = next_timer;
		next_timer = p;
		// 链表项按定时值从小到大排序。在排序时减去排在前面需要的滴答数，这样在处理定时器时 // 只要查看链表头的第一项的定时是否到期即可。
		while (p->next && p->next->jiffies < p->jiffies) {
			p->jiffies -= p->next->jiffies;
			fn = p->fn;
			p->fn = p->next->fn;
			p->next->fn = fn;
			jiffies = p->jiffies;
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
	}
	sti();
}

//// 定时器中断 C 函数处理程序，在 sys_call.s 中的_timer_interrupt(189 行)中被调用。 // 参数 cpl 是当前特权级 0 或 3，它是时钟中断发生时正被执行的代码选择符中的特权级。
// cpl=0 时表示中断发生时正在执行内核代码;cpl=3 时表示中断发生时正在执行用户代码。 // 对于一个任务，若其执行时间片用完，则进行任务切换。同时函数执行一个计时更新工作。
void do_timer(long cpl)
{
	extern int beepcount;
	extern void sysbeepstop(void);

	if (beepcount)
		if (!--beepcount)
			sysbeepstop();

	// 如果当前特权级(cpl)为 0(最高，表示是内核程序在工作)，则将内核代码运行时间 stime递增;[ Linus 把内核程序统称为超级用户(supervisor)的程序，见 sys_call.s，207 行上的英文注释。这种称呼来自于 Intel CPU 手册。] 如果 cpl > 0，则表示是一般用户程序在工作，增加 utime。

	if (cpl)
		current->utime++;
	else
		current->stime++;

	// 如果有定时器存在，则将链表第 1 个定时器的值减 1。如果已等于 0，则调用相应的处理程序，并将该处理程序指针置为空。然后去掉该项定时器。

	if (next_timer) {
		next_timer->jiffies--;
		while (next_timer && next_timer->jiffies <= 0) {
			void (*fn)(void);
			
			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;
			(fn)();  //调用定时处理函数
		}
	}

	if (current_DOR & 0xf0)
		do_floppy_timer();

	// 如果任务运行时间还没用完，则退出这里继续运行该任务。否则置当前任务运行计数值为 0。并且 // 若发生时钟中断时正在内核代码中运行则返回，否则表示在执行用户程序，于是调用调度函数尝试 // 执行任务切换操作。
	if ((--current->counter)>0) return;
	current->counter=0;
	if (!cpl) return;

	//调度进程
	schedule();
}

// 系统调用功能 - 设置报警定时器时间值(秒)。
// 若参数 seconds > 0，则设置新定时时间值，并返回原定时时间刻还剩余的间隔时间，否则返回 0。进程数据结构中报警字段 alarm 的单位是系统滴答，它是系统开机运行到现在的嘀嗒数 jiffies
// 与定时值之和，即'jiffies + HZ*定时秒值'，其中常数 HZ = 100。本函数的主要功能是设置 alarm 字段和进行两种单位之间的转换。
int sys_alarm(long seconds)
{
	int old = current->alarm;

	if (old)
		old = (old - jiffies) / HZ;
	current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
	return (old);
}

// 取当前进程号 pid。
int sys_getpid(void)
{
	return current->pid;
}

// 取父进程号 ppid。
int sys_getppid(void)
{
	return current->father;
}

// 取用户号 uid。
int sys_getuid(void)
{
	return current->uid;
}

// 取有效的用户号 euid。
int sys_geteuid(void)
{
	return current->euid;
}

// 取组号 gid。
int sys_getgid(void)
{
	return current->gid;
}

// 取有效的组号 egid。
int sys_getegid(void)
{
	return current->egid;
}

// 系统调用功能 -- 降低对 CPU 的使用优先权(有人会用吗?)。
// 应该限制 increment 为大于 0 的值，否则可使优先权增大!!
int sys_nice(long increment)
{
	if (current->priority-increment>0)
		current->priority -= increment;
	return 0;
}

void sched_init(void)
{
	int i;
	struct desc_struct * p; //描述符表结构指针

	// Linux 系统开发之初，内核不成熟。内核代码会经常被修改。Linus 怕自己无意中修改了这些 // 关键性的数据结构，造成与 POSIX 标准的不兼容。这里加入下面这个判断语句并无必要，纯粹 // 是为了提醒自己以及其他修改内核代码的人。
	if (sizeof(struct sigaction) != 16)
		panic("Struct sigaction MUST be 16 bytes");

	// 在全局描述符表 GDT 中设置初始任务(任务 0)的任务状态段 TSS 描述符和局部数据表 LDT 描述符。 FIRST_TSS_ENTRY 和 FIRST_LDT_ENTRY 的值分别是 4 和 5，定义在 include/linux/sched.h 中;gdt是一个描述符表数组(include/linux/head.h )，实际上对应程序 head.s 中第 234 行上的全局描述符表基址(_gdt)。因此 gdt+FIRST_TSS_ENTRY 即为 gdt[FIRST_TSS_ENTRY](即是 gdt[4])，也即 gdt 数组第 4 项的地址。 参见 include/asm/system.h,第 65 行开始。
	set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.tss)); //设置任务0的tss描述符
	set_ldt_desc(gdt+FIRST_LDT_ENTRY,&(init_task.task.ldt));  //设置任务0的ldt描述符

	//清除剩余所有描述符为0
	// 清任务数组和描述符表项(注意从 i=1 开始，所以初始任务的描述符还在)。描述符项结构定义在文件 include/linux/head.h 中。
	p = gdt+2+FIRST_TSS_ENTRY;
	for(i=1;i<NR_TASKS;i++) {
		task[i] = NULL;
		p->a=p->b=0;
		p++;
		p->a=p->b=0;
		p++;
	}

/* Clear NT, so that we won't have troubles with that later on */
	/* 清除标志寄存器中的位 NT，这样以后就不会有麻烦 */
	// EFLAGS 中的 NT 标志位用于控制任务的嵌套调用。当 NT 位置位时，那么当前中断任务执行IRET 指令时就会引起任务切换。NT 指出 TSS 中的 back_link 字段是否有效。NT=0 时无效。

	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");

	// 将任务 0 的 TSS 段选择符加载到任务寄存器 tr。将局部描述符表段选择符加载到局部描述符表寄存器 ldtr 中。注意!!是将 GDT 中相应 LDT 描述符的选择符加载到 ldtr。只明确加载这一次，以后新任务 LDT 的加载，是 CPU 根据 TSS 中的 LDT 项自动加载。

	ltr(0);
	lldt(0);

	// 下面代码用于初始化 8253 定时器。通道 0，选择工作方式 3，二进制计数方式。通道 0的输出引脚接在中断控制主芯片的 IRQ0 上，它每 10 毫秒发出一个 IRQ0 请求。LATCH 是初始定时计数值。

	outb_p(0x36,0x43);		/* binary, mode 3, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	//上述代码设置10ms时钟中断

	set_intr_gate(0x20,&timer_interrupt); //设置时钟中断
	outb(inb_p(0x21)&~0x01,0x21);
	set_system_gate(0x80,&system_call);   //设置系统调用门
}
