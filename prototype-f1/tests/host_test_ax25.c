/*
 * 主机 AX.25/APRS 解码自测（不依赖 HAL）。
 * 编译：gcc -I../Inc -I. host_test_ax25.c ax25.c aprs.c -o test_ax25 && ./test_ax25
 * 帧体来自 tools/ax25_reference.py 生成（不含 flag, 含 FCS）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ax25.h"
#include "aprs.h"

int main(void)
{
    /* APRS 消息帧：src=BG5BLH, dest=APRS, 正文 "Hello APRS 144.640" */
    const char hex[] =
        "82a0a4a6404060848e6a8498906103f03a424735424c482020203a"
        "48656c6c6f2041505253203134342e3634305095";

    uint8_t body[200];
    int n = 0;
    for (int i = 0; hex[i]; i += 2) {
        char b[3] = {hex[i], hex[i + 1], 0};
        body[n++] = (uint8_t)strtol(b, 0, 16);
    }

    ax25_decoded_t d;
    if (!ax25_decode(body, (uint16_t)n, &d)) {
        printf("FAIL: CRC/parse\n");
        return 1;
    }
    printf("dest=%s-%d src=%s-%d ctrl=0x%02X pid=0x%02X npath=%d\n",
           d.dest, d.dest_ssid, d.src, d.src_ssid, d.control, d.pid, d.npath);
    printf("info=%.*s\n", (int)d.info_len, d.info);

    aprs_message_t m;
    if (!aprs_parse_message(d.info, d.info_len, &m)) {
        printf("FAIL: not an APRS message\n");
        return 1;
    }
    printf("addressee=%.*s body=%.*s\n",
           (int)m.addressee_len, m.addressee, (int)m.body_len, m.body);

    if (strcmp(d.src, "BG5BLH") != 0 || strcmp(d.dest, "APRS") != 0) return 1;
    if (strncmp((const char *)m.body, "Hello APRS", 10) != 0) return 1;
    printf("OK: AX.25/APRS decode verified\n");
    return 0;
}
