/* 设备端解码日志：内部 Flash 尾部的环形缓冲（掉电不丢）。
 * 用途：无人值守跑 10 小时后，串口一次 `LOG?` 把它整段导出来，和网络侧
 *       （APRS-IS / aprs.fi / 另一台接收机）的包清单按时间戳与内容对比，算解码率。
 * 只记"新内容解出"的帧：同一次发射被 16 相位 / 跳变对齐多路同时解出时只有第一条占记录，
 * 其余走 [DUP] 计数（和 RX= 的口径一致）。
 * 记录格式（72 字节一槽；页 1024 字节是一个擦除单元，每页 14 槽）：
 *   off  size 字段
 *   0    2    seq（0xFFFF = 空槽；最后写，作"这条有效"的提交标记，掉电半条会被开机扫描忽略）
 *   2    4    t（RTC 2000-01-01 起秒数；time_kind=1 时是上电秒数）
 *   6    1    meta：bit0 time_kind(1=uptime) bit1 FIX bit2 REP bit3 have_rf bit4 截断 bit5~7 保留(0)
 *   7    1    rssi（BK4802 寄存器 24 低字节原始值）
 *   8    1    snr（寄存器 24 的 SNR 位段原始值）
 *   9    1    info_len（0..PKTLOG_INFO_MAX）
 *   10   6    call（6 字符呼号，不含 SSID）
 *   16   2    hash（与去重同一个 16bit 整帧哈希）
 *   18   1    ssid
 *   19   1    magic = 0x5A（把"旧固件残留在本区的字节"挡在门外）
 *   20   52   info（正文，超出 PKTLOG_INFO_MAX 截断）
 * 环形写入：当前页写满后擦下一页（那一页装的是最旧数据）。
 * 日志区位置：**固件映像之后**（链接脚本导出 _image_end）到 Flash 末尾；页数 = 剩余页数，
 * 上限由 bbcall_cfg.h 的 BBCALL_PKTLOG_PAGES 决定（实测 Debug 约 9 页、Release 约 13 页）。 */
#ifndef PKTLOG_H
#define PKTLOG_H

#include <stdint.h>

#define PKTLOG_INFO_MAX 52u   /* info 最大存储字节数（APRS 位置/消息帧通常够用，超长截断） */

typedef struct {
  uint32_t t;           /* time_kind=0：RTC 秒（2000-01-01 基准）；=1：上电秒数 */
  uint8_t  time_kind;
  uint8_t  flags;       /* bit0 = FIX（1bit 纠错），bit1 = REP（参考帧恢复） */
  uint8_t  have_rf;     /* 0 = 本帧没有 RSSI/SNR 采样 */
  uint8_t  rssi, snr;
  uint16_t hash;        /* 整帧哈希（与去重同一算法） */
  char     call[6];     /* 6 字符呼号（不含 SSID），AX.25 移位后的大写 */
  uint8_t  ssid;
  uint8_t  info_len;
  char     info[PKTLOG_INFO_MAX];
} pktlog_rec_t;

void     pktlog_init(void);                    /* 上电扫描（不擦除有效记录） */
uint8_t  pktlog_append(const pktlog_rec_t *r); /* 追加一条（满则擦掉最旧一页）；1 = 成功 */
uint16_t pktlog_count(void);                   /* 当前有效记录数 */
uint16_t pktlog_capacity(void);                /* 容量 = 实际页数 x 14 条 */
uint16_t pktlog_pages(void);                   /* 实际页数（日志区跟在固件映像之后算出来的） */
uint32_t pktlog_base(void);                    /* 日志区起始地址（开机行 base= 就是它） */
uint16_t pktlog_seq_last(void);                /* 最后一条的序号（= 累计写入条数-1；16bit 会回绕） */
void     pktlog_dump(void);                    /* 串口打印（旧 -> 新，格式见 DEBUG_LOG.md） */
void     pktlog_clear(void);                   /* 擦除整个日志区并复位序号 */

#endif /* PKTLOG_H */