/*
 *  linux/fs/char_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/types.h>

#include <linux/sched.h>
#include <linux/kernel.h>

#include <asm/segment.h>
#include <asm/io.h>

extern int tty_read(unsigned minor,char * buf,int count);
extern int tty_write(unsigned minor,char * buf,int count);

typedef int (*crw_ptr)(int rw,unsigned minor,char * buf,int count,off_t * pos);

//// 串口终端读写操作函数。
// 参数:rw - 读写命令;minor - 终端子设备号;buf - 缓冲区;count - 读写字节数; 
// pos - 读写操作当前指针，对于终端操作，该指针无用。
// 返回:实际读写的字节数。若失败则返回出错码。
static int rw_ttyx(int rw,unsigned minor,char * buf,int count,off_t * pos)
{
	return ((rw==READ)?tty_read(minor,buf,count):
		tty_write(minor,buf,count));
}

//// 终端读写操作函数。
// 同上 rw_ttyx()，只是增加了对进程是否有控制终端的检测。
static int rw_tty(int rw,unsigned minor,char * buf,int count, off_t * pos)
{
	// 若进程没有对应的控制终端，则返回出错号。否则调用终端读写函数 rw_ttyx()，并返回 
	// 实际读写字节数。
	if (current->tty<0)
		return -EPERM;
	return rw_ttyx(rw,current->tty,buf,count,pos);
}

//// 内存数据读写。未实现。
static int rw_ram(int rw,char * buf, int count, off_t *pos)
{
	return -EIO;
}

//// 物理内存数据读写操作函数。未实现。
static int rw_mem(int rw,char * buf, int count, off_t * pos)
{
	return -EIO;
}

//// 内核虚拟内存数据读写函数。未实现。
static int rw_kmem(int rw,char * buf, int count, off_t * pos)
{
	return -EIO;
}

// 端口读写操作函数。
// 参数:rw - 读写命令;buf - 缓冲区;cout - 读写字节数;pos - 端口地址。 
// 返回:实际读写的字节数。
static int rw_port(int rw,char * buf, int count, off_t * pos)
{
	int i=*pos;

	while (count-->0 && i<65536) {
		if (rw==READ)
			put_fs_byte(inb(i),buf++);
		else
			outb(get_fs_byte(buf++),i);
		i++;
	}
	i -= *pos;
	*pos += i;
	return i;
}

//// 内存读写操作函数。内存主设备号是 1。这里仅给出对 0-5 子设备的处理。
static int rw_memory(int rw, unsigned minor, char * buf, int count, off_t * pos)
{
	switch(minor) {
		case 0:
			return rw_ram(rw,buf,count,pos);
		case 1:
			return rw_mem(rw,buf,count,pos);
		case 2:
			return rw_kmem(rw,buf,count,pos);
		case 3:
			return (rw==READ)?0:count;	/* rw_null */
		case 4:
			return rw_port(rw,buf,count,pos);
		default:
			return -EIO;
	}
}

// 定义系统中设备种数。
#define NRDEVS ((sizeof (crw_table))/(sizeof (crw_ptr)))

// 字符设备读写函数指针表。
static crw_ptr crw_table[]={
	NULL,		/* nodev */    // 无设备(空设备) 
	rw_memory,	/* /dev/mem etc */   // /dev/mem 等 
	NULL,		/* /dev/fd */    // /dev/fd 软驱
	NULL,		/* /dev/hd */    // /dev/hd 硬盘
	rw_ttyx,	/* /dev/ttyx */   // /dev/ttyx 串口终端
	rw_tty,		/* /dev/tty */   // /dev/tty 终端
	NULL,		/* /dev/lp */   //  /dev/lp 打印机
	NULL};		/* unnamed pipes */   // 未命名管道

//// 字符设备读写操作函数。
// 参数:rw -读写命令;dev -设备号;buf -缓冲区;count -读写字节数;pos -读写指针。 
// 返回:实际读/写字节数。
int rw_char(int rw,int dev, char * buf, int count, off_t * pos)
{
	crw_ptr call_addr;

	if (MAJOR(dev)>=NRDEVS)
		return -ENODEV;
	if (!(call_addr=crw_table[MAJOR(dev)]))
		return -ENODEV;
	return call_addr(rw,MINOR(dev),buf,count,pos);
}
