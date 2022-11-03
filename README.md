**Subscribe Wechat**：<br/><img src='images/tinylab-wechat.jpg' width='110px'/><br/>

Linux 0.11 Lab
==============

The old Linux kernel source version [0.11](http://oldlinux.org) and the integrated experiment environment.

[CS630 Qemu Lab](https://gitee.com/tinylab/cs630-qemu-lab) is a related project, it is a friendly learning environment for the X86 assembly course: [CS630](http://www.cs.usfca.edu/%7Ecruse/cs630f06/).

If you want to learn the latest [Linux Kernel](http://www.kernel.org), please try our [Linux Lab](https://gitee.com/tinylab/linux-lab)

[![Linux 0.11 Lab Demo](images/linux-0.11-lab-with-docker-vncserver+novnc.jpg)](http://showdesk.io/2017-03-18-17-54-23-linux-0.11-lab-usage-00-06-42/)

## Contents

- [Introduction](#introduction)

- [Install the environment](#install-the-environment)

- [Hack Linux 0.11](#hack-linux-011)

- [Hack Rootfs](#hack-rootfs)
    - [RAM image](#ram-image)
    - [Floppy image](#floppy-image)
    - [Harddisk image](#harddisk-image)


- [Examples](#examples)

- [Changes](#changes)

- [Contact and Sponsor](#contact-and-sponsor)

## Introduction

- Basic information
    - Homepage: <http://tinylab.org/linux-0.11-lab>
    - Repository: <https://gitee.com/tinylab/linux-0.11-lab>

- Features
    - compilable with many different versions of Gcc.
    - has been tested under modern Linux, Mac OS X and even work on Windows (with docker support).
    - add bulit-in qemu and bochs support, include running and debugging.
    - integrate different prebuilt rootfs (floopy, ramdisk and harddisk).
    - allow to generate callgraph of every specified function

## Install the environment

Linux 0.11 Lab works on Linux, Windows and Mac OSX, but please install docker at first, then simply install the environment with these three commands:

    $ git clone https://gitee.com/tinylab/cloud-lab.git
    $ cd cloud-lab/
    $ tools/docker/run linux-0.11-lab

**Notes**: Some examples depends on minix fs kernel module, but the Linux kernel used by the docker of
           windows, macosx and even some Linux distributions may not provide such kernel module, please compile
           one yourself. Ubuntu is recommended.

## Install docker

Docker is required by Linux 0.11 Lab, please install it at first:

- Linux, Mac OSX, Windows 10: [Docker CE](https://store.docker.com/search?type=edition&offering=community)

- older Windows: [Docker Toolbox](https://www.docker.com/docker-toolbox) or Virtualbox/Vmware + Linux.

**Notes**:

In order to run docker without password, please make sure your user is added in the docker group:

    $ sudo usermod -aG docker $USER

In order to speedup docker images downloading, please configure a local docker mirror in `/etc/default/docker`, for example:

    $ grep registry-mirror /etc/default/docker
    DOCKER_OPTS="$DOCKER_OPTS --registry-mirror=https://docker.mirrors.ustc.edu.cn"
    $ service docker restart

In order to avoid network ip address conflict, please try following changes and restart docker:

    $ grep bip /etc/default/docker
    DOCKER_OPTS="$DOCKER_OPTS --bip=10.66.0.10/16"
    $ service docker restart

If the above changes not work, try something as following:

    $ grep dockerd /lib/systemd/system/docker.service
    ExecStart=/usr/bin/dockerd -H fd:// --bip=10.66.0.10/16 --registry-mirror=https://docker.mirrors.ustc.edu.cn
    $ service docker restart

If still have errors like 'Client.Timeout exceeded while waiting headers', please try the other docker mirror sites:

* Aliyun (Register Required): <http://t.cn/AiFxJ8QE>
* Docker China: https://registry.docker-cn.com

Configuration in Ubuntu:

    $ echo "DOCKER_OPTS=\"\$DOCKER_OPTS --registry-mirror=<your accelerate address>\"" | sudo tee -a /etc/default/docker
    $ sudo service docker restart

For Ubuntu 12.04, please install the new kernel at first, otherwise, docker will not work:

    $ sudo apt-get install linux-generic-lts-trusty

If installed via Docker Toolbox, please enter into the `/mnt/sda1` directory of the `default` system on Virtualbox, otherwise, after poweroff, the data will be lost for the default `/root` directory is only mounted in DRAM.

    $ cd /mnt/sda1

For Linux or Mac OSX, please simply choose one directory in `~/Downloads` or `~/Documents`.

    $ cd ~/Documents

## Older method

This is a **BAD** method, but if really want a try, **please comment 'DOCKER ?= yes' in Makefile at first**:

Here is the deprecated method in Linux system:

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

A new demonstration is added: [Linux 0.11 Lab: Add a new syscall into Linux 0.11](http://showterm.io/4b628301d2d45936a7f8a)

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

## Changes

* 2015-03-15, falcon <wuzhangjin@gmail.com>
    * launch a linux-0.11-lab project, see <http://tinylab.org/linux-0.11-lab>
    * integrate prebuilt rootfs images, no need to install extra images
    * integrate both qemu and bochs support and simplify the testing on emulators
    * add callgraph generation support
    * add docker support, allow to build the whole lab with one command
    * rewrite the document: README.md
    * Fix up "Reset-floppy Called"
    * clean up everything
    * repo: <https://gitee.com/tinylab/linux-0.11-lab>

* 2012-04-30, yuanxinyu <yuanxinyu.hangzhou@gmail.com>
    * add Mac OS X support
    * add latest Gcc support
    * repo: <https://github.com/yuanxinyu/Linux-0.11>

* 2011-07-31, tigercn <moonlight.yang@gmail.com>
    * port to new system and gcc
    * see: <http://www.oldlinux.org/oldlinux/viewthread.php?tid=13681>

* 2008-10-15, falcon <wuzhangjin@gmail.com>
    * add gcc 4.3.2 with 32/64bit host support
    * rewrite boot/{bootsect.s,setup.s} with AT&T
    * replace tools/build.c with tools/build.sh
    * see: <http://www.oldlinux.org/oldlinux/viewthread.php?tid=11651&extra=page%3D1>

* 2005, jiong.zhao <gohigh@sh163.net>
    * write a [book for Linux 0.11](http://www.oldlinux.org/download/clk011c-3.0.pdf)
    * release a version for RedHat 9 with rootfs, compilers and blabla
    * site: <http://www.oldlinux.org>

* 1991, Linus
    * write and release the original Linux 0.11.

## Contact and Sponsor

Our contact wechat is **tinylab**, welcome to join our user & developer discussion group.

** Contact us and Sponsor via wechat **

![contact-sponsor](images/contact-sponsor.png)
