#ifndef SIM_FEED_H
#define SIM_FEED_H

/* 返回成功注入收件箱的帧数；负数表示错误码 */
int sim_feed_wav(const char *path);
int sim_feed_log(const char *path);
int sim_feed_demo(void);

/* 模拟真机在解码当刻读到的 RSSI/SNR（芯片原始读数）：wav/log 回放路径在每帧入箱前注入。
 * rssi < 0 表示不注入（界面显示 --）；真机由 bbcall_app.c 从 BK4802 寄存器 24 读取。 */
void sim_feed_set_rf(int rssi, int snr);

#endif