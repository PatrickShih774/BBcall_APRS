/* WAV replay regression for modem.c. Usage: modem_regress.exe file.wav */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "modem.h"
#include "ax25.h"

#define SIM_FS_HZ 9600u

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static int32_t adc_from_s16(int32_t v) {
    int32_t a = 2048 + v * 800 / 32768;
    if (a < 0) a = 0;
    if (a > 4095) a = 4095;
    return a;
}

static void report(const ax25_frame_t *f, const char *kind) {
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    for (i = 0; i < f->len; ++i) {
        uint8_t b = f->frame[i], j;
        crc ^= b;
        for (j = 0; j < 8u; ++j) crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1);
    }
    crc = (uint16_t)~crc;
    printf("%s len=%u crc=%04x hex=", kind, (unsigned)f->len, (unsigned)crc);
    for (i = 0; i < f->len; ++i) printf("%02x", f->frame[i]);
    printf("\n");
}

static int drain(void) {
    ax25_frame_t f;
    int n = 0;
    while (modem_get_frame(&f)) {
        report(&f, modem_frame_was_fixed() ? "FIX" : (modem_frame_was_repeat() ? "REP" : "CRC"));
        ++n;
    }
    return n;
}

int main(int argc, char **argv) {
    FILE *fp;
    long sz;
    uint8_t *buf;
    uint16_t fmt, ch, bits;
    uint32_t rate, dlen, frames, p;
    const uint8_t *data;
    uint32_t i, k;
    uint16_t mk, sp, ot;
    int count = 0;

    if (argc != 2) { fprintf(stderr, "usage: %s file.wav\n", argv[0]); return 2; }
    fp = fopen(argv[1], "rb");
    if (!fp) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz < 44) { fclose(fp); return 2; }
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(fp); return 3; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) { free(buf); fclose(fp); return 3; }
    fclose(fp);
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) { free(buf); return 4; }
    fmt = ch = bits = 0; rate = dlen = 0; data = NULL; p = 12;
    while (p + 8u <= (uint32_t)sz) {
        uint32_t csz = rd32(buf + p + 4);
        const uint8_t *ck = buf + p + 8;
        if (memcmp(buf + p, "fmt ", 4) == 0 && csz >= 16u) {
            fmt = rd16(ck); ch = rd16(ck + 2); rate = rd32(ck + 4); bits = rd16(ck + 14);
        } else if (memcmp(buf + p, "data", 4) == 0) {
            data = ck; dlen = csz;
        }
        p += 8u + csz + (csz & 1u);
    }
    if (!data || fmt != 1u || bits != 16u || ch < 1u || rate == 0u) { free(buf); return 5; }
    frames = dlen / (2u * ch);
    printf("WAV rate=%u ch=%u frames=%u seconds=%.3f\n", rate, ch, frames, (double)frames / rate);
    modem_init();
    if (rate % SIM_FS_HZ == 0u && rate >= SIM_FS_HZ) {
        uint32_t dec = rate / SIM_FS_HZ;
        for (i = 0; i + dec <= frames; i += dec) {
            int32_t sum = 0;
            for (k = 0; k < dec; k++) {
                const uint8_t *sp2 = data + (size_t)(i + k) * 2u * ch;
                int32_t v = (int32_t)(int16_t)rd16(sp2);
                uint16_t c;
                for (c = 1u; c < ch; c++) v += (int32_t)(int16_t)rd16(sp2 + 2u * c);
                sum += v / (int32_t)ch;
            }
            modem_adc_sample((uint16_t)adc_from_s16(sum / (int32_t)dec));
            count += drain();
        }
    } else {
        uint32_t n_out = (uint32_t)((uint64_t)frames * SIM_FS_HZ / rate);
        for (i = 0; i < n_out; i++) {
            double src = (double)i * (double)rate / SIM_FS_HZ;
            uint32_t i0 = (uint32_t)src;
            double frac = src - (double)i0;
            int32_t v0, v1, v;
            if (i0 + 1u >= frames) i0 = frames ? frames - 1u : 0u;
            v0 = (int32_t)(int16_t)rd16(data + (size_t)i0 * 2u * ch);
            v1 = (int32_t)(int16_t)rd16(data + (size_t)((i0 + 1u < frames) ? (i0 + 1u) : i0) * 2u * ch);
            v = v0 + (int32_t)((double)(v1 - v0) * frac);
            modem_adc_sample((uint16_t)adc_from_s16(v));
            count += drain();
        }
    }
    free(buf);
    modem_get_stats(&mk, &sp, &ot);
    printf("STAT mark=%u space=%u other=%u FIX=%u FIX2=%u REP=%u frames=%d\n",
        mk, sp, ot, modem_get_fix_count(), modem_get_fix2_count(), modem_get_rep_count(), count);
    return 0;
}
