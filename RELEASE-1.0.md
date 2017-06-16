2017-06-16: Release of Cosix 1.0
================================

Today marks the first release of Cosix, a fully capability-based operating
system. Cosix is written from scratch and consists of a small kernel that
provides memory management and inter-process communication, and a userland that
provides an IP stack and filesystems. It runs only on x86, but there are plans
to support x86-64 and arm7 in future releases.

The capability enforcing mechanism comes from implementing only CloudABI as an
Application Binary Interface between the userland and the kernel. CloudABI
takes POSIX, adds capability-based security, and removes everything that's
incompatible with that. For more information about CloudABI, see
https://nuxi.nl/.

In the default implementation, Cosix gets its IPv4 address using DHCP and boots
to a Python 3 console listening on TCP port 26. By connecting to this port, you
get a Python shell where you can run the pre-defined test\_cosix() function to
run the userland tests and, if available, the CloudABI unit test suite. Note
that in the default unit test suite many tests still fail because of
implementation gaps.

Cosix comes with a pseudo file descriptor layer that allows filesystems,
sockets and other types of file descriptors to be implemented in user space. A
pseudo file descriptor has a 'pseudo end' and a 'reverse end'. The 'pseudo end'
acts as a normal file descriptor, where actions are translated into RPCs on the
'reverse end'. The IP stack uses this for its TCP and UDP sockets, as well as
the tmpfs implementation.

For compilation instructions and further information, see the README. Binaries
are also available at http://cosix.org/.

Timeline
--------

June 2015: repository created, first boot, handling of interrupts
July 2015: switching between userland and kernel mode
August 2015: rudimentary memory management
(4 months break)
January 2016: rudimentary driver/device stack, network card driver
February 2016: DHCP client in kernel, paging support
(5 months break)
July 2016: rudimentary scheduler
August 2016: system calls, proper processes
September 2016: threading, concurrency primitives, filesystems
November 2016: thread blocking framework, poll
December 2016: proper memory management
January 2017: monotonic clock, allocation tracking
February 2017: UNIX sockets, serial device
May 2017: IP stack in userland, VirtualBox support
June 2017: intel e1k network card driver, Python support

Known issues
------------

* There is no persistent storage support.

There is no support for hard drives. Network storage may be implemented using
the IP stack and the pseudo-filesystem layer and may provide diskless
persistent storage. Hard drive support, as well as network storage filesystems,
are planned to be added in future releases.

* There are gaps in the CloudABI implementation.

Several CloudABI system calls, parameters, or combinations of parameters are
not supported yet. Notably, the real-time clock is currently implemented as an
alias of the monotonic clock. Also, acquiring a lock or waiting on a condvar
using the poll() system call can not be combined with a timeout clock yet.

The system calls mem\_advise, mem\_sync, mem\_protect, fd\_pread, fd\_pwrite,
fd\_sync, fd\_datasync, fd\_replace, file\_advise, file\_stat\_fput,
file\_link, file\_rename, file\_symlink, file\_readlink, file\_allocate and
poll\_fd are not implemented and will return ENOSYS. Also, many flags like
MSG\_PEEK and O\_NONBLOCK are not yet supported. Also, processes cannot have
shared memory mappings yet.

Because of this, the CloudABI unittest has about 100 out of more than 900
tests that fail. This will be addressed in future releases.

* The TCP stack is not standards compliant.

The TCP stack does not keep track of sent segments and does not retransmit if
no ACK arrives. Instead, it sends a full new segment every time it has data.
This is accepted by many TCP stack implementations but not standards compliant.
Also, it may lead to broken connections when a packet is dropped and both
implementations remain waiting for additional data forever.

The incoming packet parser only accepts in-order packets. While the lack of
ACKs for the dropped or out-of-order packets will cause the sending
implementation to retransmit, this will lead to reduced performance.

There are several other smaller problems in the implementation, such as not
using the window size of the peer as a maximum outstanding window. These issues
will all be addressed in future releases.

* No stress testing, profiling or other performance analysis has been
performed.

It is likely that increased stress on the kernel or userland service daemons
will cause them to misbehave or perform badly. Such tests will be performed on
version 1.0 and corresponding improvements will appear in future releases.

A memory leak checker is present in debug mode and can be turned on using the
procfs, a file descriptor provided to init. There are several known memory
leaks. For example, threads that are blocked while their process exits may only
be cleaned up if they ever unblock. In the future, such threads will be
unblocked without their blocking constraints being satisfied, so they can be
cleaned up.

* The random\_get() system call does not provide cryptographically secure
random data.

Currently, the PRNG used for providing random data for the random\_get() system
call is a Mersenne twister operating on a fixed seed. Therefore, it is not
reliable when cryptographically secure operations are necessary. In future
releases, Cosix will likely switch to using various entropy sources and a
reliable CSPRNG implementation.

* Pointers given to the kernel from the userland are often not checked for
correctness.

This means that the userland can cause the kernel to overwrite kernel data
or cause a page fault in kernel mode, which would cause a kernel panic.
In future releases, code will be added to copy data between userland and
kernel in a secure way.
