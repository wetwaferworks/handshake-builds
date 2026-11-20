# GBC LCD color curve (HandshakeOS v5.5)

The Game Boy Color's LCD does not show raw RGB555 values the way a
modern sRGB panel does. Pan Docs ("Palettes / RGB Translation by CGBs")
records the hardware behaviour:

- Intensities are not linear. Values $10-$1F all appear very bright;
  midtones and darks are packed into $00-$0F.
- The panel is not backlit. Peak output is a light gray, not white.
- The pigments are not fully saturated. Raising one channel also lifts
  the others. The documented example is color $03EF (R=$0F, G=$1F, B=$00):
  neon green on a PC monitor, a washed-out yellow on the CGB LCD.

HandshakeOS uses the widely published 5-bit mixing form of that LCD
when converting a DMG-compat (or raw RGB555) color to a 24-bit pixel:

    R' = min(960, 26*r +  4*g +  2*b)
    G' = min(960,  4*r + 24*g +  4*b)
    B' = min(960,  2*r +  4*g + 26*b)
    8-bit = x >> 2     # peak 240, so "white" is light gray

The matrix rows sum to 32, so a neutral gray stays a (dimmer) gray.
Off-diagonal terms are the pigment crosstalk that lowers saturation
versus a raw 5-to-8 expand.

r, g, b are the 5-bit channels of the RGB555 word
(bit 0-4 red, 5-9 green, 10-14 blue).

The mixing (the 4*g + 2*b into red, 8*b into green, 6*r + 4*g into blue)
is what lowers apparent saturation versus a raw `(c << 3) | (c >> 2)`
expand, and is why $03EF becomes a yellow rather than a neon green.

This curve is applied to the CGB boot-ROM palettes used for DMG carts
(Pokémon Yellow and every other known title). The four built-in generic
ramps (classic olive, pocket b&w, deep purple, matrix green), still
selectable with L1, are authored in 24-bit already and are left alone.

R1 is unchanged: it still toggles the HUD (or AA / nearest on cores
that use it). It does not change scaling or the palette.
