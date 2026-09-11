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
#include "msg_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_screen(const char *s)
{
  if (!strcmp(s, "pattern") || !strcmp(s, "t")) return UI_SCREEN_PATTERN;
  if (!strcmp(s, "standby") || !strcmp(s, "s")) return UI_SCREEN_STANDBY;
  if (!strcmp(s, "inbox")   || !strcmp(s, "i")) return UI_SCREEN_INBOX;
  if (!strcmp(s, "detail")  || !strcmp(s, "d")) return UI_SCREEN_DETAIL;
  if (!strcmp(s, "home")    || !strcmp(s, "h")) return UI_SCREEN_HOME;
  if (!strcmp(s, "messages") || !strcmp(s, "msg")) return UI_SCREEN_MSG_INBOX;
  if (!strcmp(s, "msgread"))  return UI_SCREEN_MSG_READ;
  if (!strcmp(s, "heard"))    return UI_SCREEN_INBOX;
  if (!strcmp(s, "cnfont"))   return UI_SCREEN_CNFONT;
  if (!strcmp(s, "menu")    || !strcmp(s, "m")) return UI_SCREEN_MENU;
  if (!strcmp(s, "radio")   || !strcmp(s, "r")) return UI_SCREEN_RADIO;
  if (!strcmp(s, "about")) return UI_SCREEN_ABOUT;
  if (!strcmp(s, "boot")    || !strcmp(s, "b")) return UI_SCREEN_BOOT;
  if (!strcmp(s, "confirm") || !strcmp(s, "c")) return UI_SCREEN_CONFIRM;
  return -1;
}

/* 键盘映射自检：SDL 键码 -> 内部键码。
 * 加这个是因为踩过"文档说按 7 是待机页、键盘却没绑数字键"的坑——这类问题不该靠人肉发现。 */
static void keymap_report(void)
{
  static const struct { int sym; const char *name; int want; } T[] = {
    { SDLK_1, "1", SIM_KEY_UP },      { SDLK_2, "2", SIM_KEY_DOWN },
    { SDLK_3, "3", SIM_KEY_OK },      { SDLK_4, "4", SIM_KEY_BACK },
    { SDLK_5, "5", SIM_KEY_PATTERN }, { SDLK_7, "7", SIM_KEY_STANDBY },
    { SDLK_8, "8", SIM_KEY_DEL },     { SDLK_9, "9", SIM_KEY_MESSAGES },
    { SDLK_s, "S", SIM_KEY_STANDBY }, { SDLK_m, "M", SIM_KEY_MESSAGES },
    { SDLK_t, "T", SIM_KEY_PATTERN },
    { SDLK_UP, "Up", SIM_KEY_UP },    { SDLK_DOWN, "Down", SIM_KEY_DOWN },
    { SDLK_RETURN, "Enter", SIM_KEY_OK },
    { SDLK_BACKSPACE, "Backspace", SIM_KEY_BACK },
    { SDLK_DELETE, "Delete", SIM_KEY_DEL },
  };
  int i, fail = 0;
  printf("[sim] 键盘映射自检（SDL 键 -> 内部键码）:\n");
  for (i = 0; i < (int)(sizeof(T) / sizeof(T[0])); i++) {
    int got = lcd_sim_map_key(T[i].sym);
    int ok = (got == T[i].want);
    if (!ok) fail++;
    printf("    %-10s -> %-2d  %s\n", T[i].name, got, ok ? "OK" : "FAIL");
  }
  printf("[sim] 键盘映射自检：%s（共 %d 项）\n", fail ? "有失败" : "全部通过",
         (int)(sizeof(T) / sizeof(T[0])));
}

static void usage(void)
{
  printf("用法: bbcall_sim [选项]\n"
         "  --scale N        放大倍数 1..12（默认 4）\n"
         "  --selftest       无窗口渲染一帧并写出 BMP\n"
         "  --out FILE       自检输出文件名（默认 sim_selftest.bmp）\n"
         "  --screen NAME    boot|home|menu|inbox|heard|detail|radio|about|pattern|messages|msgread|cnfont|confirm\n"
         "  --wav FILE       WAV -> modem.c 解调 -> 收件箱\n"
         "  --replay FILE    串口日志 [RAW] hex= 回放到收件箱\n"
         "  --clock SEC      主页大时钟的固定值（自检截图用）\n"
         "  --keys LIST      按键序列（逗号分隔，如 4,3），用于验证导航路径\n"
         "  --keymap         打印并自检键盘映射（SDL 键 -> 内部键码）\n"
         "  --demo           注入内置示例帧\n"
         "\n按键: 上下=选择  Enter=打开  Backspace=返回  Delete=删除(二次确认)\n"
         "      T=图案  M=收件箱  S=待机  I=反显  B=背光  F3=面板方向  F12=截图  Esc=退出\n");
}

int main(int argc, char **argv)
{
  int scale = 4;
  int selftest = 0;
  int screen = UI_SCREEN_MSG_INBOX;   /* 开机即消息列表：它是本机唯一的主功能 */
  int demo = 0;
  int clock_sec = -1;
  int i;
  const char *wav = NULL, *log = NULL, *out = "sim_selftest.bmp";
  const char *keys = NULL;

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
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i];
    else if (!strcmp(argv[i], "--keymap")) { keymap_report(); return 0; }
    else if (!strcmp(argv[i], "--demo")) demo = 1;
    else if (!strcmp(argv[i], "--standby")) screen = UI_SCREEN_STANDBY;
    else if (!strcmp(argv[i], "--message") || !strcmp(argv[i], "--inbox")) screen = UI_SCREEN_INBOX;
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
  }

  if (selftest) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  if (lcd_sim_init("BBcall_APRS LCD Simulator", scale) != 0) return 1;

  ui_init();
  ui_set_rx_freq_khz(144640u);
  if (clock_sec >= 0) ui_set_clock_ms((uint32_t)clock_sec * 1000u);

  if (wav) sim_feed_wav(wav);
  if (log) sim_feed_log(log);
  if (demo || (!wav && !log)) {
    sim_feed_demo();
    msg_store_add_demo();     /* Sent/Drafts 侧演示数据（本项目不发射） */
  }

  printf("[sim] 收件箱 %u 条（未读 %u）/ 累计收到 %u 帧（重复抑制 %u）\n",
         (unsigned)ui_inbox_count(), (unsigned)ui_unread_count(), (unsigned)ui_rx_total(),
         (unsigned)ui_dup_total());

  ui_show(screen);

  /* --keys "4,3"：按顺序派发按键，用于验证导航路径（自检可复现） */
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
  }
  lcd_sim_render();

  if (selftest) {
    lcd_sim_save_bmp(out);
    printf("[sim] 已写出 %s（%dx%d）\n", out, 128 * scale, 64 * scale);
    lcd_sim_shutdown();
    return 0;
  }

  printf("[sim] 键盘：1/2 上下  3 确定  4 返回（在待机页或收件箱上进入二级菜单）\n"
         "[sim]       7 待机页  9 收件箱  8 删除  5 字体样张\n"
         "[sim]       S 待机页  M 收件箱  T 样张  I 反显  B 背光  F3 面板  F12 截图  Esc 退出\n"
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