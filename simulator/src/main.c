/*
 * BBcall_APRS PC LCD 模拟器主程序
 *
 * 复用固件代码：lcd_st7567.c（ST7567 驱动/绘图）、ax25.c、aprs.c、modem.c。
 * 数据来源：--wav（音频解调）/ --replay（串口日志回放）/ --demo（内置示例）。
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "lcd_sim.h"
#include "ui_harness.h"
#include "sim_feed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_screen(const char *s)
{
  if (!strcmp(s, "pattern") || !strcmp(s, "t")) return UI_SCREEN_PATTERN;
  if (!strcmp(s, "standby") || !strcmp(s, "s")) return UI_SCREEN_STANDBY;
  if (!strcmp(s, "inbox")   || !strcmp(s, "i")) return UI_SCREEN_INBOX;
  if (!strcmp(s, "detail")  || !strcmp(s, "d")) return UI_SCREEN_DETAIL;
  return -1;
}

static void usage(void)
{
  printf("用法: bbcall_sim [选项]\n"
         "  --scale N        放大倍数 1..12（默认 4）\n"
         "  --selftest       无窗口渲染一帧并写出 BMP\n"
         "  --out FILE       自检输出文件名（默认 sim_selftest.bmp）\n"
         "  --screen NAME    pattern|standby|inbox|detail\n"
         "  --wav FILE       WAV -> modem.c 解调 -> 收件箱\n"
         "  --replay FILE    串口日志 [RAW] hex= 回放到收件箱\n"
         "  --demo           注入内置示例帧\n"
         "\n按键: 上下=选择  Enter=打开  Backspace=返回  Delete=删除\n"
         "      T=图案  M=收件箱  S=待机  I=反显  B=背光  F3=面板方向  F12=截图  Esc=退出\n");
}

int main(int argc, char **argv)
{
  int scale = 4;
  int selftest = 0;
  int screen = UI_SCREEN_PATTERN;
  int demo = 0;
  int i;
  const char *wav = NULL, *log = NULL, *out = "sim_selftest.bmp";

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--selftest")) selftest = 1;
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "--screen") && i + 1 < argc) {
      int s = parse_screen(argv[++i]);
      if (s >= 0) screen = s;
    }
    else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
    else if (!strcmp(argv[i], "--replay") && i + 1 < argc) log = argv[++i];
    else if (!strcmp(argv[i], "--demo")) demo = 1;
    else if (!strcmp(argv[i], "--standby")) screen = UI_SCREEN_STANDBY;
    else if (!strcmp(argv[i], "--message") || !strcmp(argv[i], "--inbox")) screen = UI_SCREEN_INBOX;
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
  }

  if (selftest) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  if (lcd_sim_init("BBcall_APRS LCD Simulator", scale) != 0) return 1;

  ui_init();
  ui_set_rx_freq_khz(144640u);

  if (wav) sim_feed_wav(wav);
  if (log) sim_feed_log(log);
  if (demo || (!wav && !log)) sim_feed_demo();

  printf("[sim] 收件箱 %u 条 / 累计收到 %u 帧（重复抑制 %u）\n",
         (unsigned)ui_inbox_count(), (unsigned)ui_rx_total(),
         (unsigned)ui_dup_total());

  ui_show(screen);
  lcd_sim_render();

  if (selftest) {
    lcd_sim_save_bmp(out);
    printf("[sim] 已写出 %s（%dx%d）\n", out, 128 * scale, 64 * scale);
    lcd_sim_shutdown();
    return 0;
  }

  while (!lcd_sim_should_quit()) {
    int key;
    lcd_sim_poll_events();
    while ((key = lcd_sim_get_key()) != 0) ui_handle_key(key);
    ui_tick(16);
    lcd_sim_render();
    SDL_Delay(16);
  }
  lcd_sim_shutdown();
  return 0;
}