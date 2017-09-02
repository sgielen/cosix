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

[![Build Status](https://travis-ci.org/sgielen/cosix.svg?branch=master)](https://travis-ci.org/sgielen/cosix)

How to build it
===============

To compile the Cosix world including the kernel, you need the following
dependencies:

- The GNU linker with x86 support. If you build binutils with `--target i686`
  you'll have it as i686-elf-ld which is the default name CMake looks for.
  If your GNU ld is named differently, just give `-DCMAKE_GLD_LINKER=ld`.
- The `objcopy` binary with x86 support. The build looks for a binary called
  `objcopy` but does not check early if it has x86 support. If you get
  objcopy-related errors, build binutils with `--target i686` and use
  `-DCMAKE_OBJCOPY=i686-elf-objcopy`.
- The CloudABI toolchain, see instructions at https://nuxi.nl/. The toolchain
  is based on Clang/LLVM and is available for many operating systems. After
  installation, you'll have Clang in your path as i686-unknown-cloudabi-cc.
- At least the following CloudABI ports packages:
  - `i686-unknown-cloudabi-cxx-runtime`
  - `i686-unknown-cloudabi-libircclient`
  - `i686-unknown-cloudabi-mstd`
  - `i686-unknown-cloudabi-python`
  - `i686-unknown-cloudabi-flower`

Other compile flags to use:

- `-DCLOUDABI_PYTHON_BINARY` and `-DCLOUDABI_PYTHON_LIBRARIES`. The initrd
  cannot be built without CloudABI Python, so the `make boot` target cannot
  be run without this. Use the two flags, with absolute directories to the
  Python3 binary and its libraries, to be able to boot.
- `-DCLOUDABI_FLOWER_SWITCHBOARD_BINARY`. As above, this is a necessary
  dependency to run Cosix.
- `-DCLOUDABI_UNITTEST_BINARY` can be pointed at the unittest binary of
  cloudlibc, so you can run it by entering `run_unittests()` at the Python
  shell. Note that because not all system calls are (fully) implemented, the
  default binary will have a lot of failing tests.

Use the i686-elf toolchain file to set up your build directory for a native
build easily:

    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Debug \
     -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-i686-elf.cmake \
     -DCLOUDABI_PYTHON_BINARY=... \
     -DCLOUDABI_PYTHON_LIBRARIES=... \
     ..
    make

There are various targets to build or run the kernel in various ways:

    boot      - run inside qemu
    fastboot  - run inside qemu, don't build first
    gdbboot   - run inside qemu with -S -s, i.e. wait for debugger to attach
    iso       - build an ISO file using mkisofs/genisoimage/xorriso
    isoboot   - boot ISO using qemu
    bochsboot - run inside bochs

Once it's running, it will try to get a DHCP lease and start listening on TCP
port 26. In the default qemu configuration, host port 2626 is forwarded to this
port. Once you connect you'll be presented with a Python shell with these
useful utility commands (implemented in `misc/python`):

    this_conn(): gives a Socket object to your current connection
    rm_rf(name, dir_fd): recursively removes the given name from the directory fd
    run_unittests(): run the CloudABI unittests exactly once, with 1 thread
    run_unittests_count(count): run the unittests $count times
    run_leak_analysis(): run the unittests 4 times, then present kernel memory leak analysis
    run_tests(): run all userland binary tests and the unittests once

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
