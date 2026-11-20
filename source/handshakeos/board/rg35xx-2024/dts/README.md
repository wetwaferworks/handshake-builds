# RG35XX 2024 NV3052C-clean device tree

`sun50i-h700-anbernic-rg35xx-2024-nv3052c.dts` is the decompiled, verified
device tree used for HandshakeOS on the Anbernic RG35XX 2024.

The only intentional change from the ROCKNIX-patched 7.3.5 DTB is the panel
node: `panel@0` is compatible with `anbernic,rg35xx-plus-panel` only. The
generic `panel-mipi-dpi-spi` string is removed so the built-in `nv3052c`
driver binds and drives the 640×480 screen directly, with no external
`/lib/firmware/panels/*.panel` file.

Compile with:

```sh
dtc -I dts -O dtb -o sun50i-h700-anbernic-rg35xx-2024-nv3052c.dtb \
    sun50i-h700-anbernic-rg35xx-2024-nv3052c.dts
```

`assemble-img.sh` and the FEL kit use this DTB by default.
