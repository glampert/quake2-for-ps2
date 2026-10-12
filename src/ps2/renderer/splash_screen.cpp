/* ================================================================================================
 * File: splash_screen.cpp
 * Brief: The boot splash. See splash_screen.h.
 *
 *  It makes the display calls gs::Init makes - libgraph's interlaced FIELD mode with the flicker
 *  filter - then three GIF transfers: the drawing environment and a black clear, the logo's texels
 *  as an RGBA32 texture, and the logo four times its size with bilinear filtering. The output
 *  stays off until all of it is drawn. Nothing outlives it but the picture: the packets and texels
 *  share one heap block, freed once the GIF has read them, and VRAM is used at fixed spots
 *  libgraph's allocator never hears of, so gs::Init lays out its own from the start of VRAM.
 *
 * This source code is released under the GNU GPL v2 license.
 * ================================================================================================ */

#include "ps2/common.h"
#include "ps2/renderer/splash_screen.h"
#include "ps2/renderer/gif_writer.h"
#include "ps2/builtin/builtin.h"
#include "ps2/system/heap.h"

#include <cstdio>
#include <dma.h>
#include <graph.h>
#include <gs_psm.h>
#include <kernel.h> // SyncDCache

namespace ps2::splash {
namespace {

// gs::Init's first framebuffer in its default layout (ps2_fb_width 640, ps2_fb_height 0,
// ps2_fb_16bit 1, ps2_video_mode auto): the start of VRAM, the standard's full height, 16-bit.
constexpr int kFrameWidth    = 640;
constexpr int kFramePsm      = GS_PSM_16;
constexpr int kFrameVramAddr = 0;

// VRAM word addresses past the tallest such framebuffer, 640x512 at 16 bits, which takes 80 pages
// of 2048 words: the logo's texels, and the z-buffer the environment asks for, never tested or
// written.
constexpr int kVramPageWords   = 2048;
constexpr int kTextureVramAddr = 80 * kVramPageWords;
constexpr int kZBufferVramAddr = 82 * kVramPageWords; // past the two pages of a 64x64 RGBA32 texture

// The extent the GS addresses the logo's texture through: a power of two past its 36x42 texels.
// Region clamping keeps the bilinear taps inside the logo, so nothing past it is ever sampled.
constexpr int kTextureLog2        = 6;
constexpr int kTextureBufferWidth = 1 << kTextureLog2;

// The logo is 36x42 texels; four times that is 144x168, over a third of an NTSC screen's height.
constexpr int kScale = 4;

// Palette index 255 is Quake's see-through one. Here it is the black of the background.
constexpr int kTransparentIndex = 255;

// What the logo's colours are multiplied by, each channel clamping at full. 1 draws the palette's
// own colours, as the main menu does, which looked dark and faded as a splash on black. The logo
// is dark to begin with - its channels average 33 of 255 and none passes 71, so nothing clips
// below about 3.5 - and the 16-bit framebuffer then truncates each channel to 5 bits, a step of 8,
// with no dither here.
constexpr float kColorIntensity = 1.7f;

// The three transfers' room in the block, in qwords. They take: the environment (16) and a clear
// in 20 strips (29), plus the end of the packet (1); the texel upload's chain (9) and the texture
// flush (3); the texture's registers (6), the sprite (4) and the end of the packet (1).
constexpr int kEnvironmentQwords = 64;
constexpr int kUploadQwords      = 32;
constexpr int kDrawQwords        = 32;
constexpr int kPacketQwords      = kEnvironmentQwords + kUploadQwords + kDrawQwords;

// How long to wait for each transfer, in polls of the GIF channel's CHCR. They take microseconds;
// a GIF a launcher left wedged would never finish them, and the boot goes on without the splash
// rather than hang on it.
constexpr int kDmaWaitPolls = 1 << 22;

static bool            s_shown         = false;
static int             s_shownHeight   = 0;
static video::Standard s_shownStandard = video::Standard::Ntsc;

// A palette colour with its three colour channels scaled by kColorIntensity.
u32 ScaleColor(const u32 color)
{
    u32 scaledColor = color & 0xFF000000u;
    for (u32 shift = 0; shift < 24; shift += 8)
    {
        const float channel = static_cast<float>((color >> shift) & 0xFFu) * kColorIntensity;
        scaledColor |= ((channel >= 255.0f) ? 255u : static_cast<u32>(channel)) << shift;
    }
    return scaledColor;
}

bool SendAndWait(qword_t * const data, const int qwords, const bool chain)
{
    if (chain)
    {
        dma_channel_send_chain(DMA_CHANNEL_GIF, data, qwords, 0, 0);
    }
    else
    {
        dma_channel_send_normal(DMA_CHANNEL_GIF, data, qwords, 0, 0);
    }
    return dma_channel_wait(DMA_CHANNEL_GIF, kDmaWaitPolls) == 0;
}

} // namespace

void Show()
{
    const int startMsec = Sys_Milliseconds();

    // No config has been read yet, so this is the console's own standard (see video_mode.h).
    const video::Standard standard = video::Selected();
    const int frameHeight = video::VisibleLines(standard);

    const int logoWidth  = m_main_logo_width;
    const int logoHeight = m_main_logo_height;
    const int numTexels  = logoWidth * logoHeight;

    // The packets first, then the texels, all in one cache-line aligned block.
    const size_t packetBytes = static_cast<size_t>(kPacketQwords) * sizeof(qword_t);
    const size_t blockBytes  = packetBytes + (static_cast<size_t>(numTexels) * sizeof(u32));
    void * const block = heap::TryAllocAligned(heap::MemAlign(64), blockBytes, heap::MemTag::Renderer);
    if (block == nullptr)
    {
        std::printf("Splash: no memory for it, skipped.\n");
        return;
    }

    qword_t * const packet = static_cast<qword_t *>(block);
    u32 * const texels = static_cast<u32 *>(static_cast<void *>(static_cast<u8 *>(block) + packetBytes));
    for (int i = 0; i < numTexels; ++i)
    {
        const int index = m_main_logo_data[i];
        texels[i] = (index == kTransparentIndex) ? 0u : ScaleColor(global_palette[index]);
    }

    // The upload's chain REFs the texels where they are, and a chain send writes back only the
    // chain's own tags (see ps2-platform.md).
    SyncDCache(texels, texels + numTexels);

    dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0);

    // gs::Init's display calls, with the output off until the logo is drawn: the framebuffer holds
    // whatever was last in that VRAM.
    graph_set_mode(GRAPH_MODE_INTERLACED, video::GraphMode(standard), GRAPH_MODE_FIELD, GRAPH_ENABLE);
    graph_disable_output();
    graph_set_screen(0, 0, kFrameWidth, frameHeight);
    graph_set_bgcolor(0, 0, 0);
    graph_set_framebuffer_filtered(kFrameVramAddr, kFrameWidth, kFramePsm, 0, 0);

    framebuffer_t frame = {};
    frame.address = kFrameVramAddr;
    frame.width   = kFrameWidth;
    frame.height  = static_cast<unsigned int>(frameHeight);
    frame.psm     = kFramePsm;
    frame.mask    = 0;

    zbuffer_t zbuffer = {};
    zbuffer.enable  = DRAW_DISABLE;
    zbuffer.method  = ZTEST_METHOD_ALLPASS;
    zbuffer.address = kZBufferVramAddr;
    zbuffer.zsm     = GS_ZBUF_16;
    zbuffer.mask    = 1;

    gs::GifWriter environment(packet, kEnvironmentQwords);
    environment.SetupEnvironment(0, frame, zbuffer);
    environment.Clear(0, 0.0f, 0.0f, static_cast<float>(kFrameWidth), static_cast<float>(frameHeight), 0, 0, 0);
    environment.EndGifPacket();

    gs::GifWriter upload(packet + kEnvironmentQwords, kUploadQwords);
    upload.TextureTransfer(texels, logoWidth, logoHeight, GS_PSM_32, vram::Address(kTextureVramAddr), kTextureBufferWidth);
    upload.TextureFlush();

    texbuffer_t texture = {};
    texture.address         = kTextureVramAddr;
    texture.width           = kTextureBufferWidth;
    texture.psm             = GS_PSM_32;
    texture.info.width      = kTextureLog2;
    texture.info.height     = kTextureLog2;
    texture.info.components = TEXTURE_COMPONENTS_RGB;
    texture.info.function   = TEXTURE_FUNCTION_DECAL;

    clutbuffer_t clut = {};
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.load_method  = CLUT_NO_LOAD;

    lod_t lod = {};
    lod.calculation   = LOD_USE_K;
    lod.max_level     = 0;
    lod.mag_filter    = LOD_MAG_LINEAR;
    lod.min_filter    = LOD_MIN_LINEAR;
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l             = 0;
    lod.k             = 0.0f;

    texwrap_t wrap = {};
    wrap.horizontal = WRAP_REGION_CLAMP;
    wrap.vertical   = WRAP_REGION_CLAMP;
    wrap.minu       = 0;
    wrap.maxu       = logoWidth - 1;
    wrap.minv       = 0;
    wrap.maxv       = logoHeight - 1;

    const int drawWidth  = logoWidth * kScale;
    const int drawHeight = logoHeight * kScale;
    const int drawX      = (kFrameWidth - drawWidth) / 2;
    const int drawY      = (frameHeight - drawHeight) / 2;

    texrect_t rect = {};
    rect.v0.x    = static_cast<float>(drawX);
    rect.v0.y    = static_cast<float>(drawY);
    rect.v0.z    = 0u;
    rect.t0.u    = 0.0f;
    rect.t0.v    = 0.0f;
    rect.v1.x    = static_cast<float>(drawX + drawWidth);
    rect.v1.y    = static_cast<float>(drawY + drawHeight);
    rect.v1.z    = 0u;
    rect.t1.u    = static_cast<float>(logoWidth);
    rect.t1.v    = static_cast<float>(logoHeight);
    rect.color.r = 0x80;
    rect.color.g = 0x80;
    rect.color.b = 0x80;
    rect.color.a = 0x80;
    rect.color.q = 1.0f;

    gs::GifWriter draw(packet + kEnvironmentQwords + kUploadQwords, kDrawQwords);
    draw.TextureBuffer(0, texture, clut);
    draw.TextureSampling(0, lod);
    draw.TextureWrapping(0, wrap);
    draw_disable_blending(); // Read when the sprite is emitted (see gs-renderer.md).
    draw.RectTextured(0, rect);
    draw.EndGifPacket();

    // One after another, as the upload goes as a chain and the rest as plain transfers.
    const bool drawn = SendAndWait(packet, environment.QwordCount(), /*chain=*/false) &&
                       SendAndWait(packet + kEnvironmentQwords, upload.QwordCount(), /*chain=*/true) &&
                       SendAndWait(packet + kEnvironmentQwords + kUploadQwords, draw.QwordCount(), /*chain=*/false);
    if (!drawn)
    {
        // The block stays allocated: a transfer that never finished may yet read it.
        std::printf("Splash: the GIF didn't take its transfers, skipped.\n");
        return;
    }

    graph_enable_output();
    heap::Free(block, blockBytes, heap::MemTag::Renderer);

    s_shown         = true;
    s_shownHeight   = frameHeight;
    s_shownStandard = standard;
    std::printf("Splash: the id logo is up (%s, %dx%d) after %d ms.\n", video::Name(standard), kFrameWidth,
                frameHeight, Sys_Milliseconds() - startMsec);
}

bool IsShownIn(const int vramAddress, const int width, const int height, const int psm, const video::Standard standard)
{
    return s_shown && vramAddress == kFrameVramAddr && width == kFrameWidth && height == s_shownHeight &&
           psm == kFramePsm && standard == s_shownStandard;
}

} // namespace ps2::splash
