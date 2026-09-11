#include "lcd_sim.h"
#include <SDL.h>
#include <string.h>
#include <stdio.h>

#define LCD_W 128
#define LCD_H 64
#define LCD_FB 1024

static SDL_Window   *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture  *s_tex;
static uint32_t      s_pix[LCD_W * LCD_H];

static uint8_t s_fb[LCD_FB];
static int s_page = 0, s_col = 0, s_col_lo = 0, s_col_hi = 0;
static int s_display_on = 1, s_invert = 0, s_all_on = 0;
static int s_seg_rev = 0, s_com_rev = 0, s_start = 0;
static int s_contrast_next = 0;
static int s_backlight = 1;
static int s_panel_flip = 0;   /* 模拟另一种面板接线（SEG 方向反向） */
static int s_scale = 4;
static int s_running = 1;

static int s_keys[32];
static int s_key_head = 0, s_key_tail = 0;

static void push_key(int k)
{
  int nh = (s_key_head + 1) % 32;
  if (nh == s_key_tail) return;
  s_keys[s_key_head] = k;
  s_key_head = nh;
}

int lcd_sim_get_key(void)
{
  if (s_key_tail == s_key_head) return 0;
  int k = s_keys[s_key_tail];
  s_key_tail = (s_key_tail + 1) % 32;
  return k;
}

void lcd_sim_reset(void)
{
  s_page = 0; s_col = 0; s_col_lo = 0; s_col_hi = 0;
  s_display_on = 1; s_invert = 0; s_all_on = 0;
  s_seg_rev = 0; s_com_rev = 0; s_start = 0;
  s_contrast_next = 0;
  memset(s_fb, 0, sizeof(s_fb));
}

void lcd_sim_cmd(uint8_t c)
{
  if (s_contrast_next) { s_contrast_next = 0; return; }   /* 0x81 后的对比度数据 */
  if (c == 0xE2) { lcd_sim_reset(); return; }
  if (c == 0xAE) { s_display_on = 0; return; }
  if (c == 0xAF) { s_display_on = 1; return; }
  if (c == 0xA4) { s_all_on = 0; return; }
  if (c == 0xA5) { s_all_on = 1; return; }
  if (c == 0xA6) { s_invert = 0; return; }
  if (c == 0xA7) { s_invert = 1; return; }
  if (c == 0xA0) { s_seg_rev = 0; return; }
  if (c == 0xA1) { s_seg_rev = 1; return; }
  if (c == 0xC0) { s_com_rev = 0; return; }
  if (c == 0xC8) { s_com_rev = 1; return; }
  if (c >= 0x40 && c <= 0x7F) { s_start = c & 0x3F; return; }
  if (c >= 0xB0 && c <= 0xB7) { s_page = c & 0x07; return; }
  if (c >= 0x00 && c <= 0x0F) { s_col_lo = c & 0x0F; s_col = (s_col_hi << 4) | s_col_lo; return; }
  if (c >= 0x10 && c <= 0x1F) { s_col_hi = c & 0x0F; s_col = (s_col_hi << 4) | s_col_lo; return; }
  if (c == 0x81) { s_contrast_next = 1; return; }
  /* 其它命令忽略 */
}

void lcd_sim_data(uint8_t d)
{
  if (s_contrast_next) { s_contrast_next = 0; return; }
  if (s_page < 0 || s_page > 7 || s_col < 0 || s_col > 127) return;
  s_fb[s_page * LCD_W + s_col] = d;
  s_col++;
  if (s_col >= LCD_W) s_col = 0;
}

void lcd_sim_data_bytes(const uint8_t *d, uint16_t n)
{
  for (uint16_t i = 0; i < n; i++) lcd_sim_data(d[i]);
}

void lcd_sim_backlight(uint8_t on) { s_backlight = on ? 1 : 0; }
void lcd_sim_toggle_invert(void) { s_invert = !s_invert; }
void lcd_sim_toggle_backlight(void) { s_backlight = !s_backlight; }
void lcd_sim_toggle_panel(void) { s_panel_flip = !s_panel_flip; }
int  lcd_sim_should_quit(void) { return !s_running; }

int lcd_sim_init(const char *title, int scale)
{
  if (scale < 1) scale = 1;
  if (scale > 12) scale = 12;
  s_scale = scale;
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return -1;
  }
  s_win = SDL_CreateWindow(title ? title : "BBcall_APRS LCD Simulator",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           LCD_W * s_scale, LCD_H * s_scale, 0);
  if (!s_win) { fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); return -1; }
  s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!s_ren) s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
  if (!s_ren) { fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError()); return -1; }
  s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING, LCD_W, LCD_H);
  if (!s_tex) { fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError()); return -1; }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  lcd_sim_reset();
  s_running = 1;
  return 0;
}

void lcd_sim_shutdown(void)
{
  if (s_tex) SDL_DestroyTexture(s_tex);
  if (s_ren) SDL_DestroyRenderer(s_ren);
  if (s_win) SDL_DestroyWindow(s_win);
  s_tex = NULL; s_ren = NULL; s_win = NULL;
  SDL_Quit();
}

static void build_pixels(void)
{
  const uint32_t bg_on  = 0xFFD8E8C8u;   /* 背光开：浅黄绿底 */
  const uint32_t bg_off = 0xFF303830u;   /* 背光关：深色底 */
  const uint32_t px     = 0xFF182018u;   /* 点亮像素 */
  for (int y = 0; y < LCD_H; y++) {
    int sy = (y + s_start) % LCD_H;
    if (s_com_rev) sy = LCD_H - 1 - sy;
    for (int x = 0; x < LCD_W; x++) {
      int sx = (s_seg_rev ^ s_panel_flip) ? (LCD_W - 1 - x) : x;
      int on = (s_fb[(sy / 8) * LCD_W + sx] >> (sy % 8)) & 1;
      if (s_all_on) on = 1;
      if (!s_display_on) on = 0;
      if (s_invert) on = !on;
      uint32_t bg = s_backlight ? bg_on : bg_off;
      s_pix[y * LCD_W + x] = on ? px : bg;
    }
  }
}

void lcd_sim_render(void)
{
  build_pixels();
  SDL_UpdateTexture(s_tex, NULL, s_pix, LCD_W * (int)sizeof(uint32_t));
  SDL_RenderClear(s_ren);
  SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
  SDL_RenderPresent(s_ren);
}

void lcd_sim_save_bmp(const char *path)
{
  int s = (s_scale < 1) ? 1 : s_scale;
  int w = LCD_W * s, h = LCD_H * s;
  int x, y;
  SDL_Surface *surf;
  build_pixels();
  surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!surf) return;
  for (y = 0; y < h; y++) {
    uint32_t *row = (uint32_t *)((uint8_t *)surf->pixels + (size_t)y * (size_t)surf->pitch);
    for (x = 0; x < w; x++) row[x] = s_pix[(y / s) * LCD_W + (x / s)];
  }
  SDL_SaveBMP(surf, path ? path : "lcd_sim.bmp");
  SDL_FreeSurface(surf);
}

/* SDL 键码 -> 内部键码。
 * 字母键（S/M/T）与数字键都能按：数字键直接用内部编码，与 --keys 的编号一致，
 * 免得"文档写 7 是待机页、键盘却没有 7"这种困惑。 */
int lcd_sim_map_key(int sdl_sym)
{
  switch (sdl_sym) {
    case SDLK_UP:        return SIM_KEY_UP;
    case SDLK_DOWN:      return SIM_KEY_DOWN;
    case SDLK_RETURN:    case SDLK_KP_ENTER: return SIM_KEY_OK;
    case SDLK_BACKSPACE: return SIM_KEY_BACK;
    case SDLK_DELETE:    return SIM_KEY_DEL;
    case SDLK_t: case SDLK_5: case SDLK_KP_5: return SIM_KEY_PATTERN;
    case SDLK_m: case SDLK_9: case SDLK_KP_9: return SIM_KEY_MESSAGES;
    case SDLK_s: case SDLK_7: case SDLK_KP_7: return SIM_KEY_STANDBY;
    case SDLK_1: case SDLK_KP_1: return SIM_KEY_UP;
    case SDLK_2: case SDLK_KP_2: return SIM_KEY_DOWN;
    case SDLK_3: case SDLK_KP_3: return SIM_KEY_OK;
    case SDLK_4: case SDLK_KP_4: return SIM_KEY_BACK;
    case SDLK_8: case SDLK_KP_8: return SIM_KEY_DEL;
    default: return 0;
  }
}
void lcd_sim_poll_events(void)
{
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    if (ev.type == SDL_QUIT) { s_running = 0; continue; }

    if (ev.type == SDL_KEYDOWN) {
      int k = lcd_sim_map_key(ev.key.keysym.sym);
      if (k != 0) push_key(k);
      switch (ev.key.keysym.sym) {
        case SDLK_ESCAPE: s_running = 0; break;
        case SDLK_i: lcd_sim_toggle_invert(); break;
        case SDLK_b: lcd_sim_toggle_backlight(); break;
        case SDLK_F3: lcd_sim_toggle_panel(); break;
        case SDLK_F12: lcd_sim_save_bmp("lcd_sim.bmp"); break;
        default: break;
      }
    }
  }
}