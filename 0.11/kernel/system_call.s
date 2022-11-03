/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */

/*system_call.s 文件包含系统调用(system-call)底层处理子程序。由于有些代码比较类似，
所以同时也包括时钟中断处理(timer-interrupt)句柄。硬盘和软盘的中断处理程序也在这里。
注意:这段代码处理信号(signal)识别，在每次时钟中断和系统调用之后都会进行识别。一般中断过程并不处理信号识别，因为会给系统造成混乱。
从系统调用返回('ret_from_system_call')时堆栈的内容见上面 19-30 行。

上面 Linus 原注释中一般中断过程是指除了系统调用中断(int 0x80)和时钟中断(int 0x20) # 以外的其他中断。这些中断会在内核态或用户态随机发生，若在这些中断过程中也处理信号识别 # 的话，就有可能与系统调用中断和时钟中断过程中对信号的识别处理过程相冲突，这违反了内核 # 代码非抢占原则。因此系统既无必要在这些“其他”中断中处理信号，也不允许这样做。
*/

SIG_CHLD	= 17

EAX		= 0x00
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
FS		= 0x10
ES		= 0x14
DS		= 0x18
EIP		= 0x1C
CS		= 0x20
EFLAGS		= 0x24
OLDESP		= 0x28
OLDSS		= 0x2C

# 为方便在汇编程序中访问数据结构,这里给出了任务和信号数据结构中指定字段在结构中的偏移值。 # 下面这些是任务结构(task_struct)中字段的偏移值，参见 include/linux/sched.h，105 行起。

state	= 0		# these are offsets into the task-struct.
counter	= 4
priority = 8
signal	= 12
sigaction = 16		# MUST be 16 (=len of sigaction)
blocked = (33*16)

# 以下定义 sigaction 结构中各字段的偏移值，参见 include/signal.h，第 55 行开始。 
# offsets within sigaction
sa_handler = 0
sa_mask = 4
sa_flags = 8
sa_restorer = 12

#系统调用总数
nr_system_calls = 72   

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
.globl system_call,sys_fork,timer_interrupt,sys_execve
.globl hd_interrupt,floppy_interrupt,parallel_interrupt
.globl device_not_available, coprocessor_error

.align 2
bad_sys_call:
	movl $-1,%eax
	iret

.align 2
reschedule:
	pushl $ret_from_sys_call
	jmp schedule

.align 2
system_call:
	cmpl $nr_system_calls-1,%eax
	ja bad_sys_call
	push %ds
	push %es
	push %fs
# 一个系统调用最多可带有 3 个参数，也可以不带参数。下面入栈的 ebx、ecx 和 edx 中放着系统 # 调用相应 C 语言函数(见第 99 行)的调用参数。这几个寄存器入栈的顺序由 GNU gcc 规定， # ebx 中可存放第 1 个参数，ecx 中存放第 2 个参数，edx 中存放第 3 个参数。
# 系统调用语句可参见头文件 include/unistd.h 中第 150 到 200 行的系统调用宏。

	pushl %edx
	pushl %ecx		# push %ebx,%ecx,%edx as parameters
	pushl %ebx		# to the system call

# 在保存过段寄存器之后，让 ds,es 指向内核数据段，而 fs 指向当前局部数据段，即指向执行本 # 次系统调用的用户程序的数据段。注意，在 Linux 0.12 中内核给任务分配的代码和数据内存段 # 是重叠的，它们的段基址和段限长相同。参见 fork.c 程序中 copy_mem()函数。

	movl $0x10,%edx		# set up ds,es to kernel space
	mov %dx,%ds
	mov %dx,%es
	movl $0x17,%edx		# fs points to local data space
	mov %dx,%fs
	
	# cmpl nr_syscalls,%eax
	# jae bad_sys_call

# 下面这句操作数的含义是:调用地址=[_sys_call_table + %eax * 4]。请参见程序后的说明和第3章3.2.3节内容。
# sys_call_table[]是一个指针数组，定义在 include/linux/sys.h 中，该数组中设置了内核所有 82 个系统调用 C 处理函数的地址。

	call *sys_call_table(,%eax,4)
	pushl %eax

# 下面 101-106 行查看当前任务的运行状态。如果不在就绪状态(state 不等于 0)就去执行调度 # 程序。如果该任务在就绪状态，但是其时间片已经用完(counter=0)，则也去执行调度程序。 # 例如当后台进程组中的进程执行控制终端读写操作时，那么默认条件下该后台进程组所有进程 # 会收到 SIGTTIN 或 SIGTTOU 信号，导致进程组中所有进程处于停止状态，而当前进程则会立刻 # 返回。

	movl current,%eax
	cmpl $0,state(%eax)		# state
	jne reschedule
	cmpl $0,counter(%eax)		# counter
	je reschedule

# 以下这段代码执行从系统调用的 C 函数返回后，对信号进行识别处理。其他中断服务程序退出时也 # 将跳转到这里进行处理后才退出中断过程，例如后面 131 行上的处理器出错中断 int 16。
# 首先判别当前任务是否是初始任务 task0，如果是则不必对其进行信号方面的处理，直接返回。
# 109 行上的_task 对应 C 程序中的 task[]数组，直接引用 task 相当于引用 task[0]。

ret_from_sys_call:
	movl current,%eax		# task[0] cannot have signals
	cmpl task,%eax
	je 3f

# 通过对原调用程序代码选择符的检查来判断调用程序是否是用户任务。如果不是则直接退出中断 # (因为任务在内核态执行时不可抢占)。否则对任务进行信号识别处理。这里通过比较选择符是 # 否为用户代码段的选择符 0x000f(RPL=3，局部表，代码段)来判断是否为用户任务。如果不是 # 则说明是某个中断服务程序(例如中断 16)跳转到第 107 行执行到此，于是跳转退出中断程序。 # 另外，如果原堆栈段选择符不为 0x17(即原堆栈不在用户段中)，也说明本次系统调用的调用者 # 不是用户任务，则也退出。

	cmpw $0x0f,CS(%esp)		# was old code segment supervisor ?
	jne 3f
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 ?
	jne 3f

# 下面这段代码(行 115-128)用于处理当前任务的信号。首先取当前任务结构中的信号位图(32 位， # 每位代表 1 种信号)，然后用任务结构中的信号阻塞(屏蔽)码，阻塞不允许的信号位，取得数值
# 最小的信号值，再把原信号位图中该信号对应的位复位(置 0)，最后将该信号值作为参数之一调
# 用 do_signal()。do_signal()在(kernel/signal.c,128)中，其参数包括 13 个入栈的信息。
# 在 do_signal()或信号处理函数返回之后，若返回值不为 0 则再看看是否需要切换进程或继续处理 # 其它信号。

	movl signal(%eax),%ebx
	movl blocked(%eax),%ecx
	notl %ecx
	andl %ebx,%ecx
	bsfl %ecx,%ecx
	je 3f
	btrl %ecx,%ebx
	movl %ebx,signal(%eax)
	incl %ecx
	pushl %ecx
	call do_signal
	popl %eax
3:	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret

#### int16 -- 处理器错误中断。 类型:错误;无错误码。
# 这是一个外部的基于硬件的异常。当协处理器检测到自己发生错误时，就会通过 ERROR 引脚 # 通知 CPU。下面代码用于处理协处理器发出的出错信号。并跳转去执行 C 函数 math_error() # (kernel/math/error.c 11)。返回后将跳转到标号 ret_from_sys_call 处继续执行。
.align 2
coprocessor_error:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	jmp math_error

#### int7 -- 设备不存在或协处理器不存在。 类型:错误;无错误码。
# 如果控制寄存器 CR0 中 EM(模拟)标志置位，则当 CPU 执行一个协处理器指令时就会引发该
# 中断，这样 CPU 就可以有机会让这个中断处理程序模拟协处理器指令(181 行)。
# CR0 的交换标志 TS 是在 CPU 执行任务转换时设置的。TS 可以用来确定什么时候协处理器中的
# 内容与 CPU 正在执行的任务不匹配了。当 CPU 在运行一个协处理器转义指令时发现 TS 置位时， # 就会引发该中断。此时就可以保存前一个任务的协处理器内容，并恢复新任务的协处理器执行
# 状态(176 行)。参见 kernel/sched.c，92 行。该中断最后将转移到标号 ret_from_sys_call
# 处执行下去(检测并处理信号)。

.align 2
device_not_available:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	clts				# clear TS so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit)
	je math_state_restore
	pushl %ebp
	pushl %esi
	pushl %edi
	call math_emulate
	popl %edi
	popl %esi
	popl %ebp
	ret

.align 2
timer_interrupt:
	push %ds		# save ds,es and put kernel data space
	push %es		# into them. %fs is used by _system_call
	push %fs
	pushl %edx		# we save %eax,%ecx,%edx as gcc doesn't
	pushl %ecx		# save those across function calls. %ebx
	pushl %ebx		# is saved as we use that in ret_sys_call
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	incl jiffies
	movb $0x20,%al		# EOI to interrupt controller #1
	outb %al,$0x20
	movl CS(%esp),%eax
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ...
	jmp ret_from_sys_call

.align 2
sys_execve:
	lea EIP(%esp),%eax
	pushl %eax
	call do_execve
	addl $4,%esp
	ret

.align 2
sys_fork:
	call find_empty_process
	testl %eax,%eax
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call copy_process
	addl $20,%esp
1:	ret

hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1
	jmp 1f			# give port chance to breathe
1:	jmp 1f
1:	xorl %edx,%edx
	xchgl do_hd,%edx
	testl %edx,%edx
	jne 1f
	movl $unexpected_hd_interrupt,%edx
1:	outb %al,$0x20
	call *%edx		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20		# EOI to interrupt controller #1
	xorl %eax,%eax
	xchgl do_floppy,%eax
	testl %eax,%eax
	jne 1f
	movl $unexpected_floppy_interrupt,%eax
1:	call *%eax		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
