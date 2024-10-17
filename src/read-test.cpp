#include <iostream>
#include <format>
#include <string.h>
#include <sys/mman.h>
#include <liburing.h>
#include <btrfs/ioctl.h>
#include "unique_fd.h"

static constexpr uint64_t READ_BLOCK_SIZE = 4096;
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

static void do_test(int iou, const string& fn) {
    int ret;
    unique_fd fd;
    unsigned int tail, next_tail, index;

    ret = open(fn.c_str(), O_RDONLY | O_DIRECT);
    if (ret < 0)
        throw runtime_error("open failed");
    fd.reset(ret);

    auto size = file_size(fd.get());

    unsigned int num_blocks = size / READ_BLOCK_SIZE;

    if (size % READ_BLOCK_SIZE)
        num_blocks++;

    cout << format("fd = {}, file size = {}, num_blocks = {}\n",
                   fd.get(), size, num_blocks);

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
        sqe.fd = fd.get();
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
    if (ret < 0)
        throw runtime_error("io_uring_enter failed");

    cout << "sent" << endl;
    fflush(stdout);
}

static void read_from_cq() {
    unsigned int head = *cring_head;

    do {
        read_barrier();

        if (head == *cring_tail)
            break;

        const auto& cqe = cqes[head & *cring_mask];

        cout << format("cqe.res = {}, cqe.user_data = 0x{:x}\n", cqe.res, cqe.user_data);

        if (cqe.user_data) {
            auto& ctx = *(read_ctx*)cqe.user_data;

            cout << format("len {}, unencoded_len {}, unencoded_offset {}, compression {}, encryption {}\n",
                           ctx.enc.len, ctx.enc.unencoded_len, ctx.enc.unencoded_offset,
                           ctx.enc.compression, ctx.enc.encryption);

            if (cqe.res >= 0)
                cout << string_view(ctx.buf, cqe.res) << endl;

            delete &ctx;
        }

        head++;
    } while (true);

    *cring_head = head;
    write_barrier();
}

int main(int argc, char* argv[]) {
    try {
        auto iou = init_io_uring();

        for (int i = 1; i < argc; i++) {
            do_test(iou.get(), argv[i]);

            cout << "waiting" << endl;
            fflush(stdout);

            auto ret = io_uring_enter(iou.get(), 0, 1, IORING_ENTER_GETEVENTS, nullptr);
            if (ret < 0)
                throw runtime_error("io_uring_enter failed");

            read_from_cq();
        }
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
    }

    return 0;
}
