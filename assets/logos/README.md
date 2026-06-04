# Provider logos

QuotaPuter renders **original pixel identification glyphs** (a brand-ish accent
colour plus initials and a region badge) for each provider — see
`components/assets/provider_style.c`. It does **not** ship the vendors'
trademarked logos, because their public redistribution terms vary (PRD §8.2, §15).

If you have the right to use a vendor's logo, you can drop in your own pixel
bitmaps using this naming convention (16×16 and 24×24, single- or few-colour):

```
assets/logos/minimax_cn_16.png      assets/logos/minimax_cn_24.png
assets/logos/minimax_global_16.png  assets/logos/minimax_global_24.png
assets/logos/openai_16.png          assets/logos/openai_24.png
assets/logos/claude_16.png          assets/logos/claude_24.png
assets/logos/gemini_16.png          assets/logos/gemini_24.png
assets/logos/kimi_16.png            assets/logos/kimi_24.png
```

Convert a PNG to an RGB565 C array (e.g. with `lovyanGFX`'s image tools or a
small Python script) and add it to `components/assets`, then have
`ui::provider_logo` blit it via `pushImage` instead of drawing the glyph.

## Trademarks

All brand names and logos are the property of their respective owners. The
pixel glyphs in this project are used only to identify the corresponding
official service and do not imply sponsorship, endorsement, or affiliation.
