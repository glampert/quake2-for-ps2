#pragma once
/* ================================================================================================
 * File: screenshot.h
 * Brief: The "screenshot" command, which id's renderer registers (ref_gl's GL_ScreenShot_f): the
 *        last finished frame, read back out of GS VRAM, written as an uncompressed 24-bit TGA to
 *        <gamedir>/scrnshot/quakeNN.tga, the first number free, as ref_gl names them.
 *        "screenshot <name>" writes scrnshot/<name>.tga instead, replacing one already there,
 *        so a scripted test can name its shots. The Quake port has the same readback
 *        (quake-ps2, gs::DownloadFramebufferRows).
 *
 * This source code is released under the GNU GPL v2 license.
 * ================================================================================================ */

namespace ps2::screenshot {

// Registers the command. Called once from PS2_RefInit, once the GS is up.
void Init();

} // namespace ps2::screenshot
