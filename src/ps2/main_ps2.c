
/* ================================================================================================
 * -*- C -*-
 * File: main_ps2.c
 * Author: Guilherme R. Lampert
 * Created on: 26/10/15
 * Brief: Main application entry point for QPS2.
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#include "common/q_common.h"

//
// TODO:
//
// - bring up the optimized asm math functions for the PS2 (on math_funcs.c)
//
// - Need to find this 'ps2mkisofs' to be able to make a proper PS2 ISO!!!
//
// - probably add a "welcome screen" right after load to display idSoftware
//   copyright and maybe some self promoting as well ;)
//

//FIXME TEMP testing stuff
#include "client/client.h"
extern refexport_t re;
extern void Test_PS2_Draw2D(void);
extern void Test_PS2_Cinematics(void);

static const char * all_menu_names[] =
{
    "menu_main",
    "menu_game",
    "menu_loadgame",
    "menu_savegame",
    "menu_joinserver",
    "menu_addressbook",
    "menu_startserver",
    "menu_dmoptions",
    "menu_playerconfig",
    "menu_downloadoptions",
    "menu_credits",
    "menu_multiplayer",
    "menu_video",
    "menu_options",
    "menu_keys",
    "menu_quit"
};

enum
{
    NUM_MENUS = sizeof(all_menu_names) / sizeof(all_menu_names[0]),
    MENU_MSEC = 6 * 1000
};

static int next_menu = 0;
static int time_til_next_menu = MENU_MSEC;

/*
================
PS2 main() function
================
*/
int main(void)
{
    //Test_PS2_Draw2D();
    //Test_PS2_Cinematics();

#if 0
    qboolean res;
    int file_len, i;
    void * file_data;
    const char * filename = "mass:/test_file.txt"; // this works on the console!

    Dbg_ScrPrintf("HELLO!\n");
    Sys_LoadIOPModules();
    Sys_PrintLoadedIOPModules(100, &Dbg_ScrPrintf);

	res = Sys_LoadBinaryFile(filename, &file_len, &file_data);
	if (res)
	{
        char * str = (char *)file_data;
        for (i = 0; i < file_len; ++i)
        {
            Dbg_ScrPrintf("%c", str[i]);
        }
	}
    else
    {
		Sys_Error("Failed to open file \"%s\"!", filename);
    }
#endif //0

#if 1
    int time;
    int oldtime;
    int newtime;

    //TODO this has to change if running from cdfs or usb!
    FS_SetDefaultBasePath("mass:");

    // PS2 main() takes no arguments.
    // Fake a default program name argv[].
    int argc = 1;
    char * argv[] = { "QPS2.ELF" };
    Qcommon_Init(argc, argv);

    Cbuf_AddText((char *)all_menu_names[next_menu]);

    oldtime = Sys_Milliseconds();
    for (;;)
    {
        do
        {
            newtime = Sys_Milliseconds();
            time = newtime - oldtime;
        }
        while (time < 1);

        Qcommon_Frame(time);
        oldtime = newtime;

        //TEST cycling all the menus
        if (time_til_next_menu <= 0 && next_menu < NUM_MENUS)
        {
            Cbuf_AddText((char *)all_menu_names[next_menu++]);
            time_til_next_menu = MENU_MSEC;
        }
        time_til_next_menu -= time;
        //TEST cycling all the menus
    }
#endif //0

    Sys_Quit(); // Shouldn't reach this line.
    return 0;
}
