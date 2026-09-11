/*
 * 中文字库查找（二分搜索 Unicode 索引表）
 *
 * 布局沿用 EthanYan6/Dondji（Apache-2.0）的形状：
 *   [位图][Unicode 索引 4B/项 升序][版本字节]
 * 它把字库放外部 SPI Flash、固件只留布局常量；本项目暂时只做片上子集，
 * 保持同一形状是为了将来接上外部 Flash 时读取逻辑一行不用改。
 * 字源：GNU Unifont（OFL-1.1 或 GPLv2+ 双许可），见 THIRD_PARTY_NOTICES.md。
 */
#include "bbcall_cfg.h"
#include "cn_font.h"

#if CN_FONT_ENABLED
#include "cn_font_data.h"

const uint16_t *cn_font_lookup(uint32_t ucs)
{
  uint16_t lo = 0u;
  uint16_t hi = (uint16_t)(CN_FONT_CHAR_COUNT - 1u);

  while (lo <= hi) {
    uint16_t mid = (uint16_t)((uint16_t)(lo + hi) >> 1);
    uint32_t v = cn_font_index[mid];
    if (v == ucs) return &cn_font_bitmaps[(uint32_t)mid * 16u];
    if (v < ucs) {
      lo = (uint16_t)(mid + 1u);
    } else {
      if (mid == 0u) break;
      hi = (uint16_t)(mid - 1u);
    }
  }
  return 0;
}

uint16_t cn_font_count(void) { return (uint16_t)CN_FONT_CHAR_COUNT; }

uint32_t cn_font_code_at(uint16_t idx)
{
  if (idx >= (uint16_t)CN_FONT_CHAR_COUNT) return 0u;
  return cn_font_index[idx];
}

#else  /* !CN_FONT_ENABLED */

const uint16_t *cn_font_lookup(uint32_t ucs) { (void)ucs; return 0; }
uint16_t        cn_font_count(void) { return 0u; }
uint32_t        cn_font_code_at(uint16_t idx) { (void)idx; return 0u; }

#endif