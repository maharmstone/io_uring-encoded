io_uring btrfs encoded I/O
==========================

This is a test program for btrfs encoded I/O, demonstrating that the new
io_uring interface produces the same results as the existing BTRFS_IOC_ENCODED_READ
and BTRFS_IOC_ENCODED_WRITE ioctls.

To compile this you will need to have CMake and a recent version of GCC installed,
as well as the kernel headers and liburing. We're controlling io_uring manually
here rather than using the liburing helper functions, but still need the header
files that it provides.

```
$ mkdir build
$ cd build
$ cmake ..
$ make
```

Read test
---------
```
$ sudo btrfs receive . < ../stream
$ cd subvol
$ sudo ../read-test
```

You should see:
```
normal.txt: ioctl okay
zlib.txt: ioctl okay
lzo.txt: ioctl okay
zstd.txt: ioctl okay
inline.txt: ioctl okay
inline-zlib.txt: ioctl okay
inline-lzo.txt: ioctl okay
inline-zstd.txt: ioctl okay
prealloc.txt: ioctl okay
normal.txt: io_uring okay
zlib.txt: io_uring okay
lzo.txt: io_uring okay
zstd.txt: io_uring okay
inline.txt: io_uring okay
inline-zlib.txt: io_uring okay
inline-lzo.txt: io_uring okay
inline-zstd.txt: io_uring okay
prealloc.txt: io_uring okay
```

Write test
----------
```
$ sudo ./write-test
```

You should see:
```
zlib.txt: ioctl okay
lzo.txt: ioctl okay
zstd.txt: ioctl okay
inline-zlib.txt: ioctl okay
inline-lzo.txt: ioctl okay
inline-zstd.txt: ioctl okay
bookend-zlib.txt: ioctl okay
bookend-lzo.txt: ioctl okay
bookend-zstd.txt: ioctl okay
zlib.txt: io_uring okay
lzo.txt: io_uring okay
zstd.txt: io_uring okay
inline-zlib.txt: io_uring okay
inline-lzo.txt: io_uring okay
inline-zstd.txt: io_uring okay
bookend-zlib.txt: io_uring okay
bookend-lzo.txt: io_uring okay
bookend-zstd.txt: io_uring okay
```

Both the ioctl and io_uring interfaces require CAP_SYS_ADMIN (root), as
bookending can mean that they can leak data from other files.

To compile a 32-bit version on a 64-bit operating system, invoke CMake as
follows:

```
$ cmake -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_C_FLAGS=-m32 -DCMAKE_ASM_FLAGS=-m32 ..
```

Note that the LZO test will fail if your filesystem's sector size is not 4096.
This is because there are different LZO variants for each sector size, and if
there is a mismatch `btrfs receive` will handle this transparently.
