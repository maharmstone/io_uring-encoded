#include <iostream>
#include <span>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <btrfs/ioctl.h>
#include <sys/mman.h>
#include <liburing.h>
#include "unique_fd.h"

static constexpr uint32_t QUEUE_DEPTH = 3;

#define read_barrier()  __asm__ __volatile__("":::"memory")
#define write_barrier() __asm__ __volatile__("":::"memory")

using namespace std;

static void* sq_ptr;
static struct io_uring_sqe* sqes;
static unsigned int* sring_tail;
static unsigned int* sring_mask;
static unsigned int* sring_array;
static unsigned int* cring_head;
static unsigned int* cring_tail;
static unsigned int* cring_mask;
static struct io_uring_cqe* cqes;

extern uint8_t dump_normal;
extern uint64_t dump_normal_length;
extern uint8_t dump_zlib;
extern uint64_t dump_zlib_length;
extern uint8_t dump_lzo;
extern uint64_t dump_lzo_length;
extern uint8_t dump_zstd;
extern uint64_t dump_zstd_length;
extern uint8_t dump_inline;
extern uint64_t dump_inline_length;
extern uint8_t dump_inline_zlib;
extern uint64_t dump_inline_zlib_length;
extern uint8_t dump_inline_lzo;
extern uint64_t dump_inline_lzo_length;
extern uint8_t dump_inline_zstd;
extern uint64_t dump_inline_zstd_length;
extern uint8_t dump_bookend_zlib;
extern uint64_t dump_bookend_zlib_length;
extern uint8_t dump_bookend_lzo;
extern uint64_t dump_bookend_lzo_length;
extern uint8_t dump_bookend_zstd;
extern uint64_t dump_bookend_zstd_length;

struct write_ctx {
    struct iovec iov;
    btrfs_ioctl_encoded_io_args enc;
};

static constexpr uint64_t round_up(uint64_t n) {
    if (n & 0xfff)
        n = (n & ~0xfff) + 0x1000;

    return n;
}

struct test_item {
    const char* name;
    span<const uint8_t> data;
    span<const uint8_t> unencoded_data;
    uint64_t unencoded_len;
    uint64_t unencoded_offset;
    uint32_t compression;
};

static const test_item test_items[] = {
    { "zlib.txt", span(&dump_zlib, dump_zlib_length), span(&dump_normal, dump_normal_length), round_up(dump_normal_length), 0, BTRFS_ENCODED_IO_COMPRESSION_ZLIB },
    { "lzo.txt", span(&dump_lzo, dump_lzo_length), span(&dump_normal, dump_normal_length), round_up(dump_normal_length), 0, BTRFS_ENCODED_IO_COMPRESSION_LZO_4K },
    { "zstd.txt", span(&dump_zstd, dump_zstd_length), span(&dump_normal, dump_normal_length), round_up(dump_normal_length), 0, BTRFS_ENCODED_IO_COMPRESSION_ZSTD },
    { "inline-zlib.txt", span(&dump_inline_zlib, dump_inline_zlib_length), span(&dump_inline, dump_inline_length), dump_inline_length, 0, BTRFS_ENCODED_IO_COMPRESSION_ZLIB },
    { "inline-lzo.txt", span(&dump_inline_lzo, dump_inline_lzo_length), span(&dump_inline, dump_inline_length), dump_inline_length, 0, BTRFS_ENCODED_IO_COMPRESSION_LZO_4K },
    { "inline-zstd.txt", span(&dump_inline_zstd, dump_inline_zstd_length), span(&dump_inline, dump_inline_length), dump_inline_length, 0, BTRFS_ENCODED_IO_COMPRESSION_ZSTD },
    { "bookend-zlib.txt", span(&dump_bookend_zlib, dump_bookend_zlib_length), span(&dump_normal, dump_normal_length), round_up(dump_normal_length) + 0x3000, 0x1000, BTRFS_ENCODED_IO_COMPRESSION_ZLIB },
    { "bookend-lzo.txt", span(&dump_bookend_lzo, dump_bookend_lzo_length), span(&dump_normal, dump_normal_length), round_up(dump_normal_length) + 0x3000, 0x1000, BTRFS_ENCODED_IO_COMPRESSION_LZO_4K },
    { "bookend-zstd.txt", span(&dump_bookend_zstd, dump_bookend_zstd_length), span(&dump_normal, dump_normal_length), round_up(dump_normal_length) + 0x3000, 0x1000, BTRFS_ENCODED_IO_COMPRESSION_ZSTD },
};

static void do_ioctl_tests() {
    for (const auto& i : test_items) {
        btrfs_ioctl_encoded_io_args enc;
        struct iovec iov;
        int ret;
        unique_fd fd;

        ret = open(i.name, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (ret < 0)
            throw runtime_error("open failed for writing");
        fd.reset(ret);

        iov.iov_base = (void*)i.data.data();
        iov.iov_len = i.data.size();

        enc.iov = &iov;
        enc.iovcnt = 1;
        enc.offset = 0;
        enc.flags = 0;
        enc.len = i.unencoded_data.size();
        enc.unencoded_len = i.unencoded_len;
        enc.unencoded_offset = i.unencoded_offset;
        enc.compression = i.compression;
        enc.encryption = 0;
        memset(&enc.reserved, 0, sizeof(enc.reserved));

        ret = ioctl(fd.get(), BTRFS_IOC_ENCODED_WRITE, &enc);

        if (ret < 0)
            cerr << format("{}: ioctl failed (ret {}, errno {})\n", i.name, ret, errno);
        else if ((size_t)ret != i.data.size())
            cerr << format("{}: ioctl returned {}, expected {}\n", i.name, ret, i.data.size());

        if ((size_t)ret == i.data.size()) {
            bool okay = true;
            uint8_t buf[131072];

            fd.reset();

            ret = open(i.name, O_RDONLY);
            if (ret < 0)
                throw runtime_error("open failed for reading");
            fd.reset(ret);

            ret = read(fd.get(), buf, i.unencoded_data.size());
            if (ret < 0) {
                throw runtime_error(format("{}: read returned {}, expected {}",
                                           i.name, ret, i.unencoded_data.size()));
            }

            if (memcmp(i.unencoded_data.data(), buf, i.unencoded_data.size())) {
                cerr << format("{}: file contents differ\n", i.name);
                okay = false;
            }

            if (okay)
                cout << format("{}: ioctl okay\n", i.name);
        }
    }
}

int io_uring_setup(unsigned int entries, struct io_uring_params *p) {
    int ret;
    ret = syscall(__NR_io_uring_setup, entries, p);
    return (ret < 0) ? -errno : ret;
}

int io_uring_enter(unsigned int fd, unsigned int to_submit,
                   unsigned int min_complete, unsigned int flags,
                   sigset_t *sig) {
    int ret;
    ret = syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags,
                  sig, _NSIG / 8);
    return (ret < 0) ? -errno : ret;
}

static unique_fd init_io_uring() {
    int ret;
    unique_fd iou;
    struct io_uring_params p;

    memset(&p, 0, sizeof(p));
    ret = io_uring_setup(QUEUE_DEPTH, &p);
    if (ret < 0)
        throw runtime_error("io_uring_setup failed");
    iou.reset(ret);

    if (!(p.features & IORING_FEAT_SINGLE_MMAP))
        throw runtime_error("IORING_FEAT_SINGLE_MMAP not set");

    unsigned int sz = max(p.sq_off.array + p.sq_entries * sizeof(uint32_t),
                          p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe));

    // FIXME - munmap
    sq_ptr = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                  iou.get(), IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED)
        throw runtime_error("mmap failed");

    // FIXME - munmap
    sqes = (io_uring_sqe*)mmap(0, p.sq_entries * sizeof(struct io_uring_sqe),
                               PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                               iou.get(), IORING_OFF_SQES);
    if (sq_ptr == MAP_FAILED)
        throw runtime_error("mmap failed");

    sring_tail = (unsigned int*)((uint8_t*)sq_ptr + p.sq_off.tail);
    sring_mask = (unsigned int*)((uint8_t*)sq_ptr + p.sq_off.ring_mask);
    sring_array = (unsigned int*)((uint8_t*)sq_ptr + p.sq_off.array);

    cring_head = (unsigned int*)((uint8_t*)sq_ptr + p.cq_off.head);
    cring_tail = (unsigned int*)((uint8_t*)sq_ptr + p.cq_off.tail);
    cring_mask = (unsigned int*)((uint8_t*)sq_ptr + p.cq_off.ring_mask);
    cqes = (io_uring_cqe*)((uint8_t*)sq_ptr + p.cq_off.cqes);

    return iou;
}

static void sq_encoded_write(int iou, int fd, const test_item& i) {
    int ret;
    unsigned int tail, next_tail, index;

    auto ctx = new write_ctx;

    {
        tail = *sring_tail;
        next_tail = tail + 1;

        read_barrier();
        index = tail & *sring_mask;

        ctx->iov.iov_base = (void*)i.data.data();
        ctx->iov.iov_len = i.data.size();

        ctx->enc.iov = &ctx->iov;
        ctx->enc.iovcnt = 1;
        ctx->enc.offset = 0;
        ctx->enc.flags = 0;
        ctx->enc.len = i.unencoded_data.size();
        ctx->enc.unencoded_len = i.unencoded_len;
        ctx->enc.unencoded_offset = i.unencoded_offset;
        ctx->enc.compression = i.compression;
        ctx->enc.encryption = 0;
        memset(&ctx->enc.reserved, 0, sizeof(ctx->enc.reserved));

        auto& sqe = sqes[index];
        sqe.fd = fd;
        sqe.flags = 0;
        sqe.opcode = IORING_OP_URING_CMD;
        sqe.addr = (uintptr_t)&ctx->enc;
        sqe.len = sizeof(ctx->enc);
        sqe.cmd_op = BTRFS_IOC_ENCODED_WRITE;
        sqe.user_data = (uintptr_t)ctx;

        sring_array[index] = index;
        tail = next_tail;

        if (*sring_tail != tail) {
            *sring_tail = tail;
            write_barrier();
        }
    }

    ret = io_uring_enter(iou, 1, 0, 0, nullptr);
    if (ret < 0) {
        delete ctx;
        throw runtime_error("io_uring_enter failed");
    }
}

static void cq_encoded_write(int iou, const test_item& i) {
    auto ret = io_uring_enter(iou, 0, 1, IORING_ENTER_GETEVENTS, nullptr);
    if (ret < 0)
        throw runtime_error("io_uring_enter failed");

    unsigned int head = *cring_head;

    do {
        read_barrier();

        if (head == *cring_tail)
            break;

        const auto& cqe = cqes[head & *cring_mask];

        if (cqe.res < 0)
            cerr << format("{}: io_uring failed (res {})\n", i.name, cqe.res);
        else if ((size_t)cqe.res != i.data.size())
            cerr << format("{}: io_uring returned {}, expected {}\n", i.name, cqe.res, i.data.size());

        if (!cqe.user_data)
            cerr << format("{}: no user_data provided\n", i.name);
        else {
            auto& ctx = *(write_ctx*)cqe.user_data;

            if ((size_t)cqe.res == i.data.size()) {
                bool okay = true;
                uint8_t buf[131072];
                int ret;
                unique_fd fd;

                ret = open(i.name, O_RDONLY);
                if (ret < 0)
                    throw runtime_error("open failed for reading");
                fd.reset(ret);

                ret = read(fd.get(), buf, i.unencoded_data.size());
                if (ret < 0) {
                    throw runtime_error(format("{}: read returned {}, expected {}",
                                                i.name, ret, i.unencoded_data.size()));
                }

                if (memcmp(i.unencoded_data.data(), buf, i.unencoded_data.size())) {
                    cerr << format("{}: file contents differ\n", i.name);
                    okay = false;
                }

                if (okay)
                    cout << format("{}: io_uring okay\n", i.name);
            }

            delete &ctx;
        }

        head++;
    } while (true);

    *cring_head = head;
    write_barrier();
}

static void do_io_uring_tests() {
    auto iou = init_io_uring();

    for (const auto& i : test_items) {
        int ret;
        unique_fd fd;

        ret = open(i.name, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (ret < 0)
            throw runtime_error("open failed for writing");
        fd.reset(ret);

        sq_encoded_write(iou.get(), fd.get(), i);
        cq_encoded_write(iou.get(), i);
    }
}

int main() {
    try {
        do_ioctl_tests();
        do_io_uring_tests();
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
    }

    return 0;
}
