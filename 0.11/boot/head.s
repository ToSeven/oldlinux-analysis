/*
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 */
.text
.globl idt,gdt,pg_dir,tmp_floppy_area
pg_dir:
.globl startup_32
startup_32:
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
	lss stack_start,%esp
	call setup_idt
	call setup_gdt
	movl $0x10,%eax		# reload all the segment registers
	mov %ax,%ds		# after changing gdt. CS was already
	mov %ax,%es		# reloaded in 'setup_gdt'
	mov %ax,%fs
	mov %ax,%gs
	lss stack_start,%esp
	xorl %eax,%eax
1:	incl %eax		# check that A20 really IS enabled
	movl %eax,0x000000	# loop forever if it isn't
	cmpl %eax,0x100000
	je 1b

/*
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
	movl %cr0,%eax		# check math chip
	andl $0x80000011,%eax	# Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good */
	orl $2,%eax		# set MP
	movl %eax,%cr0
	call check_x87
	jmp after_page_tables

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
check_x87:
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f			/* no coprocessor: have to set bits */
	movl %cr0,%eax
	xorl $6,%eax		/* reset MP, set EM */
	movl %eax,%cr0
	ret
.align 2
1:	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */

 /*
* 下面这段是设置中断描述符表子程序 setup_idt *
* 将中断描述符表 idt 设置成具有 256 个项，并都指向 ignore_int 中断门。然后加载中断
* 描述符表寄存器(用 lidt 指令)。真正实用的中断门以后再安装。当我们在其他地方认为一切
* 都正常时再开启中断。该子程序将会被页表覆盖掉。 */
# 中断描述符表中的项虽然也是 8 字节组成，但其格式与全局表中的不同，被称为门描述符
# (Gate Descriptor)。它的 0-1,6-7 字节是偏移量，2-3 字节是选择符，4-5 字节是一些标志。 # 这段代码首先在 edx、eax 中组合设置出 8 字节默认的中断描述符值，然后在 idt 表每一项中 # 都放置该描述符，共 256 项。eax 含有描述符低 4 字节，edx 含有高 4 字节。内核在随后的初始 # 化过程中会替换安装那些真正实用的中断描述符项。

setup_idt:
	lea ignore_int,%edx     /*ignore_int 中断处理程序*/
	movl $0x00080000,%eax
	movw %dx,%ax		/* selector = 0x0008 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */

	lea idt,%edi
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)     /*eax 选择符低4位*/  //将哑中断描述符放入表中
	movl %edx,4(%edi)    /*edx 选择符高4位*/
	addl $8,%edi
	dec %ecx
	jne rp_sidt
	lidt idt_descr
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */

 /*
* 设置全局描述符表项 setup_gdt
* 这个子程序设置一个新的全局描述符表 gdt，并加载。此时仅创建了两个表项，与前
* 面的一样。该子程序只有两行，“非常的”复杂，所以当然需要这么长的注释了。
* 该子程序将被页表覆盖掉。 */

setup_gdt:
	lgdt gdt_descr
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
 * more than 16MB will have to expand this.
 */
 /* Linus 将内核的内存页表直接放在页目录之后，使用了 4 个表来寻址 16 MB 的物理内存。
* 如果你有多于 16 Mb 的内存，就需要在这里进行扩充修改。
*/
# 每个页表长为 4KB(1 页内存页面)，而每个页表项需要 4 个字节，因此一个页表共可以存放
# 1024 个表项。如果一个页表项寻址 4KB 的地址空间，则一个页表就可以寻址 4 MB 的物理内存。
# 页表项的格式为:项的前 0-11 位存放一些标志，例如是否在内存中(P 位 0)、读写许可(R/W 位 1)、 # 普通用户还是超级用户使用(U/S 位 2)、是否修改过(是否脏了)(D 位 6)等;表项的位 12-31是
# 页框地址，用于指出一页内存的物理起始地址。

.org 0x1000
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 */
tmp_floppy_area:
	.fill 1024,1,0

# 下面这几个入栈操作用于为跳转到 init/main.c 中的 main()函数作准备工作。第 139 行上的指令 # 在栈中压入了返回地址(标号 L6)，而第 140 行则压入了 main()函数代码的地址。当 head.s 最后 # 在第 218 行执行 ret 指令时就会弹出 main()的地址，并把控制权转移到 init/main.c 程序中。 # 参见第 3 章中有关 C 函数调用机制的说明。
# 前面 3 个入栈 0 值分别表示 main 函数的参数 envp、argv 指针和 argc，但 main()没有用到。
# 139 行的入栈操作是模拟调用 main 程序时将返回地址入栈的操作，所以如果 main.c 程序
# 真的退出时，就会返回到这里的标号 L6 处继续执行下去，也即死循环。140 行将 main.c 的地址 # 压入堆栈，这样，在设置分页处理(setup_paging)结束后执行'ret'返回指令时就会将 main.c # 程序的地址弹出堆栈，并去执行 main.c 程序了。
after_page_tables:
	pushl $0		# These are the parameters to main :-)
	pushl $0
	pushl $0
	pushl $L6		# return address for main, if it decides to.
	pushl $main
	jmp setup_paging
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

/* This is the default interrupt "handler" :-) */
//下面是默认的中断向量句柄  即中断 后面会

int_msg:
	.asciz "Unknown interrupt\n\r"
.align 2
ignore_int:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	pushl $int_msg
	call printk
	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 16 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * won't guarantee that's all :-( )
 */

 # 上面英文注释第 2 段的含义是指在机器物理内存中大于 1MB 的内存空间主要被用于主内存区。 # 主内存区空间由 mm 模块管理。它涉及到页面映射操作。内核中所有其他函数就是这里指的一般(普通)函数。若要使用主内存区的页面，就需要使用 get_free_page()等函数获取。因为主内存区中内存页面是共享资源，必须有程序进行统一管理以避免资源争用和竞争。
#
# 在内存物理地址 0x0 处开始存放 1 页页目录表和 4 页页表。页目录表是系统所有进程公用的，而
# 这里的 4 页页表则属于内核专用，它们一一映射线性地址起始 16MB 空间范围到物理内存上。对于
# 新建的进程，系统会在主内存区为其申请页面存放页表。另外，1 页内存长度是 4096 字节。

.align 2
setup_paging:
	movl $1024*5,%ecx		/* 5 pages - pg_dir+4 page tables */
	xorl %eax,%eax
	xorl %edi,%edi			/* pg_dir is at 0x000 */
	cld;rep;stosl

#$pg0+7=0x00001007  0000 0000 0000 0000 0001 0000 0000 1011
# 下面 4 句设置页目录表中的项。因为我们(内核)共有 4 个页表，所以只需设置 4 项。 
# 页目录项的结构与页表中项的结构一样，4 个字节为 1 项。参见上面 113 行下的说明。 例如"$pg0+7"表示:0x00001007，是页目录表中的第 1 项。则第 1 个页表所在的地址 = 0x00001007 & 0xfffff000 = 0x1000; 第 1 个页表的属性标志 = 0x00001007 & 0x00000fff = 0x07，表示该页存在、用户可读写
	movl $pg0+7,pg_dir		/* set present bit/user r/w  可读可写 任何特权就可访问*/ 
	movl $pg1+7,pg_dir+4		/*  --------- " " --------- */
	movl $pg2+7,pg_dir+8		/*  --------- " " --------- */
	movl $pg3+7,pg_dir+12		/*  --------- " " --------- */
	movl $pg3+4092,%edi
	movl $0xfff007,%eax		/*  16Mb - 4096 + 7 (r/w user,p) */
	std
1:	stosl			/* fill pages backwards - more efficient :-) */
	subl $0x1000,%eax
	jge 1b
# 现在设置页目录表基址寄存器 cr3，指向页目录表。cr3 中保存的是页目录表的物理地址，然后 # 再设置启动使用分页处理(cr0 的 PG 标志，位 31)
	cld
	xorl %eax,%eax		/* pg_dir is at 0x0000 */
	movl %eax,%cr3		/* cr3 - page directory start */
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		/* set paging (PG) bit */
	ret			/* this also flushes prefetch-queue */

# 在改变分页处理标志后要求使用转移指令刷新预取指令队列。这里用的是返回指令 ret。
# 该返回指令的另一个作用是将 140 行压入堆栈中的 main 程序的地址弹出，并跳转到/init/main.c # 程序去运行。本程序到此就真正结束了。
.align 2
.word 0

# 下面是加载中断描述符表寄存器 idtr 的指令 lidt 要求的 6 字节操作数。前 2 字节是 idt 表的限长，
# 后 4 字节是 idt 表在线性地址空间中的 32 位基地址。
idt_descr:         #中断描述符表的位置
	.word 256*8-1		# idt contains 256 entries
	.long idt
.align 2
.word 0

# 下面加载全局描述符表寄存器 gdtr 的指令 lgdt 要求的 6 字节操作数。前 2 字节是 gdt 表的限长，
# 后 4 字节是 gdt 表的线性基地址。这里全局表长度设置为 2KB 字节(0x7ff 即可)，因为每 8 字节
# 组成一个描述符项，所以表中共可有 256 项。符号_gdt 是全局表在本程序中的偏移位置，见 234 行。
gdt_descr:        #全局描述符表的位置
	.word 256*8-1		# so does gdt (not that that's any
	.long gdt		# magic number, but it works for me :^)

.align 8
idt:	.fill 256,8,0		# idt is uninitialized

# 全局描述符表。其前 4 项分别是:空项(不用)、代码段描述符、数据段描述符、系统调用段描述符，其中系统调用段描述符并没有派用处，Linus 当时可能曾想把系统调用代码放在这个独立的段中。
# 后面还预留了 252 项的空间，用于放置新创建任务的局部描述符(LDT)和对应的任务状态段 TSS
# 的描述符。
# (0-nul, 1-cs, 2-ds, 3-syscall,4-TSS0, 5-LDT0, 6-TSS1, 7-LDT1, 8-TSS2 etc...)
gdt:	
	.quad 0x0000000000000000	/* NULL descriptor */
	.quad 0x00c09a0000000fff	/* 16Mb  */
	/*    
		  00c0			    	9a00  	
		  0000 0000 1100 0000   1001 1010 0000 0000 //type 010 代码 执行/可读
		  0000					0fff
		  0000 0000 0000 0000   0000 1111 1111 1111
	*/
	.quad 0x00c0920000000fff	/* 16Mb */
	/*    
		  00c0			    	9200  	
		  0000 0000 1100 0000   1001 0010 0000 0000 //type 数据 可读写
		  0000					0fff
		  0000 0000 0000 0000   0000 1111 1111 1111
	*/
	.quad 0x0000000000000000	/* TEMPORARY - don't use */
	.fill 252,8,0			/* space for LDT's and TSS's etc */

/* *********************************************************************************************


	-----  		  ------| 内存页目录表(4k)   |
	  |				|	| 内存页表pg0(4k)   |
	  |				|	| 内存页表pg1(4k)   |
	  |				|	| 内存页表pg2(4k)   |          		  |  LDT1 描述符       |
      |       head.s代码 | 内存页表pg3(4k)   |				  |  TSS1 描述符        |
内存自上向下增加	   |   | 软盘缓冲区(1k)    |	 			 |  LDT0 描述符   		|
					|	| head.s部分代码    |    		      |  TSS0描述符 		|
					|	| 中断描述符idt(2k) |				  |  系统段描述符(未用)	  |
				------  | 全局描述符gdt(2k) | --------------> |  内核数据段描述符     |
						| main.c程序代码    | 				  | 内核代码段描述符      |
						| kernel模块代码    |				  | 描述符（ NULL)      |
						| mm 模块代码       | 
						| fs 模块代码       |
						| lib 模块代码      |
						| ................ |
*/
