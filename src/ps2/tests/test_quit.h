#pragma once
/* ================================================================================================
 * File: test_quit.h
 * Brief: How the unattended tests end: quitting the game, or leaving it up for the next one.
 *        "ps2_test_quit" (1 by default, not archived) decides it for the perf test, the map
 *        cycle and the save test. Quitting is what an unattended run wants: it ends the run,
 *        and the log, on a known line. Staying up lets one session on a
 *        console run several tests from the debug page, with nobody there to restart it.
 *        Built only where a test is: debug builds, and profiling ones for the perf test.
 *
 * This source code is released under the GNU GPL v2 license.
 * ================================================================================================ */

#if PS2_QUAKE_DEBUG || PS2_QUAKE_PROFILE
namespace ps2::test {

// Registers "ps2_test_quit". Call once from PS2_RefInit.
void RegisterTestQuitCvar();

// Ends a test that has run to its end. Sets the test's cvar back to 0 - which, for an archived
// one, also disarms it in the config the quit writes - then queues "quit" if ps2_test_quit is
// set, and returns whether it did. When it didn't, the caller resets itself, so the same test or
// another can be started next. 'testName' is for the line it prints.
bool EndTest(const char * testName, const char * testCvarName);

} // namespace ps2::test
#endif // PS2_QUAKE_DEBUG || PS2_QUAKE_PROFILE
