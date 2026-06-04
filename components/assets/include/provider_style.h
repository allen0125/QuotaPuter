// provider_style.h — per-provider visual identity for the pixel UI.
//
// These are ORIGINAL identification glyphs (brand-ish accent colour + initials),
// NOT the trademarked vendor logos. Real logos, if a user has the right to use
// them, can be imported as bitmaps under assets/logos/ (see PRD §8.2, §15).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t color;      // RGB565 accent colour
    const char *glyph;   // 1-3 char identification glyph
} provider_style_t;

// Style for a provider id; a neutral gray "?" style for unknown ids.
provider_style_t provider_style_get(const char *id);

#ifdef __cplusplus
}
#endif
