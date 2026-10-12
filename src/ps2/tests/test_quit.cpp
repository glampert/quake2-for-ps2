/* ================================================================================================
 * File: test_quit.cpp
 * Brief: How the unattended tests end. See test_quit.h.
 *
 * This source code is released under the GNU GPL v2 license.
 * ================================================================================================ */

#if PS2_QUAKE_DEBUG || PS2_QUAKE_PROFILE
#include "ps2/common.h"
#include "ps2/tests/test_quit.h"

namespace ps2::test {
namespace {

// Not archived: each boot starts with the tests quitting, as a run armed from config.cfg expects.
static const cvar_t * s_quitWhenDone = nullptr;

} // namespace

void RegisterTestQuitCvar()
{
    s_quitWhenDone = Cvar_Get("ps2_test_quit", "1", 0);
}

bool EndTest(const char * testName, const char * testCvarName)
{
    Cvar_Set(testCvarName, "0");

    if (s_quitWhenDone != nullptr && s_quitWhenDone->value == 0.0f)
    {
        Com_Printf("%s: done, and ps2_test_quit is 0: the game stays up for the next test.\n", testName);
        return false;
    }

    // "quit" through the command buffer rather than Sys_Quit() directly: the tests run from inside
    // PS2_EndFrame with the renderer mid-frame on the stack, and Qcommon_Shutdown would tear it
    // down underneath itself. The buffer runs the command from the top of the next Qcommon_Frame,
    // where CL_Quit_f can shut down in the usual order.
    Com_Printf("%s: done - quitting now (ps2_test_quit 1).\n", testName);
    Cbuf_AddText("quit\n");
    return true;
}

} // namespace ps2::test
#endif // PS2_QUAKE_DEBUG || PS2_QUAKE_PROFILE
