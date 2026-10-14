# Parked: background loading for mid-level models and their skins

*Archived 2026-10-14 from the plan written in late September 2026, after the view-weapon
preload (b86b0c7, 2026-09-25). Nothing in it is implemented. The text below is the plan as
parked, with three path and command references updated. When picking it up, keep in mind:*

- *Line numbers (`view.cpp:2293`, `md2.cpp:694`, ...) and struct offsets are from September
  and have moved. Find the functions by name.*
- *A later measurement changed the picture: `v_rail`'s md2 and skin cost about 8.9 ms, of
  which only about 1.7 ms was I/O (see `.claude/rules/performance.md`). Decode matters as much
  as I/O, so stage 0's gate is likely to bring stage 6 (decode on the loader thread) forward.*
- *Console measurements now exist, which the plan didn't have: EE work runs about 1.5x
  PCSX2's (`performance.md`). Recheck the ~2 ms per-frame pump budget against them.*

Parked in favour of the view-weapon preload (b86b0c7). It remains the only fix for deathmatch
player joins (278 KB loose models) and dropped weapons, and it has no permanent memory cost.

## Context

The perf capture reached 0 dropped frames. The last hitch class is assets that arrive by
configstring **mid-level**: weapon pickups, first switches to a carried weapon, and players joining in
DM. The client loads them synchronously inside `CL_ParseConfigString`, so the frame that parses the
configstring pays for all of it.

| Load (PCSX2 host I/O, `ab.flog`) | ClParse | FsIo |
|---|---|---|
| railgun pickup: `v_rail` md2 + skin, `a_slugs.pcx`, `rg_hum.wav` | 12.2 ms | 2.3 ms |
| slugs ammo pickup: md2 + skin | 3.4 ms | 1.3 ms |

PCSX2's host I/O runs at about 8 MB/s. USB 1.1 on hardware is about 1 MB/s. At that rate:
- `v_rail`'s model and skin (60 KB + 57 KB) take about 120 ms to read, around 7 missed frames.
- A DM player join (`players/male/tris.md2`, a 278 KB loose file, plus a 58 KB skin) takes about 340 ms.

**Scope.** In scope: MD2 and SP2 models, and the skins and sprite frames they load through
`tex::Find(Skin/Sprite)` and `RegisterSkin`. Out of scope: sounds (C engine), pics (`RegisterPic`,
`Draw_*`), and world/sky textures (level load only).

**Rule.** An entity whose model or chosen skin has not finished loading is not drawn.

---

## Findings

1. **Where the loads come from.** Mid-level loads enter through a few paths:
   - `CL_ParseConfigString`: `CS_MODELS` calls `RegisterModel`; `CS_PLAYERSKINS` calls `CL_LoadClientinfo`, which calls `RegisterModel`, `RegisterSkin` and `RegisterPic`.
   - The menu's player model.
   - `V_Gun_Model_f`.

   All of them run outside `BeginRegistration`/`EndRegistration`. Level load (`CL_PrepRefresh`)
   stays synchronous: it already has a loading plaque, and `EndRegistration` sweeps against it.
2. **The client relies on NULL for its fallbacks.** Two places depend on it:
   - `CL_LoadClientinfo` falls back to the male model.
   - `PS2_RegisterSkin` returns the placeholder for a missing file.

   So the existence check stays synchronous. After 029ce2a a pak lookup is a hash probe costing
   microseconds. Only a loose-file lookup costs an IOP round trip.
3. **The main thread never sleeps.** `graph_wait_vsync` (ps2sdk `graph.c`) spins on `GS_CSR`
   through about 11 ms of idle per frame (EE work mean is 5.4 ms). The main thread's only sleeps are
   RPC waits: two audsrv calls per frame, plus any file I/O. A lower-priority thread would get the CPU
   only in those gaps, and with sound off it would never run.
4. **Blocking file I/O sleeps, so a thread can overlap it.**
   - `sceSifCallRpc` waits with `WaitSema`.
   - fio (`host:`) and fileXio (`mass:`) serialise callers with semaphores (`_fio_io_sema`, `__lock_sema_id`).

   So a loader thread can sit in a blocking `read()` while the main thread renders. The other side:
   any main-thread file operation (a sound load, a demo read, `printf` to the IOP tty) waits behind
   the loader's in-flight RPC. That is why the loader's reads are chunked.
5. **Nothing else is thread-safe.** That covers:
   - dlmalloc (`USE_LOCKS 0`) and the heap tag stats.
   - The model and texture caches.
   - The FS globals: `file_from_pak` and the shared `pack_t::handle` file position.
   - The profiler.

   The main thread sits at priority 1 (`InitThread`) and can preempt the loader anywhere. The loader
   therefore does exactly one thing: `lseek`/`read` into buffers the main thread allocated, on
   descriptors nothing else uses.
6. **SIF DMA and the data cache.** fio splits an unaligned read's head and tail at **16 bytes**;
   fileXio splits at 64. With the main thread running during the transfer, a 16-aligned DMA region
   can share a cache line that the main thread dirties and later writes back over the transferred
   bytes. Async destinations must therefore own their cache lines: 64-aligned, allocated to the next
   64-byte boundary. PCSX2 does not emulate the EE cache, so a violation would only show on hardware.
   Assert it instead.
7. **Memory.** `LoadAliasMD2Model` reads in place: header, then the hunk, with no staging copy.
   Keep that. Staging a 278 KB player model would double it at the moment memory is tightest
   (MapCycle peak 29.76 MB).
8. **The draw-side hooks are few, and neither state flag grows a struct.**
   - Models are skipped in `DrawEntities` (`view.cpp:2293`), before the type switch.
   - Skins are checked in `SkinForEntity` (`md2.cpp:694`); shadows and shells skip with their entity.
   - Sprite frames are checked in `DrawSpriteEntity` (`view.cpp:1954`).
   - `ModelInstance` has a free padding byte at offset 69, so it stays 224 bytes.
   - `TexFlags` has seven spare bits (six once the mipmap plan above takes one).

---

## Design

### Loader thread: I/O only (new `src/ps2/system/async_io.{h,cpp}`, `ps2::aio`)
- **Thread.** One EE thread below the main thread's priority 1 (for example 0x20). Static 16 KB stack, `gp_reg = &_gp`.
- **Queue.** A fixed ring of 32 requests `{fd, offset, dest, size}`, single producer (main) and single consumer (loader).
  - The request count is a counting semaphore; the indices are volatile.
  - `Submit()` returns a ticket, and `Poll(ticket)` reports done or failed.
- **Chunked reads.** One `lseek`, then `read()` in chunks of 16 KB (`kChunkBytes`). This bounds how long a main-thread file operation waits behind the loader: about 2 ms on host, about 16 ms on USB.
- **Drain.** `WaitIdle()` sleeps on a semaphore the loader signals when the queue empties. The main thread must never busy-poll a lower-priority thread.
- **Asserts.** Each submission checks that `dest` is 64-aligned (finding 6). Callers allocate to the next 64-byte boundary.
- **Throttle.** Debug-only `ps2_aio_throttle_kbps`: a `nanosleep` after each chunk, to emulate USB (~1000 KB/s) on PCSX2's fast host.
- **Counters.** Bytes read and queue depth, sampled into the frame log by the main thread.

### Sleeping vsync (`gs.cpp`)
- An `INTC_VBLANK_S` handler does `iSignalSema(s_vblankSema); ExitHandler();`. This is the pattern in ps2sdk's `rpc/remote/samples/remote.c`.
- `PresentFramebuffer` drains the semaphore with `PollSema`, then calls `WaitSema`, replacing the `graph_wait_vsync` spin.
- The `VSync` probe keeps wrapping the wait, so "EE work = Frame − VSync" still holds. The loader now runs in the main thread's idle time.

### FS: locate without reading (`filesys.c`, `[PS2_QUAKE]`)
- `FS_LocateFile(name, fs_location_t *)` builds on `FS_FindFile` and returns `{fd, offset, length, looseFile}`:
  - **Pak file:** the pak's own loader descriptor, `pack_t::asyncFd`. The main thread `open()`s it on first use; stdio never touches it.
  - **Loose file:** the `FILE *` from `FS_FindFile`, with `fileno()` as the descriptor. `FS_ReleaseLocation` closes it.
  - It also writes the file's `NoteOpen` note to the frame log.
- `FS_SetGamedir` calls a drain hook (`PS2_DrainBackgroundLoads`) before it closes paks, since a read may be in flight on `asyncFd`.

### Texture cache (`texture.cpp`, `image_load.cpp`)
- **New flag.** `TexFlags::Loading`, with `IsDrawable()` meaning neither Loading nor Failed. `Unload` handles a slot that has no pixels and no VRAM.
- **Registration.** When async is allowed and the type is Skin or Sprite, a cache miss does the following:
  1. `FS_LocateFile`. A missing file returns nullptr, exactly as today.
  2. Register a pixel-less slot flagged Loading.
  3. Allocate a staging buffer (64-aligned, sized to the file) and issue one read.
- **Completion.** Decode from memory, fill in the fields, free the staging buffer, then clear Loading.
- **Decoders.** `image_load` splits `LoadPcx`, `LoadWal` and `LoadTga` into the file read plus `DecodePcx/Wal/Tga(const u8 *, int len, …)`, shared by the sync and async paths.
- **Cheap extra.** Fold `HasTransparentTexels` into the PCX run loop: one compare per RLE run instead of one per texel. This helps level loads too.

### Model cache (`model.cpp`, `model_load.cpp`)
- **New state.** `ModelInstance::state` (Ready/Loading/Failed) goes in the padding byte; the 224-byte `static_assert` still holds.
- **Registration.** A `.md2` or `.sp2` cache miss, when async is allowed, does the following:
  1. `FS_LocateFile`. A missing file returns nullptr.
  2. Take a slot, insert the lookup and mark it Loading.
  3. Read the first 128 bytes into a 64-aligned buffer.
  4. Record the job in a fixed pending table of 16. When the table is full, the load falls back to synchronous.
- **MD2 completion.**
  1. Header lands: validate it, then lay out the hunk: 64-aligned, **frames first** so their read starts on a line boundary, size rounded to 64. Allocate 64-aligned glcmd and skin-name scratch, and issue three reads.
  2. Body lands: expand the glcmds, clamp the normals, free the scratch, and resolve the skins through `tex::Find`, which becomes async on its own. Then mark Ready.
- **SP2 completion.** One read of the whole file into its hunk, then validate. Resolve the frames through `tex::Find`, then mark Ready.
- **Shared code.** Split `LoadAliasMD2Model` into `ValidateAliasHeader`, `LayOutAliasHunk` and `FinishAliasModel`, used by both paths so they cannot drift. Sprites get the same split.
- **One semantic change.** A corrupt model becomes Failed and draws nothing. Today's null-model octahedron came from the synchronous path returning nullptr.

### Draw paths (`view.cpp`, `md2.cpp`)
- `DrawEntities`: `if (!model->IsDrawable()) continue;` after the null check.
- `SkinForEntity`: an undrawable skin skips the entity. That covers the custom player skin in `entity.skin`.
- `DrawSpriteEntity`: an undrawable frame texture skips the entity.

### Frame integration (`ref.cpp`)
- **When loads go async.** A single `registering` flag is set by `PS2_BeginRegistration` and cleared by `PS2_EndRegistration`. Loads are async only while it is clear, which also covers menus.
- **Per frame.** `PS2_BeginFrame`, after the profiler rollover, calls `PumpBackgroundLoads(budget)`:
  - It finishes completed jobs in FIFO order and stops after about 2 ms, always finishing at least one.
  - A read that completed in the last idle window is therefore drawable this frame.
- **Level change.** `PS2_BeginRegistration` sets `registering` first, so every `Find` made during the drain is synchronous. It then calls `aio::WaitIdle()` and pumps with no budget. Everything pending becomes an ordinary cache entry before `EndRegistration` sweeps.

### Frame log
- New events: `MdlLoad` and `TexLoad`, which include their nested `FsIo`. The sync loads and the async finish steps both charge them, so the columns split a load's cost however it ran.
- New counters: `aioKB` (bytes read this frame) and `aioQ` (queue depth).
- A `FLOG#ready,<row>,<name>` line when an asset becomes drawable. `frame_budget.py` pairs it with `FLOG#open` to report request-to-ready latency.

---

## Stages

Each stage builds under `make` and `make release` and stands on its own.

- [ ] **0. Price it.** Add the `MdlLoad`/`TexLoad` events around today's synchronous loads and
  take a perf capture. Split the `v_rail` row into model CPU, skin decode, `FsIo`, and the rest
  (sound and pic). **Gate:** the largest single model or skin finish step fits comfortably on the
  main thread (≤ ~3 ms). Otherwise stage 6 comes forward.
- [ ] **1. Sleeping vsync, on its own.** Rendering should be pixel-identical. **Gate:** in the
  capture, the `Frame`/`VSync` distributions are unchanged, drops stay at 0, and there is no tearing.
- [ ] **2. `ps2::aio` + `FS_LocateFile`, with no users yet.** A debug `ps2_testaio` reads pak
  files both through stdio and through the loader, throttled and not, and compares checksums. This
  proves the thread, the ring, the drain and the alignment rule before any asset depends on them.
- [ ] **3. Async skins and sprite frames.** Includes the decoder split, the fused transparency
  scan, and the skin and sprite-frame skips. **Handoff for your visual pass:** view-weapon and
  player skins appear a few frames late. Nothing should draw with the checkerboard or garbage.
- [ ] **4. Async MD2 and SP2 models.** Includes the loader split, the pending table and the model
  skip. **Handoff:** pick up weapons and ammo, switch weapons, and open the player-setup menu model.
- [ ] **5. Measure.**
  - The perf capture against `ab.flog`: the railgun row's ClParse should fall to the sound and
    pic cost alone, with `MdlLoad`/`TexLoad` moving to later rows.
  - A throttled capture at 1000 KB/s: 0 drops, and `FLOG#ready` latency for `v_rail` around 8–10 frames.
  - MapCycle 39/39 with an unchanged PEAK (the drain runs at every level change).
  - A DM join, for the loose 278 KB player model.
- [ ] **6. (Conditional) Decode on the loader thread.** Only if stage 0 or stage 5 shows finish
  steps pushing EE work toward ~12 ms. The main thread still allocates everything, and the loader
  runs pure-CPU jobs in the main thread's idle time: PCX RLE, glcmd expansion, normal clamp. This
  depends on stage 1.

## Risks

| Risk | Mitigation |
|---|---|
| The main thread does not wake at vblank while the loader runs (EE kernel rescheduling on interrupt return) | The loader is I/O only and briefly on-CPU; stage 1 and 5 captures check that VSync→present stays inside vblank |
| A main-thread file operation waits behind a loader chunk (fio/fileXio lock) | 16 KB chunks; watch sound-load rows and demo reads in the throttled capture |
| DMA destination shares a cache line (fio's 16-byte split), invisible in PCSX2 | 64-aligned, line-padded destinations, asserted in `aio::Submit` |
| The PCSX2 `_request_end` TLB-miss flake during host I/O may become more frequent | A known flake (see `.claude/rules/testing-pcsx2.md`); re-run before investigating |
| Paks closed or level changed with reads in flight | Drain in `PS2_BeginRegistration` and in `FS_SetGamedir` |

## Critical files
`src/ps2/system/async_io.{h,cpp}` (new), `src/ps2/renderer/gs.cpp`, `src/common/filesys.c`
(+ `qcommon.h` declarations), `src/ps2/renderer/texture.{h,cpp}`, `image_load.{h,cpp}`,
`model.{h,cpp}`, `model_load.{h,cpp}`, `md2.cpp`, `view.cpp`, `ref.cpp`, `profile.{h,cpp}`,
`src/tools/scripts/frame_log/frame_budget.py`, Makefile (new source).

## Verification
- Build: `make` and `make release`, clean under `-Werror`.
- Perf: `set ps2_perftest "1"`, then `make run`, then `summarize_flog.py` / `frame_budget.py` / `compare_flog.py` against `ab.flog`.
- Throttled: the same run with `ps2_aio_throttle_kbps 1000`.
- Memory: `ps2_testmaps 1`, `ps2_testmaps_dwell 2`.
- Visual: stages 3 and 4 as listed.

