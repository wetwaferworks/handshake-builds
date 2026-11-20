# HandshakeOS RG35XX 2024 — v2-bigtext (built 2026-11-19 ~18:39 EST)
Source: /workspace/handshakeos (uncommitted on top of 193a15a). Not committed, not pushed.

FEL RAM boot: copy fel-dropin/handshakeos-initramfs.uimg over fel/fixed/handshakeos-initramfs.uimg
(same name the 03-ramboot-gz.cmd writes to 0x4FF00000). Format is identical to v1:
mkimage -A arm64 -O linux -T ramdisk -C gzip, newc cpio, gzip -9, same file list.
Size 797,970 B -> 0x4FF00000..0x4FFC2D12.

New controls: L1/R1 theme, X frame pacing (60/30/24/16 Hz), Y dial speed (SLOW/MED/FAST/FULL),
D-pad + A on the 3x2 tiles, B cancel. Kernel cmdline knobs: handshakeos.pace=24, handshakeos.bgr.
Host tools: --pace= --dial= --confirm= --bench-frames=N --live-seconds=S --bgr

Battery: the 7.3.5 kernel has CONFIG_AXP20X_ADC unset, so CHARGER_AXP20X and BATTERY_AXP20X
(both depend on AXP20X_ADC) were dropped, and CONFIG_INPUT_AXP20X_PEK is unset (no KEY_POWER).
A kernel config rebuild is needed: see kernel.config.fragment-v2-suggested.
