#include <iostream>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <btrfs/ioctl.h>
#include "unique_fd.h"

using namespace std;

extern uint8_t dump_bookend_zstd;
extern uint64_t dump_bookend_zstd_length;

static void do_ioctl_tests() {
    btrfs_ioctl_encoded_io_args enc;
    struct iovec iov;
    int ret;
    unique_fd fd;

    ret = open("test", O_CREAT | O_WRONLY, 0644);
    if (ret < 0)
        throw runtime_error("open failed");
    fd.reset(ret);

    iov.iov_base = &dump_bookend_zstd;
    iov.iov_len = dump_bookend_zstd_length;

    enc.iov = &iov;
    enc.iovcnt = 1;
    enc.offset = 0;
    enc.flags = 0;
    enc.len = 0x17eae;
    enc.unencoded_len = 0x1b000;
    enc.unencoded_offset = 0x1000;
    enc.compression = BTRFS_ENCODED_IO_COMPRESSION_ZSTD;
    enc.encryption = 0;
    memset(&enc.reserved, 0, sizeof(enc.reserved));

    ret = ioctl(fd.get(), BTRFS_IOC_ENCODED_WRITE, &enc);

    cout << "ret = " << ret << " (errno = " << errno << ")" << endl;
}

int main() {
    try {
        do_ioctl_tests();
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
    }

    return 0;
}
