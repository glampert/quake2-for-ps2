#pragma once
/* ================================================================================================
 * File: map_cycle.h
 * Brief: Memory smoke test that loads every stock map in sequence, in the order the game
 *        plays them, and logs what each one costs.
 *
 *        The point is the *transitions*, not the maps: a map change is the worst moment in
 *        the program, because the outgoing map's data can still be resident while the next
 *        one is built. Playing the units in order reproduces the real sequence of those
 *        transitions, which is where every out-of-memory failure in this port has happened.
 *
 * This source code is released under the GNU GPL v2 license.
 * ================================================================================================ */

#if PS2_QUAKE_DEBUG
namespace ps2::test {

// Advances the map cycle test by one frame. Call every frame from PS2_EndFrame.
// Gated by the "ps2_testmaps" cvar; a no-op when it is 0 and once the last map
// has been visited. "ps2_testmaps_dwell" sets the seconds spent in each map
// after it finishes loading. The pass ends in a quit unless "ps2_test_quit" is 0,
// which leaves the game up and the test ready to run again (test_quit.h).
void RunMapCycle();

} // namespace ps2::test
#endif // PS2_QUAKE_DEBUG
