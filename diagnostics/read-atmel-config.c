// Read-only identification and configuration backup; skips message FIFO T5.
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int read_at(int fd, unsigned int address, unsigned char *data, unsigned int length)
{
    while (length) {
        unsigned char pointer[2] = {address & 255, address >> 8};
        unsigned int block = length > 128 ? 128 : length;
        struct i2c_msg messages[2] = {
            {.addr=0x4a,.len=2,.buf=pointer},
            {.addr=0x4a,.flags=I2C_M_RD,.len=block,.buf=data}
        };
        struct i2c_rdwr_ioctl_data request = {.msgs=messages,.nmsgs=2};
        if (ioctl(fd, I2C_RDWR, &request) != 2) { perror("read_at"); return -1; }
        address += block; data += block; length -= block;
    }
    return 0;
}

static void hex(const unsigned char *data, unsigned int length)
{
    for (unsigned int i=0; i<length; ++i) printf("%02x", data[i]);
}

int main(void)
{
    int fd = open("/dev/i2c-2", O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }
    unsigned char info[7], table[256*6], crc[3], buffer[65536];
    if (read_at(fd, 0, info, 7) || info[0] != 0xa4 || info[1] != 4) return 2;
    if (read_at(fd, 7, table, info[6]*6) || read_at(fd, 7+info[6]*6, crc, 3)) return 3;
    printf("{\"info\":\""); hex(info, 7);
    printf("\",\"info_crc\":\"%02x%02x%02x\",\"objects\":[",crc[2],crc[1],crc[0]);
    for (unsigned int i=0; i<info[6]; ++i) {
        unsigned char *entry=table+i*6;
        unsigned int address=entry[1]+entry[2]*256, size=entry[3]+1, instances=entry[4]+1;
        if (i) printf(",");
        printf("{\"type\":%u,\"address\":%u,\"size\":%u,\"instances\":%u,\"reports\":%u",entry[0],address,size,instances,entry[5]);
        if (entry[0]!=5 && entry[0]!=44) {
            if (read_at(fd,address,buffer,size*instances)) return 4;
            printf(",\"data\":\""); hex(buffer,size*instances); printf("\"");
        }
        printf("}");
    }
    puts("]}"); close(fd); return 0;
}
