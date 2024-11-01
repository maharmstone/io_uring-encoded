#include <iostream>
#include <span>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <btrfs/ioctl.h>
#include "unique_fd.h"

using namespace std;

extern uint8_t dump_normal;
extern uint64_t dump_normal_length;
extern uint8_t dump_bookend_zstd;
extern uint64_t dump_bookend_zstd_length;

struct test_item {
    const char* name;
    span<const uint8_t> data;
    uint64_t len;
    uint64_t unencoded_len;
    uint64_t unencoded_offset;
    uint32_t compression;
};

static const test_item test_items[] = {
    { "bookend-zstd.txt", span(&dump_bookend_zstd, dump_bookend_zstd_length), dump_normal_length, 0x1b000, 0x1000, BTRFS_ENCODED_IO_COMPRESSION_ZSTD },
};

static void do_ioctl_tests() {
    for (const auto& i : test_items) {
        btrfs_ioctl_encoded_io_args enc;
        struct iovec iov;
        int ret;
        unique_fd fd;

        ret = open(i.name, O_CREAT | O_WRONLY, 0644);
        if (ret < 0)
            throw runtime_error("open failed");
        fd.reset(ret);

        iov.iov_base = (void*)i.data.data();
        iov.iov_len = i.data.size();

        enc.iov = &iov;
        enc.iovcnt = 1;
        enc.offset = 0;
        enc.flags = 0;
        enc.len = i.len;
        enc.unencoded_len = i.unencoded_len;
        enc.unencoded_offset = i.unencoded_offset;
        enc.compression = i.compression;
        enc.encryption = 0;
        memset(&enc.reserved, 0, sizeof(enc.reserved));

        ret = ioctl(fd.get(), BTRFS_IOC_ENCODED_WRITE, &enc);

        cout << "ret = " << ret << " (errno = " << errno << ")" << endl;

        // FIXME - check ret
        // FIXME - contents of file
    }
}

int main() {
    try {
        do_ioctl_tests();
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
    }

    return 0;
}
