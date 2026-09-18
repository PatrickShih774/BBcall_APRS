#ifndef BK4802_H
#define BK4802_H
#include <stdint.h>
void bk4802_init(void);
void bk4802_write_reg(uint8_t reg, uint16_t data);
uint16_t bk4802_read_reg(uint8_t reg);
uint16_t bk4802_i2c_error_count(void);
void bk4802_enter_rx(void);
void bk4802_set_rx_freq_khz(uint32_t khz);   /* 整数运算，避免拖进软浮点库（省 1KB+ Flash） */
/* 计算频率字：reg2 + 24bit word（reg0=高16 / reg1=低16）；设置与开机打印共用 */
void bk4802_freq_word_khz(uint32_t khz, uint16_t *reg2, uint32_t *word);
void bk4802_apply_audio_config(void);
void bk4802_set_squelch(uint8_t thr);
/* 自适应中频增益：code 0..7，3dB/级（0=0dB ... 7=21dB） */
void bk4802_set_if_gain_code(uint8_t code);
/* 软件静噪：mute=1 关闭接收音频通路并把音量归零，mute=0 恢复 */
void bk4802_set_rx_audio_mute(uint8_t mute);
uint8_t bk4802_get_smeter(void);
/* 总线诊断：返回 scl/sda 释放后的空闲电平（1=高），ack=1 表示从机回了 ACK（芯片是活的） */
/* scl_rel_pa8：临时放开 DIO1(PA8) 后再读的 SCL 电平；=1 说明 SCL 是被 PA8 拉低的（连锡） */
void bk4802_bus_probe(uint8_t *scl, uint8_t *sda, uint8_t *ack, uint8_t *scl_rel_pa8);
#endif
