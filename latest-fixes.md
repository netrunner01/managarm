# Latest fixes — last 24h (2026-08-06 → 2026-08-07)

Go-forward `port/keeper-rederive` (managarm/mlibc) + `port/coherence-pins` (bootstrap), all pushed to the `fork` mirrors. Most are keeper fixes re-derived onto the new base; DEF-92/93, the RB_ENABLE_CAD forward, and the poweroff watchdog are new this window.

**Poweroff / shutdown (DEF-79)**
- posix `bb84758b1` — poweroff watchdog when systemd-shutdown wedges ⚠ temporary workaround
- mlibc `3515c5ed` — forward `reboot(RB_ENABLE_CAD)`→posix (arms the watchdog)
- posix `1421cb667` — netlink LIST/DROP_MEMBERSHIPS · `e7c5b6ef2` — accept NETLINK_NETFILTER (nft stub)
- posix `bccb36681` — umount2 · `4d9d5b71f` — cgroupfs rmdir · mlibc `26ffa465` — umount2 sysdep
- thor `2380af72a` — S5 on a work queue in the pm-interface poweroff path

**AF_UNIX sockets**
- posix `2ef53939b` — clear datagram peer `_remote` on server close (DEF-92 UAF)
- posix `c33551efb` — fix abstract SOCK_DGRAM `connect()` hang (DEF-93)
- posix `52337d77f` — erase pathname sockets from the bind registry on close (DEF-85)
- posix `4ccf01014`/`01a7b0d4c` + thor `560223c86` — kernel-authenticated SCM_CREDENTIALS + bounds

**SIGKILL reaping (DEF-17)**
- posix,thor `3fd80d764` — reap SIGKILL'd interrupt-proof-blocked processes
- posix `d13b37f6e`/`e71548207`/`ef636bc77` — cancellable epoll / poll·ppoll / waitid on serve teardown
- posix `dc77bcb95` — tolerate a force-terminated observed thread on resume

**libblockfs / storage**
- `fb104a95f` — fsync flushes the device write cache
- `c5fc0cca6`/`d4089c181` — ENOSPC instead of aborting on a full disk
- `c600e9104` — ext2 free primitives; free direct blocks on shrink
- `42a744189` — tolerate ext2 writeback racing a truncate (DEF-91)
- `f1a98b697`/`76b2a9250`/`c82e6f501` — refuse bad ext mounts / no-GPT / btrfs fake-ack
- nic/rtl8168 `0f82de25f` — TX backpressure instead of asserting on a full ring

**Robustness — asserts→errors (WI-06)**
- posix `61b1b9c15`,`6e890df66`,`ceceda294`,`0b6aeb3f0`,`89ed81fa6` — user-reachable asserts → graceful errors + bounds
- posix `b6ad9a610` — getdents `_iter` UAF (re-resolve cursor by key)
- posix `ce3def608` — DAC gates on mount/chmod/chown/setgroups · `7bb471e6c` — don't terminate on stop/continue signals
- mlibc `8f4b6818` — honest fcntl locks + capability queries · `c2af5b78` — getresuid/getresgid always write outputs

**System info / display**
- posix `0b1b0101d` + thor `6bc8239e4` — real busy/idle CPU time in `/proc/stat`
- gfx/bochs `6a840823f` — 1280x1024 mode

