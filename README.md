Cosix
=====

Cosix is a fully capability-based operating system. It is written from scratch
and consists of a small kernel that provides memory management and
inter-process communication, and a userland that provides an IP stack and
filesystems. It runs only on x86, but there are plans to support x86-64 and
arm7 in future releases.

The capability enforcing mechanism comes from implementing only CloudABI[0]
as an Application Binary Interface between the userland and the kernel.
CloudABI takes POSIX, adds capability-based security, and removes everything
that's incompatible with that. For more information, see https://nuxi.nl/.

Cosix has been tested on bochs, qemu and VirtualBox. Notably, it has not been
tested on real hardware yet.

[0] http://nuxi.nl/cloudabi

How to build it
===============

To compile the kernel, first build and install Binutils with '--target
i686-elf', so that you have the GNU linker in your $PATH as i686-elf-ld. Also,
follow the instructions at https://nuxi.nl/ to install the CloudABI toolchain.
Binaries are available for many operating systems. After installation, you
should also have Clang in your $PATH as i686-unknown-cloudabi-cc.

Use the i686-elf toolchain file to set up your build directory for a native
build:

    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Debug \
     -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-i686-elf.cmake ..
    make

There are various targets to build or run the kernel in various ways:

    boot      - run inside qemu
    fastboot  - run inside qemu, don't build first
    gdbboot   - run inside qemu with -S -s, i.e. wait for debugger to attach
    iso       - build an ISO file using mkisofs/genisoimage/xorriso
    isoboot   - boot ISO using qemu
    bochsboot - run inside bochs

Unit tests
==========

To run the native tests, compile cosix with a native compiler:

    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Debug ..
    make
    make test

Note that the unit test suite is all but complete. Tests with better coverage
are available as userland binaries that can be started when the kernel is
running.

Author & contributors
=====================

See the LICENSE file for license information.

Author: Sjors Gielen <sjors@sjorsgielen.nl>
