/*
 * zk9500-probe.c — Diagnóstico USB y captura PGM para ZKTeco ZK9500 (1b55:0124)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Compilar:
 *   gcc -O2 -Wall -Wextra tools/zk9500-probe.c -o /tmp/zk9500-probe $(pkg-config --cflags --libs libusb-1.0)
 */

#include <errno.h>
#include <libusb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ZK9500_VID        0x1b55
#define ZK9500_PID        0x0124
#define ZK9500_EP_IMAGE   0x82
#define ZK9500_WIDTH      300
#define ZK9500_HEIGHT     375
#define ZK9500_FRAME_SIZE (ZK9500_WIDTH * ZK9500_HEIGHT)

#define REQ_START         0xE0
#define REQ_SYNC          0x80
#define REQ_GPIO_SET      0xE1
#define REQ_GPIO_GET      0xE2
#define REQ_SENSOR_REG    0xE3
#define REQ_EEPROM        0xE7
#define REQ_FINGER        0xEA

#define CTRL_TIMEOUT_MS   1000
#define BULK_TIMEOUT_MS   3000

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

typedef struct {
    uint16_t exposure;
    uint16_t gain_red;
    uint16_t gain_green1;
    uint16_t gain_green2;
    uint16_t gain_blue;
    uint8_t  leds[6];
    bool     from_eeprom;
} zk9500_settings_t;

static int ctrl_out(libusb_device_handle *h, uint8_t req, uint16_t val, uint16_t idx,
                    const uint8_t *data, uint16_t len) {
    uint8_t buf[64];
    if (len > sizeof(buf)) return LIBUSB_ERROR_INVALID_PARAM;
    if (len > 0 && data) memcpy(buf, data, len);
    int r = libusb_control_transfer(
        h,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
        req, val, idx, len ? buf : NULL, len, CTRL_TIMEOUT_MS);
    return (r < 0) ? r : 0;
}

static int ctrl_in(libusb_device_handle *h, uint8_t req, uint16_t val, uint16_t idx,
                   uint8_t *data, uint16_t len) {
    return libusb_control_transfer(
        h,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
        req, val, idx, data, len, CTRL_TIMEOUT_MS);
}

static int read_eeprom_range(libusb_device_handle *h, uint8_t start, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t b = 0;
        int r = ctrl_in(h, REQ_EEPROM, 0, (uint16_t)(start + i), &b, 1);
        if (r != 1) return (r < 0) ? r : LIBUSB_ERROR_IO;
        out[i] = b;
    }
    return 0;
}

static zk9500_settings_t parse_settings(const uint8_t cam[12], const uint8_t leds[6]) {
    zk9500_settings_t def = {
        .exposure    = 1200,
        .gain_red    = 200,
        .gain_green1 = 200,
        .gain_green2 = 182,
        .gain_blue   = 200,
        .leds        = {200, 200, 175, 175, 120, 120},
        .from_eeprom = false,
    };
    uint16_t w[6];
    for (int i = 0; i < 6; i++) {
        w[i] = (uint16_t)cam[2 * i] | ((uint16_t)cam[2 * i + 1] << 8);
    }
    uint32_t total = (uint32_t)w[0] + w[1] + w[2] + w[3] + w[4];
    bool leds_ok = true;
    for (int i = 0; i < 6; i++) {
        if (leds[i] == 0) leds_ok = false;
    }
    if ((w[5] == total || w[5] == total + 1) && w[0] >= 100 && w[0] <= 10000 && leds_ok) {
        zk9500_settings_t s = {
            .exposure    = w[0],
            .gain_red    = w[1],
            .gain_green1 = w[2],
            .gain_green2 = w[3],
            .gain_blue   = w[4],
            .from_eeprom = true,
        };
        memcpy(s.leds, leds, 6);
        return s;
    }
    return def;
}

static int zk9500_start(libusb_device_handle *h, zk9500_settings_t *out_s) {
    int r = ctrl_out(h, REQ_START, 0, 0, NULL, 0);
    if (r < 0) return r;

    uint8_t zeros[16] = {0};
    r = ctrl_out(h, REQ_SYNC, 0, 0, zeros, sizeof(zeros));
    if (r < 0) return r;

    uint8_t ack[4] = {0};
    int transferred = 0;
    (void)libusb_bulk_transfer(h, ZK9500_EP_IMAGE, ack, sizeof(ack), &transferred, 500);

    uint8_t gpio[2] = {0};
    (void)ctrl_in(h, REQ_GPIO_GET, 0, 0x0055, gpio, sizeof(gpio));

    uint8_t cam[12] = {0}, leds[6] = {0};
    zk9500_settings_t s;
    if (read_eeprom_range(h, 0x30, cam, sizeof(cam)) == 0 &&
        read_eeprom_range(h, 0x70, leds, sizeof(leds)) == 0) {
        s = parse_settings(cam, leds);
    } else {
        uint8_t dummy_cam[12] = {0}, dummy_leds[6] = {0};
        s = parse_settings(dummy_cam, dummy_leds);
    }
    if (out_s) *out_s = s;

    for (uint16_t i = 0; i < 6; i++) {
        uint16_t v = ((uint16_t)s.leds[i] << 8) | 1;
        if ((r = ctrl_out(h, REQ_GPIO_SET, v, i, NULL, 0)) < 0) return r;
    }

    const struct { uint16_t r1, r2, val; } gains[4] = {
        {0xa5, 0xa6, s.gain_red},
        {0xa3, 0xa4, s.gain_green1},
        {0xa9, 0xaa, s.gain_green2},
        {0xa7, 0xa8, s.gain_blue},
    };
    for (int i = 0; i < 4; i++) {
        if ((r = ctrl_out(h, REQ_SENSOR_REG, gains[i].val, gains[i].r1, NULL, 0)) < 0) return r;
        if ((r = ctrl_out(h, REQ_SENSOR_REG, gains[i].val, gains[i].r2, NULL, 0)) < 0) return r;
    }
    if ((r = ctrl_out(h, REQ_SENSOR_REG, s.exposure >> 8, 3, NULL, 0)) < 0) return r;
    if ((r = ctrl_out(h, REQ_SENSOR_REG, s.exposure & 0xff, 4, NULL, 0)) < 0) return r;

    const struct { uint16_t idx, val; } en[4] = {
        {0x53, 1}, {0x32, 0}, {0x31, 1}, {0x30, 3}
    };
    for (int i = 0; i < 4; i++) {
        if ((r = ctrl_out(h, REQ_GPIO_SET, en[i].val, en[i].idx, NULL, 0)) < 0) return r;
    }
    return 0;
}

static void zk9500_stop(libusb_device_handle *h) {
    for (uint16_t i = 0; i < 6; i++) {
        (void)ctrl_out(h, REQ_GPIO_SET, 0, i, NULL, 0);
    }
    const uint16_t stop_regs[3] = {0x15, 0x30, 0x31};
    for (int i = 0; i < 3; i++) {
        (void)ctrl_out(h, REQ_GPIO_SET, 0, stop_regs[i], NULL, 0);
    }
}

static int finger_present(libusb_device_handle *h) {
    uint8_t b = 0;
    int r = ctrl_in(h, REQ_FINGER, 0, 0, &b, 1);
    if (r < 0) return r;
    return (r == 1 && b == 1) ? 1 : 0;
}

static int read_frame(libusb_device_handle *h, uint8_t *buf, int *chunks_out) {
    int got = 0, chunks = 0;
    while (got < ZK9500_FRAME_SIZE) {
        int xfer = 0;
        int r = libusb_bulk_transfer(h, ZK9500_EP_IMAGE, buf + got,
                                     ZK9500_FRAME_SIZE - got, &xfer, BULK_TIMEOUT_MS);
        if (r < 0) {
            if (got == 0) return r;
            break;
        }
        if (xfer == 0) break;
        got += xfer;
        chunks++;
    }
    if (chunks_out) *chunks_out = chunks;
    return got;
}

int main(int argc, char **argv) {
    int want = (argc > 1) ? atoi(argv[1]) : 1;
    int secs = (argc > 2) ? atoi(argv[2]) : 25;
    const char *out_prefix = (argc > 3) ? argv[3] : "/tmp/zk9500";

    signal(SIGINT, on_sigint);

    libusb_context *ctx = NULL;
    if (libusb_init(&ctx) != 0) {
        fprintf(stderr, "Error inicializando libusb\n");
        return 1;
    }

    libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, ZK9500_VID, ZK9500_PID);
    if (!h) {
        fprintf(stderr, "No se pudo abrir 1b55:0124 (ZK9500). Verifica permisos USB (udev / sudo).\n");
        libusb_exit(ctx);
        return 2;
    }

    if (libusb_kernel_driver_active(h, 0) == 1) {
        (void)libusb_detach_kernel_driver(h, 0);
    }
    int cfg = 0;
    if (libusb_get_configuration(h, &cfg) == 0 && cfg != 1) {
        (void)libusb_set_configuration(h, 1);
    }
    if (libusb_claim_interface(h, 0) != 0) {
        fprintf(stderr, "No se pudo reclamar la interfaz 0 del ZK9500\n");
        libusb_close(h);
        libusb_exit(ctx);
        return 3;
    }

    zk9500_settings_t s;
    int r = zk9500_start(h, &s);
    if (r < 0) {
        fprintf(stderr, "Error inicializando ZK9500: %s\n", libusb_strerror(r));
        libusb_release_interface(h, 0);
        libusb_close(h);
        libusb_exit(ctx);
        return 4;
    }

    printf("ZK9500 inicializado OK (%s): exp=%u R=%u G1=%u G2=%u B=%u LEDs=[%u,%u,%u,%u,%u,%u]\n",
           s.from_eeprom ? "EEPROM" : "defaults",
           s.exposure, s.gain_red, s.gain_green1, s.gain_green2, s.gain_blue,
           s.leds[0], s.leds[1], s.leds[2], s.leds[3], s.leds[4], s.leds[5]);

    if (want == 0) {
        printf("Modo solo-init (--want=0): probando estado de dedo sin tocar = %d\n", finger_present(h));
        zk9500_stop(h);
        libusb_release_interface(h, 0);
        libusb_close(h);
        libusb_exit(ctx);
        return 0;
    }

    printf("Coloca el dedo en el lector ZK9500 (%d captura(s), timeout %ds)...\n", want, secs);
    fflush(stdout);

    uint8_t *img = malloc(ZK9500_FRAME_SIZE);
    time_t deadline = time(NULL) + secs;
    int captured = 0;

    while (!g_stop && captured < want && time(NULL) < deadline) {
        int fp = finger_present(h);
        if (fp < 0) {
            fprintf(stderr, "Error leyendo presencia de dedo: %s\n", libusb_strerror(fp));
            break;
        }
        if (fp == 1) {
            int chunks = 0;
            int got = read_frame(h, img, &chunks);
            if (got == ZK9500_FRAME_SIZE) {
                captured++;
                uint64_t sum = 0, border_sum = 0, border_n = 0;
                for (int y = 0; y < ZK9500_HEIGHT; y++) {
                    for (int x = 0; x < ZK9500_WIDTH; x++) {
                        uint8_t px = img[y * ZK9500_WIDTH + x];
                        sum += px;
                        if (x < 12 || x >= ZK9500_WIDTH - 12 || y < 12 || y >= ZK9500_HEIGHT - 12) {
                            border_sum += px;
                            border_n++;
                        }
                    }
                }
                double mean = (double)sum / ZK9500_FRAME_SIZE;
                double var_sum = 0;
                for (int i = 0; i < ZK9500_FRAME_SIZE; i++) {
                    double d = img[i] - mean;
                    var_sum += d * d;
                }
                double sq_dev = var_sum / ZK9500_FRAME_SIZE;
                double border_mean = (double)border_sum / border_n;

                char path[256];
                snprintf(path, sizeof(path), "%s-%d.pgm", out_prefix, captured);
                FILE *f = fopen(path, "wb");
                if (f) {
                    fprintf(f, "P5\n%d %d\n255\n", ZK9500_WIDTH, ZK9500_HEIGHT);
                    fwrite(img, 1, ZK9500_FRAME_SIZE, f);
                    fclose(f);
                }
                printf("Captura #%d: %d bytes (%d chunks), media=%.1f, borde=%.1f, sq_dev=%.1f -> %s\n",
                       captured, got, chunks, mean, border_mean, sq_dev, path);
                fflush(stdout);

                if (captured < want) {
                    printf("  Esperando a que retires el dedo...\n");
                    fflush(stdout);
                    while (!g_stop && time(NULL) < deadline) {
                        usleep(80 * 1000);
                        int still = finger_present(h);
                        if (still <= 0) break;
                        (void)read_frame(h, img, NULL);
                    }
                    printf("  Dedo retirado. Coloca el dedo de nuevo...\n");
                    fflush(stdout);
                }
            }
        } else {
            usleep(60 * 1000);
        }
    }

    free(img);
    zk9500_stop(h);
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(ctx);
    return (captured == want) ? 0 : 5;
}
