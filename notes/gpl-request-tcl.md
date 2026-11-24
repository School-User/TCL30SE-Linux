# GPL source request to TCL (DRAFT, not sent)

**Where to send it:** do not guess the address. Check the TCL / Alcatel mobile support site (the "Open Source" or "GPL" page for the 30 SE, or the legal / privacy contact) for the official address, and send it there. Also add a comment to the existing request on the SourceForge tracker (https://sourceforge.net/p/tcl-mobile/tickets), which is for 6165H, so mention that 6165S uses the same platform.

**Subject:** GPLv2 source code request - TCL 30 SE (model 6165S, codename Cruze_Pro), Linux kernel 4.19.188

---

Hello,

I own a TCL 30 SE (model 6165S) and I am requesting the complete corresponding source code for the Linux kernel and any other GPL/LGPL-licensed components that are in the firmware on my device, as required by the GNU General Public License, version 2 (section 3) and the LGPL.

**Device and software identifiers**

- Device: TCL 30 SE, model 6165S, codename Cruze_Pro
- SoC: MediaTek MT6762 (Helio G25), platform mt6765
- Android version: 12, build SP1A.210812.016, security patch level 2023-08-01
- Kernel version string from the boot image:
  `Linux version 4.19.188-00184-g2af9957e526-dirty-ab1rck61v164bspP33`
  built with Android clang 11.0.1 (r383902)
- Boot image kernel command line includes: `buildvariant=user JRD_VERSION_MARK_B1AD4040CX00`

**What I am requesting**

1. The full kernel source tree for this exact build, including the MediaTek platform and driver code, device tree sources (.dts / .dtsi) and the defconfig / .config used to build it. The tree should build the kernel image that is shipped on the device.
2. The build instructions and toolchain information needed to reproduce the build (compiler version, build script, and any scripts that control compilation and installation).
3. The corresponding source for any other GPL- or LGPL-licensed components in the firmware, or a list of them with where their source can be obtained.
4. If the source is published somewhere already (a download page or repository), the exact link for the 6165S.

I am asking for this so that I can build and test a modified kernel for my own device. Please send the source as a download link, an archive, or a repository link. If you cannot supply it by that means, please tell me the format and any reasonable media cost, as GPLv2 section 3 allows.

Please reply to the address below and confirm that you received this request. I would appreciate an answer within 30 days.

Thank you,

Siddh

---

## Notes (not part of the message)

- Only the kernel string, build ID and the cmdline marker above come from the phone's own boot image backup (`boot_a.img`). Add the exact software version from Settings > About phone if you want a tighter match (the build number there is what TCL support keys on).
- Do not attach IMEI or serial numbers unless TCL's form asks for them.
- If they only publish source for the 6165H, ask whether the 6165S kernel is the same tree. The tracker ticket suggests they share a platform, but that is not confirmed.
