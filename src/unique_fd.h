#include <unistd.h>

class unique_fd {
public:
    unique_fd() : fd(0) {
    }

    explicit unique_fd(int fd) : fd(fd) {
    }

    unique_fd(unique_fd&& that) noexcept {
        fd = that.fd;
        that.fd = 0;
    }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

    unique_fd& operator=(unique_fd&& that) noexcept {
        if (fd > 0)
            close(fd);

        fd = that.fd;
        that.fd = 0;

        return *this;
    }

    ~unique_fd() {
        if (fd <= 0)
            return;

        close(fd);
    }

    explicit operator bool() const noexcept {
        return fd != 0;
    }

    void reset(int new_fd = 0) noexcept {
        if (fd > 0)
            close(fd);

        fd = new_fd;
    }

    int get() const noexcept {
        return fd;
    }

private:
    int fd;
};
