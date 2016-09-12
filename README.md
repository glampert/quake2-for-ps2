
# Quake II port for the PlayStation 2

![Raw level geometry](https://raw.githubusercontent.com/glampert/quake2-for-ps2/master/misc/screens/q2ps2-level-notex-2.png "Raw level geometry")

## Overview

This is an unofficial fan made port, targeting the PS2 Console, of the original
[Quake II source code released by id Software][link_id_repo].

This port relies on the free [PS2DEV SDK][link_ps2_dev] to provide rendering,
input, audio and system services for the Quake Engine.

The project is in early development stage, but 2D rendering of menus and cinematics
is already implemented and working on both the Emulator and the PS2.

As you can see in the screenshot above, we also have some basic
hardware-accelerated raw level geometry rendering implemented.

The long term goal is to have a fully functional and playable (single-player)
Quake II on the PlayStation 2, using only on the freely available tools and libraries.

I am not able to actively update this project at the moment, so contributions are very welcome!
If you know C programming well and the basics of games programming and would like to contribute,
the main missing features right now are (with estimated difficulty between the `()`):

- Finish Vector Unit-accelerated rendering (medium, but requires knowledge of the hardware)
- Add texture mapping, lightmaps and dynamic lights (medium/hard, same as above)
- Add sound rendering/mixing for the PS2 (hard, very limited memory left for audio!)
- Add gamepad input (easy)
- Hook `dlmalloc` (easy)
- Optimize memory allocation/usage as much as possible (medium)
- Optimize rendering to ensure smooth 30fps gameplay (medium/hard, it depends...)

If you'd like to help/are keen to play the game on your old PS2, let me know!

## License

Quake II was originally released as GPL, and it remains as such. New code written
for the PS2 port or any changes made to the original source code are also released under the
GNU General Public License version 2. See the accompanying LICENSE file for the details.

You can also find a copy of the GPL version 2 [in here][link_gpl_v2].

[link_id_repo]: https://github.com/id-Software/Quake-2
[link_ps2_dev]: https://github.com/ps2dev
[link_gpl_v2]:  https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html

