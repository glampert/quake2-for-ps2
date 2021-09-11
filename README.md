
# Quake II port for the PlayStation 2

**NOTE: This project is no longer active and there are no plans to continue development.**

![Raw level geometry](https://raw.githubusercontent.com/glampert/quake2-for-ps2/master/misc/screens/q2ps2-level-notex-2.png "Raw level geometry")

## Overview

This is an unofficial fan made port, targeting the PS2 Console, of the original
[Quake II source code released by id Software][link_id_repo].

This port relies on the free [PS2DEV SDK][link_ps2_dev] to provide rendering,
input, audio and system services for the Quake Engine.

The project is in early development stage, but 2D rendering of menus and cinematics
is implemented and working on both the PCX2 Emulator and the PS2.

As shown in the screenshot above, we also have some basic
hardware-accelerated raw level geometry rendering implemented.

The long term goal would be to have a fully functional and playable (single-player)
Quake II on the PlayStation 2, using only on the freely available tools and libraries.

Some of the main features still missing are:

- Finish Vector Unit-accelerated rendering
- Add texture mapping, lightmaps and dynamic lights
- Add sound rendering/mixing for the PS2
- Add gamepad input
- Hook `dlmalloc`
- Optimize memory allocation/usage as much as possible
- Optimize rendering to ensure smooth 30fps gameplay

## License

Quake II was originally released as GPL, and it remains as such. New code written
for the PS2 port or any changes made to the original source code are also released under the
GNU General Public License version 2. See the accompanying LICENSE file for the details.

You can also find a copy of the GPL version 2 [in here][link_gpl_v2].

[link_id_repo]: https://github.com/id-Software/Quake-2
[link_ps2_dev]: https://github.com/ps2dev
[link_gpl_v2]:  https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html

