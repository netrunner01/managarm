## managarm — `https://github.com/netrunner01/managarm`

### Display / DRM / KMS

- [bbee0d3](https://github.com/netrunner01/managarm/commit/bbee0d3f389687bcbec8dbe1286f44627187de7f) — handle `DRM_IOCTL_MODE_OBJ_SETPROPERTY` (0xba) and stop asserting on client input (DEF-40).
- [06cb7ea](https://github.com/netrunner01/managarm/commit/06cb7ea71df4875b49cce926cdee6a8a4922a695) — honour `GEM_CLOSE` and `CLOSEFB` (0xd0); instrument VRAM (DEF-23, DEF-68).
- [504767f](https://github.com/netrunner01/managarm/commit/504767fd99f4cdf94b3456d1c2476a6fb4fa0f93) — gfx-bochs: return VRAM on buffer destruction; stop asserting on exhaustion (DEF-30).
- [7d81597](https://github.com/netrunner01/managarm/commit/7d81597ff9d10b58527737aa6a42e026789edb33) — core/drm: free a PRIME-exported buffer when its export fd closes (DEF-73 framebuffer teardown).
- [26abf0f](https://github.com/netrunner01/managarm/commit/26abf0f031b3292cb6d7519c6d02ca2d504f68f8) — core/drm: don't fail the legacy cursor ioctl when there is no cursor plane (DEF-69).
- [cba4775](https://github.com/netrunner01/managarm/commit/cba4775c44b5a210246662612d5e6b90c9c67383) — core/drm: only advertise cursor caps when a CRTC actually has a cursor plane (DEF-69).
- [b124491](https://github.com/netrunner01/managarm/commit/b124491452bcc204932de49f1ecb18b895b6c03d) — core/drm: implement the `DRM_IOCTL_WAIT_VBLANK` instant query.
- [6a84082](https://github.com/netrunner01/managarm/commit/6a840823f8258a07c85ef68c15befc99b59373fb) — gfx/bochs: offer a 1280x1024 mode (raise mode-list + max-dims cap).
- [e1d5a8b](https://github.com/netrunner01/managarm/commit/e1d5a8b4f49fe09105ca645a3658f30dce60dd57) — gfx-bochs: return a zero offset for buffer memory slices.
- [554079e](https://github.com/netrunner01/managarm/commit/554079e04dc20837f048f8e2a3d2a4437461cc75) — gfx-bochs: sample the outgoing mode before publishing new DRM state.

### Timers (repaint / frame scheduling)

- [bcfa947](https://github.com/netrunner01/managarm/commit/bcfa947ca600ee2281ce6bac2c23a0a941bebcb7) — thor: re-arm the one-shot timer after an early IRQ (fixes a stuck timer that stalls repaints).
- [1035f98](https://github.com/netrunner01/managarm/commit/1035f98f5307fd7c5e4e4907a9eb353d0245e0bd) — posix: reset timerfd expiration count when disarming (kills a 100%-CPU timerfd livelock).

### System-info nodes (so htop and desktop system tools show real data)

- [41c269a](https://github.com/netrunner01/managarm/commit/41c269a4f7b4d659d49e4cb250c8f6cb57e0f5e9) — posix: add `/proc/loadavg`, `/proc/stat`, `/proc/cpuinfo`.
- [85761a4](https://github.com/netrunner01/managarm/commit/85761a4b95841ffdddc048c7cce41a81c64593d2) — hel, thor: add `helQueryKernelInfo` for system-wide statistics.
- [9bad586](https://github.com/netrunner01/managarm/commit/9bad586de4f406d05323dbf596e1049016b37dba) — posix: add `/proc/meminfo` and report the real CPU count in `/proc/cpuinfo`.
- [6bc8239](https://github.com/netrunner01/managarm/commit/6bc8239e4f3c9290d0e3560870be750f4b8d8c57) — thor, hel: track cumulative busy CPU time; expose via `helQueryKernelInfo`.
- [0b1b010](https://github.com/netrunner01/managarm/commit/0b1b0101d3f811f74c4b6fb02c63be20c3f3b69a) — posix: report real busy/idle CPU time in `/proc/stat`.

### Filesystem stability under the desktop

- [42a7441](https://github.com/netrunner01/managarm/commit/42a744189237e6e9a28fd0ed844177ebce7ca4e6) — libblockfs: tolerate an out-of-range ext2 writeback racing a truncate (DEF-91; fixed a desktop wedge under disk-write load).

## mlibc — `https://github.com/netrunner01/mlibc`

- [2641e69](https://github.com/netrunner01/mlibc/commit/2641e69ae6281728221eb2f2f6aa91d5878214cd) — sysdeps/managarm: implement `DRM_IOCTL_MODE_OBJ_SETPROPERTY` (client half of DEF-40).
- [448221d](https://github.com/netrunner01/mlibc/commit/448221d76b99c31904310bd7d98114c9dfe30070) — sysdeps/managarm: make DRM buffer teardown reach the server (client half of GEM_CLOSE/CLOSEFB, DEF-23/68/73).
- [c2af5b7](https://github.com/netrunner01/mlibc/commit/c2af5b78aa8768ab81d237b18d0067ad135c8e16) — sysdeps/managarm: `getresuid`/`getresgid` must always write their outputs (unblocks GTK apps).

## bootstrap-managarm — `https://github.com/netrunner01/bootstrap-managarm`

Mostly image-assembly plumbing; the entries below are the ones that directly
affect the desktop the user sees (menu, browser, env, htop). Pure version-pin
bumps are omitted.

- [b60248b](https://github.com/netrunner01/bootstrap-managarm/commit/b60248bc1628ba0b8f0027dc267971e835e02dbc) — populate the weston desktop menu (rootfs overlay).
- [f5dd93f](https://github.com/netrunner01/bootstrap-managarm/commit/f5dd93f168b7cc24f0ac751a14f62bcd289d68a4) — add a web browser (epiphany) to the weston menu.
- [87e4de1](https://github.com/netrunner01/bootstrap-managarm/commit/87e4de1d53bd2c85aaa5862436ea51e53ca2be16) — drop the weston-editor launcher from the panel.
- [c27ea23](https://github.com/netrunner01/bootstrap-managarm/commit/c27ea2377ba40b186ba7251fa14402cb5ff7f8ad) — ship `/etc/environment` with the desktop bring-up env vars.
- [f9a168d](https://github.com/netrunner01/bootstrap-managarm/commit/f9a168d3a3ccdd053fd315e346dfb1c1714a1125) — also build the shared MIME database during image assembly (apps resolve file types).
- [d3d9c01](https://github.com/netrunner01/bootstrap-managarm/commit/d3d9c017e1378cbc516af4ee0ea280c472bf9542) — compile GSettings schemas during image assembly (GTK/GNOME apps need them).
- [6a310f2](https://github.com/netrunner01/bootstrap-managarm/commit/6a310f21f33bee4ffbf23f6f41e8d01f4bab1d21) — htop: re-enable memory + CPU-time scans now that `/proc` provides them.
