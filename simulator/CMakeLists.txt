cmake_minimum_required(VERSION 3.16)
project(BBcall_APRS_Sim C)

set(CMAKE_C_STANDARD 11)
set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../firmware-stm32porject)

find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

add_executable(bbcall_sim
  src/main.c
  src/sim_hal.c
  src/lcd_sim.c
  src/sim_feed.c
  ${FW}/Core/Src/ui_harness.c
  ${FW}/Core/Src/lcd_st7567.c
  ${FW}/Core/Src/ax25.c
  ${FW}/Core/Src/aprs.c
  ${FW}/Core/Src/modem.c
)

target_include_directories(bbcall_sim PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${FW}/Core/Inc
)
target_compile_definitions(bbcall_sim PRIVATE LCD_SIM=1 SDL_MAIN_HANDLED=1)

if(TARGET SDL2::SDL2)
  target_link_libraries(bbcall_sim PRIVATE SDL2::SDL2)
else()
  target_include_directories(bbcall_sim PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(bbcall_sim PRIVATE ${SDL2_LIBRARIES})
endif()