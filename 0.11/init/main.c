/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
static inline int fork(void) __attribute__((always_inline));
static inline int pause(void) __attribute__((always_inline));
static inline _syscall0(int,fork)
static inline _syscall0(int,pause)  //系统调用：
static inline _syscall1(int,setup,void *,BIOS)
static inline _syscall0(int,sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */

/*以下这些数据是在内核引导期间由 setup.s 程序设置的。 * /
/* 下面三行分别将指定的线性地址强行转换为给定数据类型的指针，并获取指针所指内容。由于内核代码段被映射到从物理地址零开始的地方，因此这些线性地址正好也是对应的物理地址。这些指定地址处内存值的含义(setup 程序读取并保存的参数)。
drive_info 结构请参见下面第 125 行。*/

#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

static void time_init(void)
{
	struct tm time;

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

struct drive_info { char dummy[32]; } drive_info;

void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */

/*
* 此时中断仍被禁止着，做完必要的设置后就将其开启。 */
// 首先保存根文件系统设备号和交换文件设备号，并根据 setup.s 程序中获取的信息设置控制台终端屏幕行、列数环境变量 TERM，并用其设置初始 init进程中执行 etc/rc 文件和 shell 程序使用的环境变量，以及复制内存 0x90080 处的硬盘参数表(请参见 6.3.3 节表 6-4)。
// 其中 ROOT_DEV 已在前面包含进的 include/linux/fs.h 文件第 206 行上被声明为 extern int，而 SWAP_DEV 在 include/linux/mm.h 文件内也作了相同声明。这里 mm.h 文件并没有显式地列在本程序前部，因为前面包含进的 include/linux/sched.h 文件中已经含有它。

 	ROOT_DEV = ORIG_ROOT_DEV;
 	drive_info = DRIVE_INFO;
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= 0xfffff000;
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;
	main_memory_start = buffer_memory_end;

// 如果在 Makefile 文件中定义了内存虚拟盘符号 RAMDISK，则初始化虚拟盘。此时主内存将减少。
//参见 kernel/blk_drv/ramdisk.c。
#ifdef RAMDISK_SIZE
	main_memory_start += rd_init(main_memory_start, RAMDISK_SIZE*1024);
#endif

	mem_init(main_memory_start,memory_end);
	trap_init();   //陷阱初始化
	blk_dev_init();
	chr_dev_init();
	tty_init();    //tty 初始化
	time_init();
	sched_init();
	buffer_init(buffer_memory_end);
	hd_init();
	floppy_init();
	sti();   //开启中断

//	下面过程通过在堆栈中设置的参数，利用中断返回指令启动任务 0 执行。然后在任务 0 中立刻运行 fork() 创建任务 1(又称 init 进程)，并在任务 1 中执行 init() 函数。 对于被新创建的子进程， fork() 将返回 0 值，对于原进程(父进程) 则返回子进程的进程号 pid。

	move_to_user_mode(); //加载任务0的代码段和数据段并移到用户模式
	if (!fork()) {		/* we count on this going ok */
		init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
	for(;;)
		pause(); //进程 0 通常也被称为 idle 进程。此时进程 0 仅执行 pause()系统调用，并又会调用调度函数。
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

// init()函数运行在任务 0 第 1 次创建的子进程 1(任务 1)中。它首先对第一个将要执行的
// 程序(shell)的环境进行初始化，然后以登录 shell 方式加载该程序并执行之。
void init(void)
{
	int pid,i;
	// setup() 是一个系统调用。用于读取硬盘参数包括分区表信息并加载虚拟盘(若存在的话)和 安装根文件系统设备。该函数用 25 行上的宏定义，对应函数是 sys_setup()，其实现请参见 // kernel/blk_drv/hd.c，74 行。
	setup((void *) &drive_info);

	// 下面以读写访问方式打开设备“/dev/tty0”，它对应终端控制台。由于这是第一次打开文件操作，因此产生的文件句柄号(文件描述符)肯定是 0。该句柄是 UNIX 类操作系统默认的控 // 制台标准输入句柄 stdin(0)。这里再把它以读和写的方式分别打开是为了复制产生标准输出句柄 stdout(1)和标准出错输出句柄 stderr(2)。函数前面的“(void)”前缀用于强制函数无需返回值。
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);

	// 下面打印缓冲区块数(每块 1024 字节)和总字节数，以及主内存区空闲内存字节数。
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);

 /*
 下面再创建一个子进程(任务 2)，并在该子进程中运行/etc/rc 文件中的命令。对于被创建的子进程，fork()将返回 0 值，对于原进程(父进程)则返回子进程的进程号 pid。所以第 195-201 行是子进程中执行的代码。该子进程的代码首先把标准输入 stdin 重定向到/etc/rc 文件，然后使用execve()函数运行/bin/sh 程序。该程序从标准输入中读取 rc 文件中的命令，并以解释方式执行之。sh 运行时所携带的参数和环境变量分别由 argv_rc 和 envp_rc 数组给出。
 关闭句柄 0 并立刻打开/etc/rc 文件的作用是把标准输入 stdin 重新定向到/etc/rc 文件。这样通过控制台读操作就可以读取/etc/rc 文件中的内容。由于这里 sh 的运行方式是非交互式的，因此在执行完 rc 文件后就会立刻退出，进程 2 也会随之结束。关于 execve()函数说明请参见 fs/exec.c程序，207 行。函数_exit()退出时的出错码 1-操作未许可;2-文件或目录不存在。*/

	if (!(pid = fork()))
	{
		close(0);
		if (open("/etc/rc", O_RDONLY, 0))
			_exit(1);
		execve("/bin/sh", argv_rc, envp_rc);
		_exit(2);
	}

	// 下面是父进程(1)执行的语句。wait()等待子进程停止或终止，返回值应是子进程的进程号
	// (pid)。这三句的作用是父进程等待子进程的结束。&i 是存放返回状态信息的位置。如果 wait() // 返回值不等于子进程号，则继续等待。
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;

	// 如果执行到这里，说明刚创建的子进程已执行完/etc/rc 文件(或文件不存在)，因此该子进程自动停止或终止。下面循环中会再次创建一个子进程，用于运行登录和控制台 shell 程序。
	// 该新建子进程首先将关闭所有以前还遗留的句柄(stdin, stdout, stderr)，新创建一个会话，然后重新打开/dev/tty0 作为 stdin，并复制生成 stdout 和 stderr。然后再次执行/bin/sh 程序。但这次执行所选用的参数和环境数组是另一套(见上 122-123 行)。此后父进程再次运行 wait()等待。如果子进程又停止执行(例如用户执行了 exit 命令)，则在标准输出上显示出错信息
	// “子进程 pid 停止运行，返回码是 i”，然后继续重试下去...，形成“大”死循环。

	while (1) 
	{
		if ((pid=fork())<0) //创建新进程失败
		{
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid)   //子进程执行
		{
			close(0);close(1);close(2);  
			setsid();  //创建会话
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));  //sh 执行命令
		}
		while (1)    
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();  // 同步操作 刷新缓冲区
	}
	_exit(0);	/* NOTE! _exit, not exit() */
	// _exit()和 exit()都用于正常终止一个函数。但_exit()直接是一个 sys_exit系统调用，而exit()则通常是普通函数库中的一个函数。它会先执行一些清除操作，例如调用执行各终止处理程序、关闭所有标准 IO 等，然后调用 sys_exit。
}
