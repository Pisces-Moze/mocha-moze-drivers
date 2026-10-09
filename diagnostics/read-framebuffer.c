/* SPDX-License-Identifier: GPL-2.0-only */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

/* Snapshot the confirmed boot framebuffer. No memory or register writes. */
int main(void) {
    const size_t bytes = 1536U * 2048U * 2U;
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { perror("open"); return 1; }
    const uint8_t *fb = mmap(NULL, bytes, PROT_READ, MAP_SHARED, fd, 0xf1700000ULL);
    if (fb == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
    size_t sent = 0;
    while (sent < bytes) {
        ssize_t n = write(STDOUT_FILENO, fb + sent, bytes - sent);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { perror("write"); return 1; }
        sent += (size_t)n;
    }
    munmap((void *)fb, bytes);
    close(fd);
    return 0;
}
