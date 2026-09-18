/*
 * BBcall_APRS PC LCD 模拟器主程序（design.md v2.0 三态模型）
 *
 * 复用固件代码：lcd_st7567.c（ST7567 驱动/绘图）、ax25.c、aprs.c、modem.c。
 * 数据来源：--wav（音频解调）/ --replay（串口日志回放）/ --demo（内置示例）。
 * 按键模型与真机一致：只有 ▲ ▼ ●（Enter 长按 620ms = 长按）。
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "lcd_sim.h"
#include "ui_harness.h"
#include "sim_feed.h"
#include "bbcall_cfg.h"   /* 默认墙钟与实机同源 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_screen(const char *s)
{
  if (!strcmp(s, "idle")    || !strcmp(s, "standby") || !strcmp(s, "s")) return UI_SCREEN_IDLE;
  if (!strcmp(s, "unread")  || !strcmp(s, "u"))                          return UI_SCREEN_UNREAD;
  if (!strcmp(s, "inbox")   || !strcmp(s, "i") || !strcmp(s, "messages")) return UI_SCREEN_INBOX;
  if (!strcmp(s, "pattern") || !strcmp(s, "t"))                          return UI_SCREEN_PATTERN;
  return -1;
}

/* 键盘映射自检：SDL 键码 -> 内部键码。
 * 加这个是因为踩过"文档说按 7 是待机页、键盘却没绑数字键"的坑——这类问题不该靠人肉发现。 */
static void keymap_report(void)
{
  static const struct { int sym; const char *name; int want; } T[] = {
    { SDLK_1, "1", SIM_KEY_UP },
    { SDLK_2, "2", SIM_KEY_DOWN },
    { SDLK_3, "3", SIM_KEY_OK },
    { SDLK_UP, "Up", SIM_KEY_UP },
    { SDLK_DOWN, "Down", SIM_KEY_DOWN },
    { SDLK_RETURN, "Enter", SIM_KEY_OK },
  };
  int i, fail = 0;
  printf("[sim] 键盘映射自检（SDL 键 -> 内部键码）:\n");
  for (i = 0; i < (int)(sizeof(T) / sizeof(T[0])); i++) {
    int got = lcd_sim_map_key(T[i].sym);
    int ok = (got == T[i].want);
    if (!ok) fail++;
    printf("    %-10s -> %-2d  %s\n", T[i].name, got, ok ? "OK" : "FAIL");
  }
  printf("[sim] 键盘映射自检：%s（共 %d 项；Enter 长按 %dms 由事件循环判定期）\n",
         fail ? "有失败" : "全部通过", (int)(sizeof(T) / sizeof(T[0])), 620);
}

static void usage(void)
{
  printf("用法: bbcall_sim [选项]\n"
         "  --scale N        放大倍数 1..12（默认 4）\n"
         "  --selftest       无窗口渲染一帧并写出 BMP\n"
         "  --out FILE       自检输出文件名（默认 sim_selftest.bmp）\n"
         "  --screen NAME    idle|unread|inbox|pattern（自检渲染指定态）\n"
         "  --wav FILE       WAV -> modem.c 解调 -> 收件箱\n"
         "  --replay FILE    串口日志 [RAW] hex= 回放到收件箱\n"
         "  --demo           注入内置示例帧（含中文消息；无外部文件时的默认）\n"
         "  --clock SEC      设备时钟固定值（自检截图用，决定时钟/时间显示）\n"
         "  --wallclock W,M,D  模拟 RTC：态 1 大格显示 周W M/D（如 3,9,16 = 周三 9/16）\n"
         "  --rf R,S         wav/日志回放时按真机那样注入 RSSI,SNR（芯片原始读数）\n"
         "  --mycall CALL    本机呼号（态 1 上格，默认 NOCALL）\n"
         "  --batt N         电量挡位 0 低 / 1 中 / 2 高（默认无采样，显示 --）\n"
         "  --keys LIST      按键序列（1=上 2=下 3=确定 4=长按确定），验证导航路径\n"
         "  --keymap         打印并自检键盘映射\n"
         "\n按键: 上/下 = 翻条(收件箱)  Enter = 确定  Enter 长按 620ms = 退出/最外层\n"
         "      I=反显  B=背光  F3=面板方向  F12=截图  Esc=退出\n");
}

int main(int argc, char **argv)
{
  int scale = 4;
  int selftest = 0;
  int screen = 0;                     /* 0 = 按状态机自动（未读>0 则有未读态） */
  int demo = 0;
  int clock_sec = -1;
  int batt = -1;
  int i;
  const char *wav = NULL, *log = NULL, *out = "sim_selftest.bmp";
  const char *keys = NULL, *mycall = NULL, *wallclock = NULL, *rf = NULL;

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
    else if (!strcmp(argv[i], "--clock") && i + 1 < argc) clock_sec = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--wallclock") && i + 1 < argc) wallclock = argv[++i];
    else if (!strcmp(argv[i], "--rf") && i + 1 < argc) rf = argv[++i];
    else if (!strcmp(argv[i], "--mycall") && i + 1 < argc) mycall = argv[++i];
    else if (!strcmp(argv[i], "--batt") && i + 1 < argc) batt = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i];
    else if (!strcmp(argv[i], "--keymap")) { keymap_report(); return 0; }
    else if (!strcmp(argv[i], "--demo")) demo = 1;
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
  }

  if (selftest) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  if (lcd_sim_init("BBcall_APRS LCD Simulator", scale) != 0) return 1;

  ui_init();
  ui_set_rx_freq_khz(144640u);
  if (mycall) ui_set_mycall(mycall);
  if (batt >= 0) ui_set_batt((int8_t)batt);
  if (wallclock) {
    unsigned w = 0, mo = 0, d = 0;
    if (sscanf(wallclock, "%u,%u,%u", &w, &mo, &d) == 3 && w < 7)
      ui_set_wallclock((uint8_t)w, (uint8_t)mo, (uint8_t)d);
    else
      printf("[sim] --wallclock 格式应为 W,M,D（W: 0=周日..6=周六），已忽略: %s\n", wallclock);
  }
  if (rf) {                                /* --rf RSSI,SNR：复现真机解码当刻的芯片读数 */
    unsigned r = 0, s = 0;
    if (sscanf(rf, "%u,%u", &r, &s) == 2) sim_feed_set_rf((int)r, (int)s);
    else printf("[sim] --rf 格式应为 RSSI,SNR（如 73,19），已忽略: %s\n", rf);
  }
  if (clock_sec >= 0) ui_set_clock_ms((uint32_t)clock_sec * 1000u);
  else                ui_set_clock_ms(BBCALL_WALLCLOCK_BASE_MS);   /* 不给 --clock 就用实机的默认值 */
#if BBCALL_WALLCLOCK_ENABLE
  if (!wallclock)     ui_set_wallclock(BBCALL_WALLCLOCK_WDAY, BBCALL_WALLCLOCK_MON, BBCALL_WALLCLOCK_DAY);
#endif

  if (wav) sim_feed_wav(wav);
  if (log) sim_feed_log(log);
  if (demo || (!wav && !log)) {
    sim_feed_demo();          /* 内置示例帧 */
  }

  printf("[sim] 收件箱 %u 条（未读 %u，满箱丢弃未读 %u）/ 累计收到 %u 帧（重复抑制 %u）\n",
         (unsigned)ui_inbox_count(), (unsigned)ui_unread_count(), (unsigned)ui_unread_dropped(),
         (unsigned)ui_rx_total(), (unsigned)ui_dup_total());

  /* --screen 只用于自检强制指定画面；不给就保持状态机自己的结果。
   * 真机上没有任何代码会在收包后调 ui_show，这里必须与真机一致，
   * 才能验证「解码成功 -> 自动从待机切到有未读页」。 */
  if (screen) ui_show(screen);
  else printf("[sim] 状态机当前页 = %u（1=待机 2=有未读 3=收件箱）\n", (unsigned)ui_current_screen());

  /* --keys "3,2,2"：按顺序派发按键，用于验证导航路径（自检可复现） */
  if (keys) {
    const char *q = keys;
    printf("[sim] 按键序列:");
    while (*q) {
      int k = atoi(q);
      if (k > 0) { printf(" %d", k); ui_handle_key(k); }
      while (*q && *q != ',') q++;
      if (*q == ',') q++;
    }
    printf("\n");
    /* 按键之后再报一次当前页，便于自检断言按键结果（1=待机 2=有未读 3=收件箱） */
    printf("[sim] 按键后状态机当前页 = %u\n", (unsigned)ui_current_screen());
  }
  lcd_sim_render();

  if (selftest) {
    lcd_sim_save_bmp(out);
    printf("[sim] 已写出 %s（%dx%d）\n", out, 128 * scale, 64 * scale);
    lcd_sim_shutdown();
    return 0;
  }

  printf("[sim] 键盘：↑ ↓ 翻条（仅收件箱）  Enter 确定  Enter 长按 620ms 退出/最外层\n"
         "[sim]       I 反显  B 背光  F3 面板方向  F12 截图  Esc 退出\n"
         "[sim]       注意：先把鼠标点进模拟器窗口，否则按键不会送进来\n");

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
