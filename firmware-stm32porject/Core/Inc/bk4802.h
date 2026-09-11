#ifndef BK4802_H
#define BK4802_H
#include <stdint.h>
void bk4802_init(void);
void bk4802_write_reg(uint8_t reg, uint16_t data);
uint16_t bk4802_read_reg(uint8_t reg);
uint16_t bk4802_i2c_error_count(void);
void bk4802_enter_rx(void);
void bk4802_set_rx_freq_mhz(double mhz);
void bk4802_apply_audio_config(void);
void bk4802_set_squelch(uint8_t thr);
/* 自适应中频增益：code 0..7，3dB/级（0=0dB ... 7=21dB） */
void bk4802_set_if_gain_code(uint8_t code);
/* 软件静噪：mute=1 关闭接收音频通路并把音量归零，mute=0 恢复 */
void bk4802_set_rx_audio_mute(uint8_t mute);
uint8_t bk4802_get_smeter(void);
#endif
