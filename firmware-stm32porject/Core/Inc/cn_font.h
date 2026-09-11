#ifndef CN_FONT_H
#define CN_FONT_H
#include <stdint.h>

/* 中文字库子集（16x16 点阵），布局与生成方式见 tools/gen_cn_font.py。
 * 设计与字源选择参考 EthanYan6/Dondji 的外部 SPI Flash 字库方案，
 * 但本项目只在片上放子集；CN_FONT_ENABLED=0 时全部退化为空实现。 */

/* 返回该码点的字形位图指针（16 行 x uint16_t，MSB = 最左像素）；未收录返回 0 */
const uint16_t *cn_font_lookup(uint32_t ucs);
uint16_t        cn_font_count(void);
/* 第 idx 个字形对应的 Unicode 码点（用于自检样张逐字显示） */
uint32_t        cn_font_code_at(uint16_t idx);
#endif