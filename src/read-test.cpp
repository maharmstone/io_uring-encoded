#include <iostream>
#include <format>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <liburing.h>
#include <btrfs/ioctl.h>
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

struct read_ctx {
    char buf[131072];
    struct iovec iov;
    btrfs_ioctl_encoded_io_args enc;
};

static uint64_t file_size(int fd) {
    struct stat st;

    if (fstat(fd, &st) < 0)
        throw runtime_error("stat failed");

    return st.st_size;
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
extern uint8_t dump_zero_sector;
extern uint64_t dump_zero_sector_length;

static constexpr uint64_t round_up(uint64_t n) {
    if (n & 0xfff)
        n = (n & ~0xfff) + 0x1000;

    return n;
}

struct test_item {
    const char* name;
    span<const uint8_t> data;
    uint64_t len;
    uint64_t unencoded_len;
    uint64_t unencoded_offset;
    uint32_t compression;
};

static const test_item test_items[] = {
    { "normal.txt", span(&dump_normal, dump_normal_length), dump_normal_length, dump_normal_length, 0, 0 },
    { "zlib.txt", span(&dump_zlib, dump_zlib_length), dump_normal_length, round_up(dump_normal_length), 0, 1 },
    { "lzo.txt", span(&dump_lzo, dump_lzo_length), dump_normal_length, round_up(dump_normal_length), 0, 3 },
    { "zstd.txt", span(&dump_zstd, dump_zstd_length), dump_normal_length, round_up(dump_normal_length), 0, 2 },
    { "inline.txt", span(&dump_inline, dump_inline_length), dump_inline_length, dump_inline_length, 0, 0 },
    { "inline-zlib.txt", span(&dump_inline_zlib, dump_inline_zlib_length), dump_inline_length, dump_inline_length, 0, 1 },
    { "inline-lzo.txt", span(&dump_inline_lzo, dump_inline_lzo_length), dump_inline_length, dump_inline_length, 0, 3 },
    { "inline-zstd.txt", span(&dump_inline_zstd, dump_inline_zstd_length), dump_inline_length, dump_inline_length, 0, 2 },
    { "prealloc.txt", span(&dump_zero_sector, dump_zero_sector_length), dump_zero_sector_length, dump_zero_sector_length, 0, 0 },
};

static void do_ioctl_tests() {
    for (const auto& i : test_items) {
        int ret;
        unique_fd fd;
        char buf[131072];
        struct iovec iov;
        btrfs_ioctl_encoded_io_args enc;

        ret = open(i.name, O_RDONLY);
        if (ret < 0)
            throw runtime_error("open failed");
        fd.reset(ret);

        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);

        enc.iov = &iov;
        enc.iovcnt = 1;
        enc.offset = 0;
        enc.flags = 0;

        ret = ioctl(fd.get(), BTRFS_IOC_ENCODED_READ, &enc);

        if (ret < 0)
            cerr << format("{}: ioctl failed (ret {}, errno {})\n", i.name, ret, errno);
        else if ((size_t)ret != i.data.size())
            cerr << format("{}: ioctl returned {}, expected {}\n", i.name, ret, i.data.size());

        if ((size_t)ret == i.data.size()) {
            bool okay = true;

            if (enc.len != i.len) {
                cerr << format("{}: ioctl enc.len was {}, expected {}\n", i.name, enc.len, i.len);
                okay = false;
            }

            if (enc.unencoded_len != i.unencoded_len) {
                cerr << format("{}: ioctl enc.unencoded_len was {}, expected {}\n", i.name, enc.unencoded_len, i.unencoded_len);
                okay = false;
            }

            if (enc.unencoded_offset != i.unencoded_offset) {
                cerr << format("{}: ioctl enc.unencoded_offset was {}, expected {}\n", i.name, enc.unencoded_offset, i.unencoded_offset);
                okay = false;
            }

            if (enc.compression != i.compression) {
                cerr << format("{}: ioctl enc.compression was {}, expected {}\n", i.name, enc.compression, i.compression);
                okay = false;
            }

            if (enc.encryption != 0) {
                cerr << format("{}: ioctl enc.encryption was {}, expected 0\n", i.name, enc.encryption);
                okay = false;
            }

            if (memcmp(buf, i.data.data(), i.data.size())) {
                cerr << format("{}: ioctl data doesn't match what was expected\n", i.name);
                okay = false;
            }

            if (okay)
                cout << format("{}: ioctl okay\n", i.name);
        }
    }
}

static void sq_encoded_read(int iou, int fd) {
    int ret;
    unsigned int tail, next_tail, index;

    auto ctx = new read_ctx;

    {
        tail = *sring_tail;
        next_tail = tail + 1;

        read_barrier();
        index = tail & *sring_mask;

        ctx->iov.iov_base = ctx->buf;
        ctx->iov.iov_len = sizeof(ctx->buf);

        ctx->enc.iov = &ctx->iov;
        ctx->enc.iovcnt = 1;
        ctx->enc.offset = 0;
        ctx->enc.flags = 0;

        auto& sqe = sqes[index];
        sqe.fd = fd;
        sqe.flags = 0;
        sqe.opcode = IORING_OP_URING_CMD;
        sqe.addr = (uintptr_t)&ctx->enc;
        sqe.len = sizeof(ctx->enc);
        sqe.cmd_op = BTRFS_IOC_ENCODED_READ;
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

static void cq_encoded_read(int iou, const test_item& i) {
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
            auto& ctx = *(read_ctx*)cqe.user_data;

            if ((size_t)cqe.res == i.data.size()) {
                const auto& enc = ctx.enc;
                bool okay = true;

                if (enc.len != i.len) {
                    cerr << format("{}: io_uring enc.len was {}, expected {}\n", i.name, enc.len, i.len);
                    okay = false;
                }

                if (enc.unencoded_len != i.unencoded_len) {
                    cerr << format("{}: io_uring enc.unencoded_len was {}, expected {}\n", i.name, enc.unencoded_len, i.unencoded_len);
                    okay = false;
                }

                if (enc.unencoded_offset != i.unencoded_offset) {
                    cerr << format("{}: io_uring enc.unencoded_offset was {}, expected {}\n", i.name, enc.unencoded_offset, i.unencoded_offset);
                    okay = false;
                }

                if (enc.compression != i.compression) {
                    cerr << format("{}: io_uring enc.compression was {}, expected {}\n", i.name, enc.compression, i.compression);
                    okay = false;
                }

                if (enc.encryption != 0) {
                    cerr << format("{}: io_uring enc.encryption was {}, expected 0\n", i.name, enc.encryption);
                    okay = false;
                }

                if (memcmp(ctx.buf, i.data.data(), i.data.size())) {
                    cerr << format("{}: ioctl data doesn't match what was expected\n", i.name);
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

        ret = open(i.name, O_RDONLY);
        if (ret < 0)
            throw runtime_error("open failed");
        fd.reset(ret);

        sq_encoded_read(iou.get(), fd.get());
        cq_encoded_read(iou.get(), i);
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
