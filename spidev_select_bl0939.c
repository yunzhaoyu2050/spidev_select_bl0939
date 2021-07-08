#include <fcntl.h>
#include <getopt.h>
#include <linux/spi/spidev.h>
#include <linux/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#include <stdlib.h>
#include <string.h>

#include <sys/time.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void pabort(const char *s) {
    perror(s);
    abort();
}

static const char *device = "/dev/spidev32766.1";
static uint32_t mode;
static uint8_t bits = 8;
static uint32_t speed = 800000;
static uint16_t delay;

static unsigned int g_st_time = 20;  // ms, default

uint32_t g_bvs_rtm_c = 0;

#define MAX_ELEM_SIZE 20  // 20 elem

struct bl0939_val_st_tmp {
    uint32_t chA[MAX_ELEM_SIZE];
    uint32_t chB[MAX_ELEM_SIZE];
    uint32_t vtg;
    int indexA;
    int indexB;
};
struct bl0939_val_st_tmp bvst;

// -------------------------------------------------------------bl0939 func start

static uint32_t bl0939_read_reg(int fd, uint8_t reg) {
    uint8_t tx[2] = {0x55, reg};
    uint8_t rx[4] = {0};

    struct spi_ioc_transfer tr[2] = {{
                                         .tx_buf = (unsigned long)tx,
                                         // .rx_buf = (unsigned long)rx,
                                         .len = ARRAY_SIZE(tx),
                                         .delay_usecs = delay,
                                         .speed_hz = speed,
                                         .bits_per_word = bits,
                                     },
                                     {
                                         // .tx_buf = (unsigned long)tx,
                                         .rx_buf = (unsigned long)rx,
                                         .len = ARRAY_SIZE(rx),
                                         .delay_usecs = delay,
                                         .speed_hz = speed,
                                         .bits_per_word = bits,
                                     }};

    int ret = ioctl(fd, SPI_IOC_MESSAGE(2), &tr);
    if (ret < 1)
        pabort("can't send spi message");

    uint32_t val = 0;
    // if(rx_buf[5] == (uint8_t)~(0x55+reg+rx_buf[2]+rx_buf[3]+rx_buf[4]))
    { val = (uint32_t)rx[0] << 16 | (uint32_t)rx[1] << 8 | (uint32_t)rx[2] << 0; }
    return val;
}
static int bl0939_write_reg(int fd, uint8_t reg, uint32_t val, int check) {
    static uint32_t r_temp = 0;
    uint8_t h = val >> 16;
    uint8_t m = val >> 8;
    uint8_t l = val >> 0;
    uint8_t tx[6] = {0xA5, reg, h, m, l, ~(0XA5 + reg + h + m + l)};
    struct spi_ioc_transfer tr[1] = {{
        .tx_buf = (unsigned long)tx,
        // .rx_buf = (unsigned long)rx,
        .len = ARRAY_SIZE(tx),
        .delay_usecs = delay,
        .speed_hz = speed,
        .bits_per_word = bits,
    }};

    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 1)
        pabort("can't send spi message");
    if (0 == check)
        return 0;
    r_temp = bl0939_read_reg(fd, reg);
    if (r_temp == val)
        return 0;
    return 1;
}
static void bl0939_spi_reset(int fd) {
    uint8_t tx[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    struct spi_ioc_transfer tr[1] = {{
        .tx_buf = (unsigned long)tx,
        // .rx_buf = (unsigned long)rx,
        .len = ARRAY_SIZE(tx),
        .delay_usecs = delay,
        .speed_hz = speed,
        .bits_per_word = bits,
    }};

    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 1)
        pabort("can't send spi message");
}
static void bl0939_reset(int fd) {
    bl0939_spi_reset(fd);
    bl0939_write_reg(fd, 0x19, 0x005a5a5a, 0);  //复位用户寄存器
    bl0939_write_reg(fd, 0x1a, 0x00000055, 1);  //解除写保护
    bl0939_write_reg(fd, 0x10, 0xffff, 0);  // Threshold A
    bl0939_write_reg(fd, 0x1E, 0xffff, 1);  // Threshold B
    // B 通道漏电/过流报警输出指示管脚为 I_leak，无需配置即可直接输出。
    // A 通道漏电/过流报警输出指示引脚为 CF，需先设置 MODE[12]=1，再设置 TPS_CTRL[14]=1
    //高有效
    bl0939_write_reg(fd, 0x18, 0x00002000, 1);  // cf
    bl0939_write_reg(fd, 0x1B, 0x000047ff, 0);  // cf
    bl0939_write_reg(fd, 0x1a, 0x00000000, 1);  //写保护
}
static uint32_t bl0939_get_current_A(int fd) {
    uint32_t Ia = bl0939_read_reg(fd, 0x00);
    return Ia;
}
static uint32_t bl0939_get_current_B(int fd) {
    uint32_t Ib = bl0939_read_reg(fd, 0x07);
    return Ib;
}
static uint32_t bl0939_get_voltage(int fd) {
    uint32_t v = bl0939_read_reg(fd, 0x06);
    return v;
}
// -------------------------------------------------------------bl0939 func end

static void print_usage(const char *prog) {
    printf("Usage: %s [-Dth]\n", prog);
    puts(
        "  -D --device   device to use (default /dev/spidev32766.1)\n"
        "  -t --time     timer timing value (default 20ms)\n"
        "  -h            help\n");
    exit(1);
}

static void parse_opts(int argc, char *argv[]) {
    while (1) {
        static const struct option lopts[] = {
            {"device", 1, 0, 'D'}, {"time", 1, 0, 't'}, {NULL, 0, 0, 0},
        };
        int c;
        c = getopt_long(argc, argv, "D:t:h", lopts, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'D':
            device = optarg;
            break;
        case 't':
            g_st_time = atoi(optarg);
            break;
        default:
            print_usage(argv[0]);
            break;
        }
    }
}

static inline uint64_t timespec2ms(struct timespec *tp) {
    return ((tp->tv_sec) * 1000 + (tp->tv_nsec) / 1000000);
}

static inline void insterData(uint32_t targetVal, int *index, uint32_t *chaT, uint32_t chaTLen) {
    int i;
    if (*index >= chaTLen) {
        for (i = 0; i < chaTLen - 1; i++) {
            chaT[i] = chaT[i + 1];
        }
        *index = chaTLen - 1;
    }
    chaT[(*index)++] = targetVal;
}

static void printfOutData(uint32_t indexA, uint32_t indexB, uint32_t *chA, uint32_t *chB,
                          uint32_t vtg) {
    int i;
    char out[1024];
    int offset = sprintf(out, "[[");
    indexA = (indexA > 1024 ? 1024 : indexA);
    indexB = (indexB > 1024 ? 1024 : indexB);
    for (i = 0; i < indexA; i++) {
        offset += sprintf(out + offset, "%d,", chA[i]);
    }
    offset -= 1; // 清除最后一个 ,
    offset += sprintf(out + offset, "],[");
    for (i = 0; i < indexB; i++) {
        offset += sprintf(out + offset, "%d,", chB[i]);
    }
    offset -= 1;
    offset += sprintf(out + offset, "],%d]\n", vtg);
    fprintf(stdout, "%s", out);
    fflush(stdout);
}

static inline void clearOutData(void) {
    memset(&bvst, 0, sizeof(bvst));
}

/* main */
int main(int argc, char *argv[]) {
    int ret = 0;
    int fd;

    parse_opts(argc, argv);

    fd = open(device, O_RDWR);
    if (fd < 0)
        pabort("can't open device");

    mode |= SPI_CPHA;  // bl0939 default mode 1

    /*
     * spi mode
     */
    ret = ioctl(fd, SPI_IOC_WR_MODE32, &mode);
    if (ret == -1)
        pabort("can't set spi mode");

    ret = ioctl(fd, SPI_IOC_RD_MODE32, &mode);
    if (ret == -1)
        pabort("can't get spi mode");

    /*
     * bits per word
     */
    ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    if (ret == -1)
        pabort("can't set bits per word");

    ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
    if (ret == -1)
        pabort("can't get bits per word");

    /*
     * max speed hz
     */
    ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret == -1)
        pabort("can't set max speed hz");

    ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
    if (ret == -1)
        pabort("can't get max speed hz");

    struct timeval timeout;
    uint8_t keep_run = 0;
    g_bvs_rtm_c = 0;
    while (1) {
        unsigned long uSec = g_st_time * 1000;
        timeout.tv_sec = uSec / 1000000;
        timeout.tv_usec = uSec % 1000000;
        keep_run = 1;

        while (keep_run) {
            // tempfd=recvfd;
            int ret = select(0, NULL, NULL, NULL, &timeout);
            if (-1 == ret) {
                perror("select error");
                continue;
            } else if (0 == ret) {
                g_bvs_rtm_c++;

                struct timespec tpstart;
                struct timespec tpend;
                int i;
                clock_gettime(CLOCK_MONOTONIC, &tpstart);
                uint32_t chA_val = bl0939_get_current_A(fd);
                uint32_t chB_val = bl0939_get_current_B(fd);
                if (chA_val > 0) {
                    insterData(chA_val, &bvst.indexA, bvst.chA, MAX_ELEM_SIZE);
                }
                if (chB_val > 0) {
                    insterData(chB_val, &bvst.indexB, bvst.chB, MAX_ELEM_SIZE);
                }
                if (g_bvs_rtm_c >= 10) {  // 200ms
                    uint32_t vtg_val = bl0939_get_voltage(fd);
                    if (vtg_val > 0) {
                        bvst.vtg = vtg_val;
                    }
                    printfOutData(bvst.indexA, bvst.indexB, bvst.chA, bvst.chB, bvst.vtg);
                    clearOutData();
                    g_bvs_rtm_c = 0;
                }
                clock_gettime(CLOCK_MONOTONIC, &tpend);
                int64_t diff = timespec2ms(&tpstart) + g_st_time - timespec2ms(&tpend);  // ms
                if (diff > 0) {
                    uSec = diff * 1000;
                    timeout.tv_sec = uSec / 1000000;
                    timeout.tv_usec = uSec % 1000000;
#if DEBUG
                    printf("[bl0939] run . diff:%lld, g_st_time:%u.\r\n", diff, g_st_time);
                    fflush(stdout);
#endif
                } else {
                    uSec = g_st_time * 1000;  // default 20 ms
                    timeout.tv_sec = uSec / 1000000;
                    timeout.tv_usec = uSec % 1000000;
#if DEBUG
                    printf("[bl0939] spi func timout. diff:%lld.\r\n", diff);
                    fflush(stdout);
#endif
                    // diff < 0
                    chA_val = bl0939_get_current_A(fd);
                    chB_val = bl0939_get_current_B(fd);
                    uint8_t tc = abs(diff) / g_st_time;
                    for (i = 0; i < tc; i++) {  // 超时数据
                        g_bvs_rtm_c++;
                        if (chA_val > 0) {
                            insterData(chA_val, &bvst.indexA, bvst.chA, MAX_ELEM_SIZE);
                        }
                        if (chB_val > 0) {
                            insterData(chB_val, &bvst.indexB, bvst.chB, MAX_ELEM_SIZE);
                        }
                        if (g_bvs_rtm_c >= 10) {  // 200ms
#if DEBUG
                            printf("[bl0939] timeout >200ms. tc:%u. g_bvs_rtm_c:%d\r\n", tc,
                                   g_bvs_rtm_c);
                            fflush(stdout);
#endif
                            uint32_t vtg_val = bl0939_get_voltage(fd);
                            if (vtg_val > 0) {
                                bvst.vtg = vtg_val;
                            }
                            printfOutData(bvst.indexA, bvst.indexB, bvst.chA, bvst.chB, bvst.vtg);
                            clearOutData();
                            g_bvs_rtm_c = 0;
                        }
                    }
#if DEBUG
                    printf("[bl0939] re inster. tc:%u.abs:%d\r\n", tc, abs(diff));
#endif
                    // keep_run = 0;
                }
            }
        }
        // is reinit?
        bl0939_reset(fd);
    }
    // clear
    close(fd);

    return ret;
}