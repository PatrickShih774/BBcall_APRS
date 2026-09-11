#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "lcd_sim.h"
#include "ui_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
  int scale = 4;
  int selftest = 0;
  int start_screen = SIM_KEY_PATTERN;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--selftest")) selftest = 1;
    else if (!strcmp(argv[i], "--standby")) start_screen = SIM_KEY_STANDBY;
    else if (!strcmp(argv[i], "--message")) start_screen = SIM_KEY_MESSAGE;
  }

  if (selftest) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  if (lcd_sim_init("BBcall_APRS LCD Simulator", scale) != 0) return 1;
  ui_init();
  ui_handle_key(start_screen);
  lcd_sim_render();

  if (selftest) {
    lcd_sim_save_bmp("sim_selftest.bmp");
    printf("selftest ok, wrote sim_selftest.bmp\n");
    lcd_sim_shutdown();
    return 0;
  }

  while (!lcd_sim_should_quit()) {
    lcd_sim_poll_events();
    int key;
    while ((key = lcd_sim_get_key()) != 0) ui_handle_key(key);
    ui_tick(16);
    lcd_sim_render();
    SDL_Delay(16);
  }
  lcd_sim_shutdown();
  return 0;
}