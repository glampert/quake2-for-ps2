/* ================================================================================================
 * File: debug_menu.h
 * Brief: The Options menu's "debug" page: the backend's debugging aids on the gamepad, for a
 *        console with no keyboard to type them at the Quake console. The log file, the IOP and
 *        save reports, a dump of the console to a file, and the perf and map cycle tests in the
 *        builds that have them.
 *        NOTE: Shared header between C and C++.
 *
 * This source code is released under the GNU GPL v2 license.
 * ================================================================================================ */
#ifndef PS2_DEBUG_DEBUG_MENU_H
#define PS2_DEBUG_DEBUG_MENU_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Opens the debug page over the menu that asked for it (the Options menu's "debug" row). */
void PS2_DebugMenuOpen(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PS2_DEBUG_DEBUG_MENU_H */
