#ifndef BK4802_H
#define BK4802_H

#include <stdint.h>

/* 初始化 GPIO（I2C 位敲） */
void bk4802_init(void);

/* 写一个 16-bit 寄存器 */
void bk4802_write_reg(uint8_t reg, uint16_t data);
/* 读一个 16-bit 寄存器（用于 RSSI/S-meter 等） */
uint16_t bk4802_read_reg(uint8_t reg);

/* 进入接收模式，写入默认接收寄存器表 */
void bk4802_enter_rx(void);

/* 设置接收频率（MHz），自动按频段取 Ndiv 并写 reg0/1/2 */
void bk4802_set_rx_freq_mhz(float mhz);

/* 解调输出幅度 0..3（reg19 B13:B12） */
void bk4802_set_demod_amp(uint8_t level);
/* 音量 0..15（reg19 B03:B00） */
void bk4802_set_volume(uint8_t vol);
/* 中频增益 0..7（reg12 B07:B05） */
void bk4802_set_if_gain(uint8_t gain);
/* 中频带宽 0=200kHz 1=1MHz（reg12 B04） */
void bk4802_set_if_bandwidth(uint8_t wide);

/* 静噪 RSSI 阈值（reg22 B07:B00）0=常开音频 */
void bk4802_set_squelch(uint8_t thr);

/* 信号强度（近似 S-meter，寄存器待上电校验） */
uint8_t bk4802_get_smeter(void);

/* 使能音频输出常用辅助：把 reg19 解调幅度/音量按配置写一次 */
void bk4802_apply_audio_config(void);

#endif /* BK4802_H */
