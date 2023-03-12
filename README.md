Linux 0.11 Lab
==============

The old Linux kernel source version [0.11](http://oldlinux.org) and the integrated experiment environment.

## Contents

## Introduction

- Features
    - compilable with many different versions of Gcc.
    - add bulit-in qemu and bochs support, include running and debugging.
    - integrate different prebuilt rootfs (floopy, ramdisk and harddisk).
    - allow to generate callgraph of every specified function

## Install the environment

* The Linux distributions: debian and ubuntu (>= 14.04) are recommended
* Install basic tools

        $ sudo apt-get install vim cscope exuberant-ctags build-essential qemu lxterminal

* Optional

        $ sudo apt-get install bochs vgabios bochsbios bochs-doc bochs-x libltdl7 bochs-sdl bochs-term
        $ sudo apt-get install graphviz cflow

## Hack Linux 0.11

**Notes** To enable kvm speedup for hardisk boot, please make sure cpu virtualization is enabled in bios features.

    $ make help		// get help
    $ make  		// compile
    $ make boot-hd	// boot it on qemu with hard disk image
    $ make boot-hd G=0  // Use curses based terminal instead of graphics, friendly for ssh login, exit with 'ESC+2 quit' or 'ALT+2 quit'

    $ make switch                // switch to another emulator, between qemu and bochs
    Switch to use emulator: bochs
    $ make boot VM=qemu|bochs    // specify the emulator, between qemu and bochs

    // edit .kernel_gdbinit(for kernel.sym) and .boot_gdbinit(for bootsect.sym and setup.sym) before debugging

    $ make debug-hd	// debug kernel.sym via qemu and start gdb automatically to connect it.
    $ make debug-hd DST=src/boot/bootsect.sym  // debug bootsect, can not debug code after ljmp
    $ make debug-hd DST=src/boot/setup.sym     // debug setup, can not debug after ljmp

## Hack Rootfs

Three different root filesystem images are stored in `rootfs/`:

* rootram.img -- RAM image
* rootimage   -- Floppy image
* hdc-0.11.img-- Harddisk image

### Ram image

`rootram.img` is mountable directly:

    $ mkdir /path/to/rootram/
    $ sudo mount rootfs/rootram.img /path/to/rootram/

A new target `ramfs-install` is added to install files from `examples/` to ramfs.

    $ make ramfs-install

### Floppy image

`rootimage-0.11` is a minix filesystem, must with `-t minix` obviously:

    $ sudo mkdir /path/to/rootimage/
    $ sudo mount -t minix rootfs/rootimage-0.11 /path/to/rootimage

A new target `flp-install` is added to install files from `examples/` to floppy image.

    $ make flp-install

### Harddisk image

`hdc-0.11.img` has a partition table, should be mounted with an offset:

    $ mkdir /path/to/hdc/
    $ fdisk -lu rootfs/hdc-0.11.img
    $ fdisk -l rootfs/hdc-0.11.img

    Disk rootfs/hdc-0.11.img: 127 MB, 127631360 bytes
    16 heads, 38 sectors/track, 410 cylinders, total 249280 sectors
    Units = sectors of 1 * 512 = 512 bytes
    Sector size (logical/physical): 512 bytes / 512 bytes
    I/O size (minimum/optimal): 512 bytes / 512 bytes
    Disk identifier: 0x00000000

                  Device Boot      Start         End      Blocks   Id  System
    rootfs/hdc-0.11.img1               2      124031       62015   81  Minix / old Linux
    rootfs/hdc-0.11.img2          124032      248063       62016   81  Minix / old Linux

    $ sudo mount -o offset=$((2*512)) rootfs/hdc-0.11.img /path/to/hdc/

A new target `hda-install` is added to install files from `examples/` to harddisk image.

    $ make hda-install

## Examples

Some examples are stored in `examples/` with their own README.md:

### Syscall -- shows how to add a new system call

  Host:

    $ patch -p1 < examples/syscall/syscall.patch
    $ make start-hd

  Emulator:

    $ cd examples/syscall/
    $ make
    as -o syscall.o syscall.s
    ld -o syscall syscall.o
    ./syscall
    Hello, Linux 0.11

**Notes** If not `examples/` found in default directory: `/usr/root` of Linux
          0.11, that means your host may not have minix fs kernel module, compile
          one yourself, or switch your host to Ubuntu.

### Linux 0.00

  Host:

    $ make boot-hd

  Emulator:

    $ cd examples/linux-0.00
    $ make
    $ sync

  Host:

    $ make linux-0.00

### Building Linux 0.11 in Linux 0.11

  Host:

    $ make boot-hd

  Emulator:

    $ cd examples/linux-0.11
    $ make
    $ sync

  Host:

    $ make hd-boot

