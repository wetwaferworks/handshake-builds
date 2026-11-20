# HandshakeOS v3-audio (RG35XX 2024)

Built 2026-11-19 9:44 PM ET (America/New_York). Not committed, pushed, or flashed.
builds/v1-firstboot, v2-bigtext, v2-stable-sd, v2-hc, v3-play untouched.

Kernel: copy of src/linux-7.3.5 in linux-7.3.5-audio/ (proven tree + fel/build/Image untouched).
Config = builds/v3-play/kernel-7.3.5.config + only these flips (olddefconfig):
SOUND SND SND_TIMER SND_PCM SND_SOC SND_DMAENGINE_PCM DMA_SUN6I SND_SUN4I_CODEC
SND_SUN8I_CODEC SND_SUN8I_CODEC_ANALOG SND_SUN50I_CODEC_ANALOG SND_SUN8I_ADDA_PR_REGMAP
SND_SIMPLE_CARD(+_UTILS) SND_AUDIO_GRAPH_CARD SND_SOC_SIMPLE_AMPLIFIER (all =y, plus
auto SND_COMPRESS_OFFLOAD SND_SOC_I2C_AND_SPI SND_SOC_SDCA_OPTIONAL =y), AXP20X_ADC
BATTERY_AXP20X CHARGER_AXP20X INPUT_AXP20X_PEK =y. AXP20X_POWER and DRM_PANFROST were already =y.

qemu-test/ is TEST ONLY (adds CONFIG_SND_VIRTIO=y to exercise raw ALSA in QEMU). Never ship it.
fel/ = unique-named FEL kit (proven LPDDR3 U-Boot 17ce4fb5, proven NV3052C DTB 814740b1).
