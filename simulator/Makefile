# BBcall_APRS PC LCD simulator (SDL2) - Makefile fallback
CC      ?= gcc
SDLCONF ?= sdl2-config

FW   := ../firmware-stm32porject
SRCS := src/main.c src/sim_hal.c src/lcd_sim.c src/ui_harness.c src/sim_feed.c \
        $(FW)/Core/Src/lcd_st7567.c $(FW)/Core/Src/ax25.c $(FW)/Core/Src/aprs.c $(FW)/Core/Src/modem.c

CFLAGS  += -O2 -std=c11 -Wall -DLCD_SIM=1 -DSDL_MAIN_HANDLED=1 -Isrc -I$(FW)/Core/Inc
CFLAGS  += $(shell $(SDLCONF) --cflags)
LDFLAGS += $(shell $(SDLCONF) --libs)

bbcall_sim: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f bbcall_sim bbcall_sim.exe sim_selftest.bmp lcd_sim.bmp

run: bbcall_sim
	./bbcall_sim --scale 4

selftest: bbcall_sim
	./bbcall_sim --selftest

.PHONY: clean run selftest