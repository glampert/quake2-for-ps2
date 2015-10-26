
// glimp_null.c -- null OpenGL context.

#include "client/client.h"

void GLimp_BeginFrame(float camera_separation)
{
}

void GLimp_EndFrame(void)
{
}

int GLimp_Init(void * hinstance, void * hWnd)
{
    return 0;
}

void GLimp_Shutdown(void)
{
}

int GLimp_SetMode(int * pwidth, int * pheight, int mode, qboolean fullscreen)
{
    return 0;
}

void GLimp_AppActivate(qboolean active)
{
}

void GLimp_EnableLogging(qboolean enable)
{
}

void GLimp_LogNewFrame(void)
{
}
