/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>

void do_exit(int error_code);

// 获取当前任务信号屏蔽位图(屏蔽码或阻塞码)。sgetmask 可分解为 signal-get-mask。以下类似。
int sys_sgetmask()
{
	return current->blocked;
}

// 设置新的信号屏蔽位图。信号 SIGKILL 和 SIGSTOP 不能被屏蔽。返回值是原信号屏蔽位图。
int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1));
	return old;
}

// 复制 sigaction 数据到 fs 数据段 to 处。即从内核空间复制到用户(任务)数据段中。首先验证 to 处的内存空间是否足够大。然后把一个 sigaction 结构信息复制到 fs 段(用户)空间中。宏函数 put_fs_byte()在 include/asm/segment.h 中实现。
static inline void save_old(char * from,char * to)
{
	int i;

	verify_area(to, sizeof(struct sigaction));
	for (i=0 ; i< sizeof(struct sigaction) ; i++) {
		put_fs_byte(*from,to);
		from++;
		to++;
	}
}

// 把 sigaction 数据从 fs 数据段 from 位置复制到 to 处。即从用户数据空间取到内核数据段中。
static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}

/* signal()系统调用。类似于 sigaction()。为指定的信号安装新的信号句柄(信号处理程序)。信号句柄可以是用户指定的函数，也可以是 SIG_DFL(默认句柄)或 SIG_IGN(忽略)。
参数 signum --指定的信号;handler -- 指定的句柄;restorer –恢复函数指针，该函数由Libc 库提供。
用于在信号处理程序结束后恢复系统调用返回时几个寄存器的原有值以及系统调用的返回值，就好象系统调用没有执行过信号处理程序而直接返回到用户程序一样。 函数返回原信号句柄。
*/
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;

	// 首先验证信号值在有效范围(1--32)内，并且不得是信号 SIGKILL(和 SIGSTOP)。因为这两个信号不能被进程捕获。
	// 然后根据提供的参数组建 sigaction 结构内容。sa_handler 是指定的信号处理句柄(函数)。sa_mask 是执行信号处理句柄时的信号屏蔽码。sa_flags 是执行时的一些标志组合。这里设定该信号处理句柄只使用 1 次后就恢复到默认值，并允许信号在自己的处理句柄中收到。

	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	tmp.sa_handler = (void (*)(int)) handler;
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	tmp.sa_restorer = (void (*)(void)) restorer;
	handler = (long) current->sigaction[signum-1].sa_handler;
	current->sigaction[signum-1] = tmp;
	return handler;
}

// sigaction()系统调用。改变进程在收到一个信号时的操作。signum 是除了 SIGKILL 以外的任何信号。[如果新操作(action)不为空 ]则新操作被安装。如果 oldaction 指针不为空，则原操作被保留到 oldaction。成功则返回 0，否则为-EINVAL。
int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	tmp = current->sigaction[signum-1];
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	return 0;
}

// 系统调用中断处理程序中真正的信号预处理程序。这段代码的主要作用是将信号处理句柄插入到
// 用户程序堆栈中，并在本系统调用结束返回后立刻去执行信号句柄程序，然后继续执行用户程序。函数的参数包括进入系统调用处理程序 sys_call.s 开始，直到调用本函数(sys_call.s 第 125 行) 前逐步压入堆栈的值。这些值包括(在 sys_call.s 中的代码行):
// 1 CPU 执行中断指令压入的用户栈地址 ss 和 esp、标志寄存器 eflags 和返回地址 cs 和 eip;
// 2 第 85--91 行在刚进入 system_call 时压入栈的段寄存器 ds、es、fs 以及寄存器 eax
// (orig_eax)、edx、ecx 和 ebx 的值;
// 3 第 100 行调用 sys_call_table 后压入栈中的相应系统调用处理函数的返回值(eax)。 
// 4 第 124 行压入栈中的当前处理的信号值(signr)。
void do_signal(long signr,long eax, long ebx, long ecx, long edx,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;
	long old_eip=eip;
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;
	unsigned long * tmp_esp;

	sa_handler = (unsigned long) sa->sa_handler;

	if (sa_handler==1)
		return;
	if (!sa_handler) {
		if (signr==SIGCHLD)
			return;
		else
			do_exit(1<<(signr-1));
	}

	// 如果该信号句柄只需被调用一次，则将该句柄置空。注意，该信号句柄在前面已经保存在 // sa_handler 指针中。
	// 在系统调用进入内核时，用户程序返回地址(eip、cs)被保存在内核态栈中。下面这段代 // 码修改内核态堆栈上用户调用系统调用时的代码指针 eip 为指向信号处理句柄，同时也将 // sa_restorer、signr、进程屏蔽码(如果 SA_NOMASK 没置位)、eax、ecx、edx 作为参数以及 // 原调用系统调用的程序返回指针及标志寄存器值压入用户堆栈。 因此在本次系统调用中断 // 返回用户程序时会首先执行用户的信号句柄程序，然后再继续执行用户程序。
	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;

	// 将内核态栈上用户下一条代码指令指针 eip 指向该信号处理句柄。由于 C 函数是传值函数，因此 // 给 eip 赋值时需要使用 "*(&eip)" 的形式。
	// 这里请注意，使用如下方式(第 193 行)对普通 C 函数参数进行如此修改是不起作用的。因为当 // 函数返回时堆栈上的参数将会被调用者丢弃。这里之所以可以使用这种方式，是因为该函数是从 // 汇编程序中被调用，并且在函数返回后汇编程序并没有把调用 do_signal()时的所有参数都丢弃。 // eip 等仍然在堆栈中。
	// sigaction 结构的 sa_mask 字段给出了在当前信号句柄程序执行期间应该被屏蔽的信号集。同时， // 引起本信号句柄执行的当前信号也会被屏蔽。不过若 sa_flags 中使用了 SA_NOMASK 标志，那么 // 当前信号将不会被屏蔽掉。另外，如果允许信号处理句柄程序收到自己的信号，则也需要将进程 // 的信号阻塞码压入堆栈。
	*(&eip) = sa_handler;
	longs = (sa->sa_flags & SA_NOMASK)?7:8;
	// 将原调用程序的用户堆栈指针向下扩展 7(或 8)个长字(用来存放调用信号句柄的参数等)， // 并检查内存使用情况(例如如果内存超界则分配新页等)。
	*(&esp) -= longs;
	verify_area(esp,longs*4);
	// 在用户堆栈中从下到上存放 sa_restorer、信号 signr、屏蔽码 blocked(若 SA_NOMASK 置位)、 // eax、ecx、edx、eflags 和用户程序原代码指针。
	tmp_esp=esp;
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask;
}
