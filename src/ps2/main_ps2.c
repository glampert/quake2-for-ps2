
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
// - move main to a separate file (main_ps2.c)
// - replace raw malloc calls so we can account for memory
// - Need to find this 'ps2mkisofs' to be able to make a proper PS2 ISO!!!
//

//FIXME TEMP testing stuff
#include "client/client.h"
extern refexport_t re;
extern void Test_PS2_Draw2D(void);
extern void Test_PS2_Cinematics(void);

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
    }
#endif //0

    Sys_Quit(); // Shouldn't reach this line.
    return 0;
}
