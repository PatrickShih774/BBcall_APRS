#ifndef SIM_FEED_H
#define SIM_FEED_H

/* 返回成功注入收件箱的帧数；负数表示错误码 */
int sim_feed_wav(const char *path);
int sim_feed_log(const char *path);
int sim_feed_demo(void);

#endif