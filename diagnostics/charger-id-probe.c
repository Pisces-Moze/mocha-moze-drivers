/* SPDX-License-Identifier: GPL-2.0-only
 * Read only the documented vendor/part/revision register (0x0a).
 * Do not scan the bus or change charge settings.
 */
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    union i2c_smbus_data data;
    struct i2c_smbus_ioctl_data transaction = {
        .read_write = I2C_SMBUS_READ,
        .command = 0x0a,
        .size = I2C_SMBUS_BYTE_DATA,
        .data = &data,
    };
    int fd = open("/dev/i2c-1", O_RDWR | O_CLOEXEC);
    int value;

    if (fd < 0) {
        perror("open i2c-1");
        return 1;
    }
    /* The existing charger driver owns this address. This probe reads only
     * its immutable ID register; the I2C adapter serializes the transaction.
     */
    if (ioctl(fd, I2C_SLAVE_FORCE, 0x6b) < 0 ||
        ioctl(fd, I2C_SMBUS, &transaction) < 0) {
        perror("read charger ID");
        close(fd);
        return 1;
    }
    value = data.byte;
    printf("BQ2419X_REG0A=0x%02x\nPART_NUMBER=%d\nREVISION=%d\n",
           value, (value >> 3) & 7, value & 3);
    close(fd);
    return 0;
}
