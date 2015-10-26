
// sys_null.h -- null system driver to aid porting efforts

#include "common/q_common.h"

int curtime;
unsigned sys_frame_time;

//=============================================================================

void Sys_mkdir(char * path)
{
}

void Sys_Error(char * error, ...)
{
    va_list argptr;

    printf("Sys_Error: ");
    va_start(argptr, error);
    vprintf(error, argptr);
    va_end(argptr);
    printf("\n");

    exit(1);
}

void Sys_Quit(void)
{
    exit(0);
}

void Sys_UnloadGame(void)
{
}

void * Sys_GetGameAPI(void * parms)
{
    return NULL;
}

char * Sys_ConsoleInput(void)
{
    return NULL;
}

void Sys_ConsoleOutput(char * string)
{
}

void Sys_SendKeyEvents(void)
{
}

void Sys_AppActivate(void)
{
}

void Sys_CopyProtect(void)
{
}

char * Sys_GetClipboardData(void)
{
    return NULL;
}

void * Hunk_Begin(int maxsize)
{
    return NULL;
}

void * Hunk_Alloc(int size)
{
    return NULL;
}

void Hunk_Free(void * buf)
{
}

int Hunk_End(void)
{
    return 0;
}

int Sys_Milliseconds(void)
{
    return 0;
}

void Sys_Mkdir(char * path)
{
}

char * Sys_FindFirst(char * path, unsigned musthave, unsigned canthave)
{
    return NULL;
}

char * Sys_FindNext(unsigned musthave, unsigned canthave)
{
    return NULL;
}

void Sys_FindClose(void)
{
}

void Sys_Init(void)
{
}

//=============================================================================
//
// QPS2-Null:
//
//=============================================================================

// This is missing on the PS2
struct tm * localtime(const time_t * timep)
{
    static struct tm tdummy;
    return &tdummy;
}

// This is missing on the PS2 as well
int rename(const char * a, const char * b)
{
    return -1;
}

int main(/*int argc, char ** argv*/)
{
    // PS2 main() takes no arguments.
    int argc = 1;
    char * argv[] = { "NULL_SYS" };
    Qcommon_Init(argc, argv);

    while (true)
    {
        Qcommon_Frame(1); // FIXME: use a time delta!
    }

    return 0;
}
