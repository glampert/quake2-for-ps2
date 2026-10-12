/* ================================================================================================
 * File: audsrv_device.cpp
 * Brief: PCM output device over the audsrv IOP driver. See audsrv_device.h.
 *
 *  audsrv keeps a ring buffer on the IOP sized to ten of its 512-sample feed blocks -
 *  9400 bytes, about 100ms, at 22050Hz/16bit/stereo - and a thread that drains it into
 *  the SPU2 93.75 times a second, upsampling to the SPU2's native 48kHz on the way.
 *  Two of its quirks shape everything below:
 *
 *   - audsrv_play_audio() copies only as much as the ring has room for, but the EE-side
 *     wrapper advances its source pointer by the size it was *asked* for. Submitting
 *     more than audsrv_available() therefore drops audio silently.
 *   - The ring reports "write cursor == read cursor" as empty rather than full, so
 *     filling it right to the last byte makes a full queue read back as drained.
 *
 *  FreeBytes() answers both: it is the only size Enqueue() may ever be handed.
 *
 * This source code is released under the GNU GPL v2 license.
 * ================================================================================================ */

#include "ps2/audio/audsrv_device.h"
#include "ps2/system/iop_boot.h"
#include "ps2/common.h"

#include <audsrv.h>
#include <delaythread.h>

#include <cstring>

namespace ps2::audio {
namespace {

static_assert(AudsrvDevice::kFrameBytes == sizeof(u32), "IsSilence reads one stereo frame as a u32");

bool IsSilence(const u8 * const data, const int sizeBytes)
{
    // A frame at a time: everything enqueued is whole 16-bit stereo frames (see FreeBytes).
    for (int i = 0; i < sizeBytes; i += AudsrvDevice::kFrameBytes)
    {
        u32 frame;
        std::memcpy(&frame, data + i, sizeof(frame));
        if (frame != 0)
        {
            return false;
        }
    }
    return true;
}

// Set once the IOP modules are up and audsrv_init() has bound the RPC channel.
// A second audsrv_init() would leak the EE-side RPC thread it spawns.
static bool s_iopStarted = false;

} // namespace

bool AudsrvDevice::StartIopSide()
{
    if (s_iopStarted)
    {
        return true;
    }

    // libsd and audsrv (iop_boot.cpp), loaded once, or the ones running already.
    if (!ps2::sys::StartSoundDriver())
    {
        return false;
    }

    const int err = audsrv_init();
    if (err != AUDSRV_ERR_NOERROR)
    {
        Com_Printf("WARNING: audsrv_init failed (%d: %s)\n", err, audsrv_get_error_string());
        return false;
    }

    s_iopStarted = true;
    return true;
}

bool AudsrvDevice::SetFormat()
{
    audsrv_fmt_t format;
    format.freq     = m_sampleRateHz;
    format.bits     = kSampleBits;
    format.channels = kChannels;

    const int err = audsrv_set_format(&format);
    if (err != AUDSRV_ERR_NOERROR)
    {
        Com_Printf("WARNING: audsrv_set_format(%dHz, %d bit, %d ch) failed (%d: %s)\n",
                   m_sampleRateHz, kSampleBits, kChannels, err, audsrv_get_error_string());
        return false;
    }
    return true;
}

bool AudsrvDevice::Init(const int sampleRateHz)
{
    m_ready  = false;
    m_held   = false;
    m_silent = false;

    if (!StartIopSide())
    {
        return false;
    }

    m_sampleRateHz = sampleRateHz;
    if (!SetFormat())
    {
        return false;
    }

    // audsrv_set_format sizes the ring to ten of the feed blocks its play thread moves to the
    // SPU2 at a time - 512 output samples at 48kHz, in this stream's frames - which is 9400
    // bytes at 22050Hz (audsrv.c). Hold() rewrites all of it.
    m_ringBytes = ((512 * sampleRateHz) / 48000) * kFrameBytes * 10;

    // Quake scales every sample by s_volume as it mixes (snd_scaletable / snd_vol),
    // so the hardware side stays wide open.
    audsrv_set_volume(MAX_VOLUME);

    m_ready = true;
    return true;
}

void AudsrvDevice::Shutdown()
{
    if (!m_ready)
    {
        return;
    }

    // Deliberately not audsrv_quit(): leaving the IOP modules and the audsrv EE RPC
    // thread up makes snd_restart - which cl_main.c triggers on s_khz changes and
    // cinematics - a plain audsrv_set_format() instead of a full teardown. Nothing
    // on the PS2 ever returns from main() anyway.
    audsrv_stop_audio();
    m_ready = false;
}

int AudsrvDevice::FreeBytes() const
{
    if (!m_ready)
    {
        return 0;
    }

    // Hold one frame back so the write cursor can never land exactly on the read
    // cursor, which audsrv reports as an empty ring rather than a full one.
    const int freeBytes = audsrv_available() - kFrameBytes;
    if (freeBytes < kFrameBytes)
    {
        return 0;
    }

    // Whole stereo frames only: audsrv steps its read cursor by four bytes, so a
    // misaligned write cursor would swap left and right for the rest of the session.
    return freeBytes & ~(kFrameBytes - 1);
}

int AudsrvDevice::Enqueue(const u8 * const data, const int sizeBytes)
{
    if (!m_ready || sizeBytes <= 0)
    {
        return 0;
    }

    // audsrv_play_audio restarts a stopped stream, so this ends a hold whether or not
    // Release ran - and Release is what makes the room for it.
    m_held = false;

    // The ring stays all silence for as long as only silence goes in, which during a level
    // load is everything the mixer paints between the loading steps: a Hold then has nothing
    // to play out. Only looked at while it is still true, so normal play pays nothing for it.
    if (m_silent)
    {
        m_silent = IsSilence(data, sizeBytes);
    }

    const int sent = audsrv_play_audio(reinterpret_cast<const char *>(data), sizeBytes);
    return (sent > 0) ? sent : 0;
}

void AudsrvDevice::Hold()
{
    if (!m_ready || m_held)
    {
        return;
    }

    // Silence goes in as the play thread frees room, so what was queued plays out first
    // and nothing is cut short. Once a whole ring's worth has gone in, every byte of the
    // ring is silence, and stopping there leaves nothing that a restart could replay.
    static const u8 kSilence[1024] = {};
    static_assert((sizeof(kSilence) % kFrameBytes) == 0, "Whole stereo frames only");

    // A play thread that isn't draining the ring - audsrv already stopped by something else
    // - would never make room. The ring lasts ~106 ms, so this is plenty for one that is.
    constexpr int kGiveUpMsec = 250;
    constexpr int kPollMicros = 2000;

    const int startMsec = Sys_Milliseconds();
    int written = m_silent ? m_ringBytes : 0; // Silent already: nothing to play out.
    while (written < m_ringBytes)
    {
        const int freeBytes = FreeBytes();
        if (freeBytes > 0)
        {
            int chunk = m_ringBytes - written;
            chunk = (freeBytes < chunk) ? freeBytes : chunk;
            chunk = (static_cast<int>(sizeof(kSilence)) < chunk) ? static_cast<int>(sizeof(kSilence)) : chunk;
            written += Enqueue(kSilence, chunk);
            continue;
        }
        if ((Sys_Milliseconds() - startMsec) > kGiveUpMsec)
        {
            break;
        }
        DelayThread(kPollMicros);
    }

    audsrv_stop_audio();
    m_held   = true;
    m_silent = (written >= m_ringBytes); // Not if it gave up: some of the ring is still stale.
}

void AudsrvDevice::Release()
{
    if (!m_ready || !m_held)
    {
        return;
    }

    m_held = false;
    if (!SetFormat())
    {
        m_ready = false; // As a failed Init: run silent rather than stall on a full ring.
    }
}

void AudsrvDevice::Silence()
{
    Hold();
    Release();
}

} // namespace ps2::audio
