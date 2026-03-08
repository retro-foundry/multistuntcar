//--------------------------------------------------------------------------------------
// File: StuntCarRacer.cpp
//
// SDL/OpenGL runtime entry and game loop.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "platform_sdl_gl.h"

#include "StuntCarRacer.h"
#include "3D_Engine.h"
#include "Backdrop.h"
#include "Track.h"
#include "Car.h"
#include "Car_Behaviour.h"
#include "Opponent_Behaviour.h"
#include "wavefunctions.h"
#include "Atlas.h"
#include "version.h"

#if defined(linux) && !defined(_WIN32)
#include <unistd.h>
#elif defined(_WIN32)
#include <direct.h>
#define chdir _chdir
#endif

#ifdef linux
#define STRING "%S"
#else
#define STRING L"%s"
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

//-----------------------------------------------------------------------------
// Defines, constants, and global variables
//-----------------------------------------------------------------------------

#define HEIGHT_ABOVE_ROAD (100)

#define FURTHEST_Z (131072.0f)
static const double LOGIC_STEP_SECONDS = 1.0 / 50.0;

GameModeType GameMode = TRACK_MENU;

// Both the following are used for keyboard input
UINT keyPress = '\0';
DWORD lastInput = 0;

static IDirectSound8* ds;
IDirectSoundBuffer8* WreckSoundBuffer = NULL;
IDirectSoundBuffer8* HitCarSoundBuffer = NULL;
IDirectSoundBuffer8* GroundedSoundBuffer = NULL;
IDirectSoundBuffer8* CreakSoundBuffer = NULL;
IDirectSoundBuffer8* SmashSoundBuffer = NULL;
IDirectSoundBuffer8* OffRoadSoundBuffer = NULL;
IDirectSoundBuffer8* EngineSoundBuffers[8] = {NULL};

IDirect3DTexture9* g_pAtlas = NULL;

int wideScreen = 0;

static bool bFrameMoved = FALSE;
static double g_logicAccumulator = 0.0;
static double g_lastFrameTime = 0.0;
static double g_timingWindowStart = 0.0;
static uint64_t g_renderFramesInWindow = 0;
static uint64_t g_logicTicksInWindow = 0;
static uint64_t g_logicTickTotal = 0;
static double g_renderFpsDisplay = 0.0;
static double g_logicTickRateDisplay = 0.0;

bool bShowStats = FALSE;
bool bNewGame = FALSE;
bool bPaused = FALSE;
bool bPlayerPaused = FALSE;
bool bOpponentPaused = FALSE;
long bTrackDrawMode = 0;
bool bOutsideView = FALSE;
long engineSoundPlaying = FALSE;
double gameStartTime, gameEndTime;
bool bSuperLeague = FALSE;

#if defined(DEBUG) || defined(_DEBUG)
FILE* out;
bool bTestKey = FALSE;
char OutputFile[] = "SCRlog.txt";
long VALUE1 = 1, VALUE2 = 2, VALUE3 = 3;
#endif

extern long TrackID;
extern long boostReserve, boostUnit, StandardBoost, SuperBoost;
extern long INITIALISE_PLAYER;
extern bool raceFinished, raceWon;
extern long lapNumber[];

// League / Super League variable
extern long damaged_limit;
extern long road_cushion_value;
extern long engine_power;
extern long boost_unit_value;
extern long opp_engine_power;

//-----------------------------------------------------------------------------
// Static variables
//-----------------------------------------------------------------------------
// Player 1 orientation
static long player1_x = 0, player1_y = 0, player1_z = 0;

static long player1_x_angle = (0 << 6), player1_y_angle = (0 << 6), player1_z_angle = (0 << 6);

// Opponent orientation
static long opponent_x = 0, opponent_y = 0, opponent_z = 0;

static float opponent_x_angle = 0.0f, opponent_y_angle = 0.0f, opponent_z_angle = 0.0f;

// Previous logic-tick state for render interpolation
static long prev_player1_x = 0, prev_player1_y = 0, prev_player1_z = 0;
static long prev_player1_x_angle = 0, prev_player1_y_angle = 0, prev_player1_z_angle = 0;
static long prev_opponent_x = 0, prev_opponent_y = 0, prev_opponent_z = 0;
static float prev_opponent_x_angle = 0.0f, prev_opponent_y_angle = 0.0f, prev_opponent_z_angle = 0.0f;
static bool have_prev_car_state = false;

// Viewpoint 1 orientation
static long viewpoint1_x, viewpoint1_y, viewpoint1_z;
static long viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle;

// Target (lookat) point
static long target_x, target_y, target_z;

/**************************************************************************
  DSInit

  Description:
    Initialize all the DirectSound specific stuff
 **************************************************************************/

bool DSInit() {
    HRESULT err;

    //
    //    First create a DirectSound object

    err = DirectSoundCreate8(NULL, &ds, NULL);

    if (err != DS_OK)
        return FALSE;

    //
    //    Now set the cooperation level

    err = ds->SetCooperativeLevel(GetWindowHandle(), DSSCL_NORMAL);

    if (err != DS_OK)
        return FALSE;

    return TRUE;
}

/**************************************************************************
  DSSetMode

    Initialises all DirectSound samples etc

 **************************************************************************/

bool DSSetMode() {
    int i;

    // Amiga channels 1 and 2 are right side, channels 0 and 3 are left side

    if ((WreckSoundBuffer = MakeSoundBuffer(ds, L"WRECK")) == NULL)
        return FALSE;
    WreckSoundBuffer->SetPan(DSBPAN_RIGHT);
    WreckSoundBuffer->SetVolume(AmigaVolumeToMixerGain(64));

    if ((HitCarSoundBuffer = MakeSoundBuffer(ds, L"HITCAR")) == NULL)
        return FALSE;
    HitCarSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 238);
    HitCarSoundBuffer->SetPan(DSBPAN_RIGHT);
    HitCarSoundBuffer->SetVolume(AmigaVolumeToMixerGain(56));

    if ((GroundedSoundBuffer = MakeSoundBuffer(ds, L"GROUNDED")) == NULL)
        return FALSE;
    GroundedSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 400);
    GroundedSoundBuffer->SetPan(DSBPAN_RIGHT);

    if ((CreakSoundBuffer = MakeSoundBuffer(ds, L"CREAK")) == NULL)
        return FALSE;
    CreakSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 238);
    CreakSoundBuffer->SetPan(DSBPAN_RIGHT);
    CreakSoundBuffer->SetVolume(AmigaVolumeToMixerGain(64));

    if ((SmashSoundBuffer = MakeSoundBuffer(ds, L"SMASH")) == NULL)
        return FALSE;
    SmashSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 280);
    SmashSoundBuffer->SetPan(DSBPAN_LEFT);
    SmashSoundBuffer->SetVolume(AmigaVolumeToMixerGain(64));

    if ((OffRoadSoundBuffer = MakeSoundBuffer(ds, L"OFFROAD")) == NULL)
        return FALSE;
    OffRoadSoundBuffer->SetPan(DSBPAN_RIGHT);
    OffRoadSoundBuffer->SetVolume(AmigaVolumeToMixerGain(64));

    if ((EngineSoundBuffers[0] = MakeSoundBuffer(ds, L"TICKOVER")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers[1] = MakeSoundBuffer(ds, L"ENGINEPITCH2")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers[2] = MakeSoundBuffer(ds, L"ENGINEPITCH3")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers[3] = MakeSoundBuffer(ds, L"ENGINEPITCH4")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers[4] = MakeSoundBuffer(ds, L"ENGINEPITCH5")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers[5] = MakeSoundBuffer(ds, L"ENGINEPITCH6")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers[6] = MakeSoundBuffer(ds, L"ENGINEPITCH7")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers[7] = MakeSoundBuffer(ds, L"ENGINEPITCH8")) == NULL)
        return FALSE;

    for (i = 0; i < 8; i++) {
        EngineSoundBuffers[i]->SetPan(DSBPAN_LEFT);
        // Original Amiga volume was 48, but have reduced this for testing
        EngineSoundBuffers[i]->SetVolume(AmigaVolumeToMixerGain(48 / 2));
    }

    return TRUE;
}

/**************************************************************************
  DSTerm
 **************************************************************************/

void DSTerm() {
    if (WreckSoundBuffer)
        WreckSoundBuffer->Release(), WreckSoundBuffer = NULL;
    if (HitCarSoundBuffer)
        HitCarSoundBuffer->Release(), HitCarSoundBuffer = NULL;
    if (GroundedSoundBuffer)
        GroundedSoundBuffer->Release(), GroundedSoundBuffer = NULL;
    if (CreakSoundBuffer)
        CreakSoundBuffer->Release(), CreakSoundBuffer = NULL;
    if (SmashSoundBuffer)
        SmashSoundBuffer->Release(), SmashSoundBuffer = NULL;
    if (OffRoadSoundBuffer)
        OffRoadSoundBuffer->Release(), OffRoadSoundBuffer = NULL;

    for (int i = 0; i < 8; i++) {
        if (EngineSoundBuffers[i])
            EngineSoundBuffers[i]->Release(), EngineSoundBuffers[i] = NULL;
    }

    if (ds)
        ds->Release(), ds = NULL;
}

/*    ======================================================================================= */
/*    Function:        InitialiseData                                                            */
/*                                                                                            */
/*    Description:                                                                            */
/*    ======================================================================================= */

static long InitialiseData(void) {
    long success = FALSE;
#if defined(DEBUG) || defined(_DEBUG)
    errno_t err;

    if ((err = fopen_s(&out, OutputFile, "w")) != 0)
        return FALSE;
#endif

    CreateSinCosTable();

    ConvertAmigaTrack(LITTLE_RAMP);

    // Seed the random-number generator with current time so that
    // the numbers will be different every time we run
    srand((unsigned)time(NULL));

    success = TRUE;

    return (success);
}

/*    ======================================================================================= */
/*    Function:        FreeData                                                                */
/*                                                                                            */
/*    Description:                                                                            */
/*    ======================================================================================= */

static void FreeData(void) {
    FreeTrackData();
    DSTerm();
#if defined(DEBUG) || defined(_DEBUG)
    fclose(out);
#endif
    //    CloseAmigaRecording();
    return;
}

/*    ======================================================================================= */
/*    Function:        GetScreenDimensions                                                        */
/*                                                                                            */
/*    Description:    Provide screen width and height                                            */
/*    ======================================================================================= */

/*    ======================================================================================= */
/*    Function:        GetScreenDimensions                                                            */
/*                                                                                                    */
/*    Description:    Retrieve current screen/backbuffer width and height                    */
/*                                                                                                    */
/*    Parameters:        screen_width  - Output: current screen width in pixels                */
/*                    screen_height - Output: current screen height in pixels                */
/*    ======================================================================================= */

void GetScreenDimensions(long* screen_width, long* screen_height) {
#ifdef linux
    /*const SDL_VideoInfo* info = SDL_GetVideoInfo();
    *screen_width = info->current_w;
    *screen_height = info->current_h; */
    *screen_width = (wideScreen) ? 800 : 640;
    *screen_height = 480;
#else
    const D3DSURFACE_DESC* desc;
    desc = GetBackBufferSurfaceDesc();

    *screen_width = desc->Width;
    *screen_height = desc->Height;
#endif
}

//--------------------------------------------------------------------------------------
// Colours
//--------------------------------------------------------------------------------------

#define NUM_PALETTE_ENTRIES (42 + 6)
//#define    PALETTE_COMPONENT_BITS    (8)        // bits per colour r/g/b component

static PALETTEENTRY SCPalette[NUM_PALETTE_ENTRIES] = {
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},

    // car colours 1
    {0x00, 0x00, 0x00},
    {0x88, 0x00, 0x22},
    {0xaa, 0x00, 0x33},
    {0xcc, 0x00, 0x44},
    {0xee, 0x00, 0x55},
    {0x22, 0x22, 0x33},
    {0x44, 0x44, 0x44},
    {0x33, 0x33, 0x33},

    // car colours 2
    {0x00, 0x00, 0x00},
    {0x22, 0x00, 0x88},
    {0x33, 0x00, 0xaa},
    {0x44, 0x00, 0xcc},
    {0x55, 0x00, 0xee},
    {0x22, 0x22, 0x33},
    {0x44, 0x44, 0x44},
    {0x33, 0x33, 0x33},

    // track colours (i.e. Stunt Car Racer car colours)
    {0x00, 0x00, 0x00},
    {0x99, 0x99, 0x77},
    {0xbb, 0xbb, 0x99},
    {0xff, 0xff, 0x00},
    {0x99, 0xbb, 0x33},
    {0x55, 0x77, 0x77},
    {0x55, 0xbb, 0xff},
    {0x55, 0x99, 0xff},
    {0x33, 0x55, 0x77},
    {0x55, 0x00, 0x00}, // 9
    {0x77, 0x33, 0x33}, //10
    {0x99, 0x55, 0x55},
    {0xdd, 0x99, 0x99}, //12
    {0x77, 0x77, 0x55},
    {0xbb, 0xbb, 0xbb},
    {0xff, 0xff, 0xff},

    // extra track colours (altered super league)
    {51, 51, 119}, // SCR_BASE_COLOUR+16
    {119, 153, 119},
    {85, 153, 85},
    {0x00, 0x00, 0x55}, //19
    {0x33, 0x33, 0x77}, //20
    {0x99, 0x99, 0xdd}, //21
};

DWORD SCRGB(long colour_index) // return full RGB value
{
    return (
        D3DCOLOR_XRGB(SCPalette[colour_index].peRed, SCPalette[colour_index].peGreen, SCPalette[colour_index].peBlue));
}

DWORD Fill_Colour, Line_Colour;

void SetSolidColour(long colour_index) {
    /*
    static DWORD reducedSCPalette[NUM_PALETTE_ENTRIES];
    static long first_time = TRUE;

    // make all reduced palette values on first call
    if (first_time)
        {
        long i;
        for (i = 0; i < NUM_PALETTE_ENTRIES; i++)
            {
            // reduce R/G/B to 5/8 of original
            reducedSCPalette[i] = D3DCOLOR_XRGB((5*SCPalette[i].peRed)/8,
                                           (5*SCPalette[i].peGreen)/8,
                                           (5*SCPalette[i].peBlue)/8);
            }

        first_time = FALSE;
        }

    Fill_Colour = reducedSCPalette[colour_index];
*/
    Fill_Colour = SCRGB(colour_index);
}

void SetLineColour(long colour_index) { Line_Colour = SCRGB(colour_index); }

void SetTextureColour(long colour_index) { Fill_Colour = SCRGB(colour_index); }

#ifdef NOT_USED
/*    ======================================================================================= */
/*    Function:        EnforceConstantFrameRate                                                */
/*                                                                                            */
/*    Description:    Attempt to keep frame rate close to MAX_FRAME_RATE                        */
/*    ======================================================================================= */

static void EnforceConstantFrameRate(long max_frame_rate) {
    static long first_time = TRUE;

    static DWORD last_time_ms;
    DWORD this_time_ms, frame_time_ms;
    DWORD min_frame_time_ms = (1000 / max_frame_rate);
    long remaining_ms; // use long because it is signed (DWORD isn't)

    if (first_time) {
        first_time = FALSE;
        last_time_ms = timeGetTime();
    } else {
        this_time_ms = timeGetTime();
        frame_time_ms = this_time_ms - last_time_ms;

        remaining_ms = static_cast<long>(min_frame_time_ms) - static_cast<long>(frame_time_ms);
        last_time_ms = this_time_ms;
        if (remaining_ms > 0) {
            Sleep(remaining_ms);
            last_time_ms += static_cast<DWORD>(remaining_ms);
        }
    }
    return;
}
#endif

//--------------------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------------------
TTF_Font* g_pFont = NULL;
TTF_Font* g_pFontLarge = NULL;
float GetTextScale() {
    return 1.0f; //TODO
}
GLuint g_pSprite = 0; // Texture for batching text calls
// some helper functions....
void CreateFonts() {
    if (!TTF_WasInit() && TTF_Init() == -1) {
        printf("TTF_Init: %s\n", TTF_GetError());
        exit(1);
    }

    if (g_pFont == NULL) {
        g_pFont = TTF_OpenFont("data/DejaVuSans-Bold.ttf", 15);
    }
    if (g_pFontLarge == NULL) {
        g_pFontLarge = TTF_OpenFont("data/DejaVuSans-Bold.ttf", 25);
    }
    if (g_pFont == NULL || g_pFontLarge == NULL) {
        printf("Could not load font data/DejaVuSans-Bold.ttf: %s\n", TTF_GetError());
        exit(1);
    }
    printf("Font created (%p / %p)\n", g_pFont, g_pFontLarge);
}
void CloseFonts() {
    if (g_pFont != NULL) {
        TTF_CloseFont(g_pFont);
        g_pFont = 0;
    }
    if (g_pFontLarge != NULL) {
        TTF_CloseFont(g_pFontLarge);
        g_pFontLarge = NULL;
    }
}
void LoadTextures() {
    if (!g_pAtlas)
        g_pAtlas = new IDirect3DTexture9();
    g_pAtlas->LoadTexture("data/Bitmap/atlas.png");
    InitAtlasCoord();
    printf("Texture loaded\n");
}
void CreateBuffers(IDirect3DDevice9* pd3dDevice) {
    if (CreatePolygonVertexBuffer(pd3dDevice) != S_OK)
        printf("Error creating PolygonVertexBuffer\n");
    if (CreateTrackVertexBuffer(pd3dDevice) != S_OK)
        printf("Error creating TrackVertexBuffer\n");
    if (CreateShadowVertexBuffer(pd3dDevice) != S_OK)
        printf("Error creating ShadowVertexBuffer\n");
    if (CreateCarVertexBuffer(pd3dDevice) != S_OK)
        printf("Error creating CarVertexBuffer\n");
    if (CreateCockpitVertexBuffer(pd3dDevice) != S_OK)
        printf("Error creating CarVertexBuffer\n");
}
/*    ======================================================================================= */
/*    Function:        CalcTrackMenuViewpoint                                                    */
/*                                                                                            */
/*    Description:    */
/*    ======================================================================================= */

static void CalcTrackMenuViewpoint(void) {
    static long circle_y_angle = 0;

    short sin, cos;
    long centre = (NUM_TRACK_CUBES * CUBE_SIZE) / 2;
    long radius = ((NUM_TRACK_CUBES - 2) * CUBE_SIZE) / PRECISION;

    // Target orientation - centre of world
    target_x = (NUM_TRACK_CUBES * CUBE_SIZE) / 2;
    target_y = 0;
    target_z = (NUM_TRACK_CUBES * CUBE_SIZE) / 2;

    // camera moves in a circle around the track
    if (!bPaused)
        circle_y_angle += 128;
    circle_y_angle &= (MAX_ANGLE - 1);

    GetSinCos(circle_y_angle, &sin, &cos);

    viewpoint1_x = centre + (sin * radius);
    viewpoint1_y = -CUBE_SIZE * 3;
    viewpoint1_z = centre + (cos * radius);

    LockViewpointToTarget(viewpoint1_x, viewpoint1_y, viewpoint1_z, target_x, target_y, target_z, &viewpoint1_x_angle,
                          &viewpoint1_y_angle);
    viewpoint1_z_angle = 0;
}

/*    ======================================================================================= */
/*    Function:        CalcTrackPreviewViewpoint                                                */
/*                                                                                            */
/*    Description:    */
/*    ======================================================================================= */

#define NUM_PREVIEW_CAMERAS (9)

static void CalcTrackPreviewViewpoint(void) {
    // Target orientation - opponent
    target_x = opponent_x, target_y = opponent_y, target_z = opponent_z;

#ifndef PREVIEW_METHOD1
    long centre = (NUM_TRACK_CUBES * CUBE_SIZE) / 2;

    viewpoint1_x = centre;

    if (TrackID == DRAW_BRIDGE)
        viewpoint1_y = opponent_y - (CUBE_SIZE * 5) / 2; // Draw Bridge requires a higher viewpoint
    else
        viewpoint1_y = opponent_y - CUBE_SIZE / 2;

    viewpoint1_z = centre;

    viewpoint1_x += (target_x - viewpoint1_x) / 2;
    viewpoint1_z += (target_z - viewpoint1_z) / 2;

    // lock viewpoint y angle to target
    LockViewpointToTarget(viewpoint1_x, viewpoint1_y, viewpoint1_z, target_x, target_y, target_z, &viewpoint1_x_angle,
                          &viewpoint1_y_angle);
#else
    // cameras - four at corners, four half way along, one at centre
    long camera_x[NUM_PREVIEW_CAMERAS] = {
        CUBE_SIZE, CUBE_SIZE, (NUM_TRACK_CUBES - 1) * CUBE_SIZE, (NUM_TRACK_CUBES - 1) * CUBE_SIZE,
        //
        0, (NUM_TRACK_CUBES / 2) * CUBE_SIZE, (NUM_TRACK_CUBES)*CUBE_SIZE, (NUM_TRACK_CUBES / 2) * CUBE_SIZE,
        //
        (NUM_TRACK_CUBES / 2) * CUBE_SIZE};
    long camera_z[NUM_PREVIEW_CAMERAS] = {
        CUBE_SIZE, (NUM_TRACK_CUBES - 1) * CUBE_SIZE, (NUM_TRACK_CUBES - 1) * CUBE_SIZE, CUBE_SIZE,
        //
        (NUM_TRACK_CUBES / 2) * CUBE_SIZE, (NUM_TRACK_CUBES)*CUBE_SIZE, (NUM_TRACK_CUBES / 2) * CUBE_SIZE, 0,
        //
        (NUM_TRACK_CUBES / 2) * CUBE_SIZE};

    // calculate nearest camera
    long camera, distance, shortest_distance = 0, nearest = 0;
    double o, a;
    for (camera = 0; camera < NUM_PREVIEW_CAMERAS; camera++) {
        o = double(camera_x[camera] - target_x);
        a = double(camera_z[camera] - target_z);
        distance = static_cast<long>(sqrt((o * o) + (a * a)));

        if (camera == 0) {
            shortest_distance = distance;
            nearest = camera;
        } else if (distance < shortest_distance) {
            shortest_distance = distance;
            nearest = camera;
        }
    }

    viewpoint1_x = camera_x[nearest];
    viewpoint1_y = player1_y - CUBE_SIZE / 2;
    viewpoint1_z = camera_z[nearest];

    LockViewpointToTarget(viewpoint1_x, viewpoint1_y, viewpoint1_z, target_x, target_y, target_z, &viewpoint1_x_angle,
                          &viewpoint1_y_angle);
#endif

    viewpoint1_z_angle = 0;
}

/*    ======================================================================================= */
/*    Function:        CalcGameViewpoint                                                        */
/*                                                                                            */
/*    Description:    */
/*    ======================================================================================= */

static void CalcGameViewpoint(void) {
    long x_offset, y_offset, z_offset;

    if (bOutsideView) {
        // set Viewpoint 1 to behind Player 1
        // 04/11/1998 - would probably need to do a final rotation (i.e. of the trig. coefficients)
        //                to allow a viewpoint with e.g. a different X angle to that of the player.
        //                For the car this would mean the following rotations: Y,X,Z, Y,X,Z, X
        //                For the viewpoint this would mean the following rotations: Y,X,Z, X (possibly!)
        CalcYXZTrigCoefficients(player1_x_angle, player1_y_angle, player1_z_angle);

        // vector from centre of car
        x_offset = 0;
        y_offset = 0xc0;
        z_offset = 0x300;
        WorldOffset(&x_offset, &y_offset, &z_offset);
        viewpoint1_x = (player1_x - x_offset);
        viewpoint1_y = (player1_y - y_offset);
        viewpoint1_z = (player1_z - z_offset);

        viewpoint1_x_angle = player1_x_angle;
        //viewpoint1_x_angle = (player1_x_angle + (48<<6)) & (MAX_ANGLE-1);
        viewpoint1_y_angle = player1_y_angle;
        //viewpoint1_y_angle = (player1_y_angle - (64<<6)) & (MAX_ANGLE-1);
        viewpoint1_z_angle = player1_z_angle;
        //viewpoint1_x_angle = 0;
        //viewpoint1_z_angle = 0;
    } else {
        viewpoint1_x = player1_x;
        viewpoint1_y = player1_y - (HEIGHT_ABOVE_ROAD << LOG_PRECISION);
        //        viewpoint1_y = player1_y - (90 << LOG_PRECISION);
        viewpoint1_z = player1_z;

        viewpoint1_x_angle = player1_x_angle;
        viewpoint1_y_angle = player1_y_angle;
        viewpoint1_z_angle = player1_z_angle;
    }
}

//--------------------------------------------------------------------------------------
// Handle updates to the scene
//--------------------------------------------------------------------------------------
static D3DXMATRIX matWorldTrack, matWorldCar, matWorldOpponentsCar;

static float FixedPointToWorldCoord(long value) {
    return static_cast<float>(value) / static_cast<float>(1 << LOG_PRECISION);
}

static float PlayerAngleToRadians(long angle) {
    return (static_cast<float>(angle) * 2.0f * D3DX_PI) / 65536.0f;
}

static long WrappedAngleDelta(long from, long to) {
    long delta = (to - from) & (MAX_ANGLE - 1);
    if (delta > (MAX_ANGLE / 2))
        delta -= MAX_ANGLE;
    return delta;
}

static float LerpWrappedPlayerAngle(long from, long to, float alpha) {
    const float interpolated = static_cast<float>(from) + static_cast<float>(WrappedAngleDelta(from, to)) * alpha;
    return (interpolated * 2.0f * D3DX_PI) / 65536.0f;
}

static float NormalizeRadians(float angle) {
    while (angle > D3DX_PI)
        angle -= 2.0f * D3DX_PI;
    while (angle < -D3DX_PI)
        angle += 2.0f * D3DX_PI;
    return angle;
}

static float LerpWrappedRadians(float from, float to, float alpha) {
    const float delta = NormalizeRadians(to - from);
    return from + delta * alpha;
}

static float LerpFixedCoord(long from, long to, float alpha) {
    const float fromf = static_cast<float>(from);
    const float tof = static_cast<float>(to);
    return (fromf + (tof - fromf) * alpha) / static_cast<float>(1 << LOG_PRECISION);
}

static void BuildCarWorldTransform(D3DXMATRIX* out, float x, float y, float z, float xa, float ya, float za,
                                   float yOffset) {
    D3DXMATRIX matRot, matTemp, matTrans;

    D3DXMatrixIdentity(&matRot);
    D3DXMatrixRotationZ(&matTemp, za);
    D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
    D3DXMatrixRotationX(&matTemp, xa);
    D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
    D3DXMatrixRotationY(&matTemp, ya);
    D3DXMatrixMultiply(&matRot, &matRot, &matTemp);

    D3DXMatrixTranslation(&matTrans, x, -y + yOffset, z);
    D3DXMatrixMultiply(out, &matRot, &matTrans);
}

static void CapturePreviousCarState(void) {
    prev_player1_x = player1_x;
    prev_player1_y = player1_y;
    prev_player1_z = player1_z;
    prev_player1_x_angle = player1_x_angle;
    prev_player1_y_angle = player1_y_angle;
    prev_player1_z_angle = player1_z_angle;

    prev_opponent_x = opponent_x;
    prev_opponent_y = opponent_y;
    prev_opponent_z = opponent_z;
    prev_opponent_x_angle = opponent_x_angle;
    prev_opponent_y_angle = opponent_y_angle;
    prev_opponent_z_angle = opponent_z_angle;

    have_prev_car_state = true;
}

static void UpdateInterpolatedCarTransforms(float alpha) {
    if (!have_prev_car_state)
        CapturePreviousCarState();

    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    const float playerX = LerpFixedCoord(prev_player1_x, player1_x, alpha);
    const float playerY = LerpFixedCoord(prev_player1_y, player1_y, alpha);
    const float playerZ = LerpFixedCoord(prev_player1_z, player1_z, alpha);
    const float playerXa = LerpWrappedPlayerAngle(prev_player1_x_angle, player1_x_angle, alpha);
    const float playerYa = LerpWrappedPlayerAngle(prev_player1_y_angle, player1_y_angle, alpha);
    const float playerZa = LerpWrappedPlayerAngle(prev_player1_z_angle, player1_z_angle, alpha);
    BuildCarWorldTransform(&matWorldCar, playerX, playerY, playerZ, playerXa, playerYa, playerZa, VCAR_HEIGHT / 3.0f);

    const float opponentX = LerpFixedCoord(prev_opponent_x, opponent_x, alpha);
    const float opponentY = LerpFixedCoord(prev_opponent_y, opponent_y, alpha);
    const float opponentZ = LerpFixedCoord(prev_opponent_z, opponent_z, alpha);
    // Opponent angles come from terrain-derived instantaneous values and can wobble when
    // interpolated as Euler angles. Keep orientation at current logic-tick value and
    // interpolate translation only.
    const float opponentXa = opponent_x_angle;
    const float opponentYa = opponent_y_angle;
    const float opponentZa = opponent_z_angle;
    BuildCarWorldTransform(&matWorldOpponentsCar, opponentX, opponentY, opponentZ, opponentXa, opponentYa, opponentZa,
                           VCAR_HEIGHT / 4.0f);
}

static void SetCarWorldTransform(void) {
    BuildCarWorldTransform(&matWorldCar, FixedPointToWorldCoord(player1_x), FixedPointToWorldCoord(player1_y),
                           FixedPointToWorldCoord(player1_z), PlayerAngleToRadians(player1_x_angle),
                           PlayerAngleToRadians(player1_y_angle), PlayerAngleToRadians(player1_z_angle),
                           VCAR_HEIGHT / 3.0f);
}

static void SetOpponentsCarWorldTransform(void) {
    BuildCarWorldTransform(&matWorldOpponentsCar, FixedPointToWorldCoord(opponent_x), FixedPointToWorldCoord(opponent_y),
                           FixedPointToWorldCoord(opponent_z), opponent_x_angle, opponent_y_angle, opponent_z_angle,
                           VCAR_HEIGHT / 4.0f);
}

static void StopEngineSound(void) {
    if (engineSoundPlaying) {
        for (int i = 0; i < 8; i++)
            EngineSoundBuffers[i]->Stop();

        engineSoundPlaying = FALSE;
    }
}

void CALLBACK OnFrameMove(IDirect3DDevice9* pd3dDevice, double fTime, float fElapsedTime, void* pUserContext) {
    static D3DXVECTOR3 vUpVec(0.0f, 1.0f, 0.0f);
    DWORD input = lastInput; // take copy of user input
    D3DXMATRIX matRot, matTemp, matTrans, matView;
    bFrameMoved = FALSE;

    if (GameMode == GAME_OVER) {
        StopEngineSound();
        return;
    }

    if (bPaused) {
        StopEngineSound();
    }

    if (TrackID == NO_TRACK)
        return;

    if ((GameMode == TRACK_PREVIEW) || (GameMode == GAME_IN_PROGRESS)) {
        if (GameMode == GAME_IN_PROGRESS) {
            // Fixed-step engine/audio update (50Hz logic tick).
            if (!bPaused)
                FramesWheelsEngine(EngineSoundBuffers);
        }
    } else if (GameMode == TRACK_MENU) {
        // Stop engine sound if at track menu or if game has finished
        StopEngineSound();
    }

    if ((GameMode == GAME_IN_PROGRESS) && (keyPress == 'R')) {
        // point car in opposite direction
        player1_y_angle += _180_DEGREES;
        player1_y_angle &= (MAX_ANGLE - 1);
        INITIALISE_PLAYER = TRUE;
        keyPress = '\0';
    }

    if (!bPaused)
        MoveDrawBridge();

    // Car behaviour
    if ((GameMode == TRACK_PREVIEW) || (GameMode == GAME_IN_PROGRESS)) {
        if (!bPaused) {
            if ((GameMode == GAME_IN_PROGRESS) && (!bPlayerPaused))
                CarBehaviour(input, &player1_x, &player1_y, &player1_z, &player1_x_angle, &player1_y_angle,
                             &player1_z_angle);

            OpponentBehaviour(&opponent_x, &opponent_y, &opponent_z, &opponent_x_angle, &opponent_y_angle,
                              &opponent_z_angle, bOpponentPaused);
        }

        LimitViewpointY(&player1_y);
    }

    if ((GameMode == TRACK_MENU) || (GameMode == TRACK_PREVIEW)) {
        if (GameMode == TRACK_MENU)
            CalcTrackMenuViewpoint();
        else {
            CalcTrackPreviewViewpoint();

            // Set the car's world transform matrix
            SetOpponentsCarWorldTransform();
        }

        // Set Direct3D transforms, ready for OnFrameRender
        viewpoint1_x >>= LOG_PRECISION;
        // NOTE: viewpoint1_y must be preserved for use by DrawBackdrop
        viewpoint1_z >>= LOG_PRECISION;

        target_x >>= LOG_PRECISION;
        target_y = -target_y;
        target_y >>= LOG_PRECISION;
        target_z >>= LOG_PRECISION;

        // Set the track's world transform matrix
        D3DXMatrixIdentity(&matWorldTrack);

        //
        // Set the view transform matrix
        //
        // Set the eye point
        D3DXVECTOR3 vEyePt(static_cast<float>(viewpoint1_x), static_cast<float>(-viewpoint1_y >> LOG_PRECISION),
                           static_cast<float>(viewpoint1_z));
        // Set the lookat point
        D3DXVECTOR3 vLookatPt(static_cast<float>(target_x), static_cast<float>(target_y), static_cast<float>(target_z));
        D3DXMatrixLookAtLH(&matView, &vEyePt, &vLookatPt, &vUpVec);
        pd3dDevice->SetTransform(D3DTS_VIEW, &matView);
    } else if (GameMode == GAME_IN_PROGRESS) {
        CalcGameViewpoint();

        // Set Direct3D transforms, ready for OnFrameRender
        viewpoint1_x >>= LOG_PRECISION;
        // NOTE: viewpoint1_y must be preserved for use by DrawBackdrop
        viewpoint1_z >>= LOG_PRECISION;

        // Set the track's world transform matrix
        D3DXMatrixIdentity(&matWorldTrack);

        // Set the opponent's car world transform matrix
        /*
        // temp set opponent's position to same as player
        if ((opponent_x == 0) && (opponent_y == 0) && (opponent_z == 0))
        {
            opponent_x = player1_x;
            opponent_y = player1_y + (0xc00 * 256 * 4);    // Subtract amount above road, added by PositionCarAbovePiece()
            opponent_z = player1_z;
            opponent_x_angle = player1_x_angle;
            opponent_y_angle = player1_y_angle;
            opponent_z_angle = player1_z_angle;
        }
        */
        SetOpponentsCarWorldTransform();

        if (bOutsideView) {
            // Set the car's world transform matrix
            SetCarWorldTransform();
        }

        //
        // Set the view transform matrix
        //
        // Produce the translation matrix
        D3DXMatrixTranslation(&matTrans, static_cast<float>(-viewpoint1_x),
                              static_cast<float>(viewpoint1_y >> LOG_PRECISION), static_cast<float>(-viewpoint1_z));
        D3DXMatrixIdentity(&matRot);
        float xa = ((static_cast<float>(-viewpoint1_x_angle) * 2 * D3DX_PI) / 65536.0f);
        float ya = ((static_cast<float>(-viewpoint1_y_angle) * 2 * D3DX_PI) / 65536.0f);
        float za = ((static_cast<float>(-viewpoint1_z_angle) * 2 * D3DX_PI) / 65536.0f);
        // Produce and combine the rotation matrices
#ifdef linux
        D3DXMatrixRotationY(&matTemp, ya + D3DX_PI);
        D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
        D3DXMatrixRotationX(&matTemp, -xa);
        D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
        D3DXMatrixRotationZ(&matTemp, -za);
        D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
#else
        D3DXMatrixRotationY(&matTemp, ya);
        D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
        D3DXMatrixRotationX(&matTemp, xa);
        D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
        D3DXMatrixRotationZ(&matTemp, za);
        D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
#endif
        // Combine the rotation and translation matrices to complete the world matrix
        D3DXMatrixMultiply(&matView, &matTrans, &matRot);
#ifdef linux
        D3DXMatrixScaling(&matTrans, +1, -1, +1);
        D3DXMatrixMultiply(&matView, &matView, &matTrans);
#endif
        pd3dDevice->SetTransform(D3DTS_VIEW, &matView);
    }

    if (!bPaused)
        bFrameMoved = TRUE;
}

/*    ======================================================================================= */
/*    Function:        HandleTrackMenu                                                            */
/*                                                                                            */
/*    Description:    Output track menu text                                                    */
/*    ======================================================================================= */
#ifdef linux
#define FIRSTMENU SDLK_1
#define STARTMENU SDLK_s
#define LEAGUEMENU SDLK_l
#else
#define FIRSTMENU '1'
#define STARTMENU 'S'
#define LEAGUEMENU 'L'
#endif

static void HandleTrackMenu(TextHelper& txtHelper) {
    long i, track_number;
    UINT firstMenuOption, lastMenuOption;
    float textScale = GetTextScale();
    txtHelper.SetInsertionPos(static_cast<int>((2 + (wideScreen ? 10 : 0)) * textScale),
                              static_cast<int>(15 * 8 * textScale));
    txtHelper.DrawTextLine(L"Choose track :-");

    for (i = 0, firstMenuOption = FIRSTMENU; i < NUM_TRACKS; i++) {
        txtHelper.DrawFormattedTextLine(L"'%d' -  " STRING, (i + 1), GetTrackName(i));
    }
    lastMenuOption = i + FIRSTMENU - 1;

    // output instructions
    const D3DSURFACE_DESC* pd3dsdBackBuffer = GetBackBufferSurfaceDesc();
    txtHelper.SetInsertionPos(static_cast<int>((2 + (wideScreen ? 10 : 0)) * textScale),
                              static_cast<int>(pd3dsdBackBuffer->Height - 15 * 8 * textScale));
    txtHelper.DrawFormattedTextLine(L"Current track - " STRING L".  Press 'S' to select, Escape to quit",
                                    (TrackID == NO_TRACK ? L"None" : GetTrackName(TrackID)));
    txtHelper.DrawTextLine(L"'L' to switch Super League On/Off");

    if (((keyPress >= firstMenuOption) && (keyPress <= lastMenuOption)) || (keyPress == LEAGUEMENU)) {
        if (keyPress == LEAGUEMENU) {
            bSuperLeague = !bSuperLeague;
            track_number = TrackID;
            CreateCarVertexBuffer(GetRenderDevice()); // recreate car
        } else
            track_number = keyPress - firstMenuOption; // start at 0

        if (!ConvertAmigaTrack(track_number)) {
#if defined(DEBUG) || defined(_DEBUG)
            fprintf(out, "Failed to convert track %d\n", track_number);
#endif
            MessageBox(NULL, L"Failed to convert track", L"Error", MB_OK); //temp
            return;
        }

        if (CreateTrackVertexBuffer(GetRenderDevice()) != S_OK) {
#if defined(DEBUG) || defined(_DEBUG)
            fprintf(out, "Failed to create track vertex buffer %d\n", track_number);
#endif
            MessageBox(NULL, L"Failed to create track vertex buffer", L"Error", MB_OK); //temp
            return;
        }

        keyPress = '\0';
    }

    if ((keyPress == STARTMENU) && (TrackID != NO_TRACK)) {
        bNewGame = TRUE; // Used here just to reset the opponent's car, which is then shown during the track preview
        ResetPlayer(); // Also reset player to clear values if there was a previous game (CarBehaviour normally does this, but isn't called for track preview)
        GameMode = TRACK_PREVIEW;
        bPlayerPaused = bOpponentPaused = FALSE;
        keyPress = '\0';
    }

    return;
}

/*    ======================================================================================= */
/*    Function:        HandleTrackPreview                                                        */
/*                                                                                            */
/*    Description:    Output track preview text                                                */
/*    ======================================================================================= */

static void HandleTrackPreview(TextHelper& txtHelper) {
    // output instructions
    const D3DSURFACE_DESC* pd3dsdBackBuffer = GetBackBufferSurfaceDesc();
    float textScale = GetTextScale();
    txtHelper.SetInsertionPos(static_cast<int>((2 + (wideScreen ? 10 : 0)) * textScale),
                              static_cast<int>(pd3dsdBackBuffer->Height - 15 * 9 * textScale));
    txtHelper.DrawFormattedTextLine(L"Selected track - " STRING L".  Press 'S' to start game",
                                    (TrackID == NO_TRACK ? L"None" : GetTrackName(TrackID)));
    txtHelper.DrawTextLine(L"'M' for track menu, Escape to quit");
    txtHelper.DrawTextLine(L"(Press F4 to change scenery)");

    txtHelper.SetInsertionPos(static_cast<int>((2 + (wideScreen ? 10 : 0)) * textScale),
                              static_cast<int>(pd3dsdBackBuffer->Height - 15 * 6 * textScale));
    txtHelper.DrawTextLine(L"Keyboard controls during game :-");
#if defined(PANDORA) || defined(PYRA)
    txtHelper.DrawTextLine(L"  DPad = Steer, (X) = Accelerate, (B) = Brake, (R) = Nitro");
#else
    txtHelper.DrawTextLine(
        L"  Arrow left = Steer left, Arrow right = Steer right, Space = Accelerate, Arrow Down = Brake");
#endif
    txtHelper.DrawTextLine(L"  R = Point car in opposite direction, P = Pause, O = Unpause");
    txtHelper.DrawTextLine(L"  M = Back to track menu, Escape = Quit");

    if (keyPress == STARTMENU) {
        bNewGame = TRUE;
        GameMode = GAME_IN_PROGRESS;
        // initialise game data
        ResetLapData(OPPONENT);
        ResetLapData(PLAYER);
        gameStartTime = GetTimeSeconds();
        gameEndTime = 0;
        if (bSuperLeague) {
            boostReserve = SuperBoost;
            road_cushion_value = 1;
            engine_power = 320;
            boost_unit_value = 12;
            opp_engine_power = 314;
        } else {
            boostReserve = StandardBoost; // SuperBoost for super league
            road_cushion_value = 0;
            engine_power = 240;
            boost_unit_value = 16;
            opp_engine_power = 236;
        }
        boostUnit = 0;
        bPlayerPaused = bOpponentPaused = FALSE;
        keyPress = '\0';
    }

    return;
}

//--------------------------------------------------------------------------------------
// Render the help and statistics text. This function uses the ID3DXFont interface for
// efficient text rendering.  Also render text specific to GameMode.
//--------------------------------------------------------------------------------------
extern long new_damage;
extern long opponentsID;
extern WCHAR* opponentNames[];

void RenderText(double fTime) {
    // The helper object simply helps keep track of text position, and color
    // and then it calls pFont->DrawText( m_pSprite, strMsg, -1, &rc, DT_NOCLIP, m_clr );
    // If NULL is passed in as the sprite object, then it will work fine however the
    // pFont->DrawText() will not be batched together.  Batching calls will improve perf.
    float textScale = GetTextScale();
    static TextHelper txtHelper(g_pFont, g_pSprite, static_cast<int>(15 * textScale));

    // Output statistics
    txtHelper.Begin();
    txtHelper.SetForegroundColor(D3DXCOLOR(1.0f, 1.0f, 0.0f, 1.0f));
    if (bShowStats) {
        txtHelper.SetInsertionPos(static_cast<int>((2 + (wideScreen ? 10 : 0)) * textScale), 0);
        txtHelper.DrawFormattedTextLine(L"fTime: %0.1f  sin(fTime): %0.4f", fTime, sin(fTime));
        txtHelper.DrawFormattedTextLine(L"Render FPS: %5.1f  Logic: %5.1f Hz  Tick#: %.0f", g_renderFpsDisplay,
                                        g_logicTickRateDisplay, static_cast<double>(g_logicTickTotal));

#if defined(DEBUG) || defined(_DEBUG)
        // Output VALUE1, VALUE, VALUE3
        txtHelper.DrawFormattedTextLine(L"V1: %08x, V2: %08x, V3: %08x", VALUE1, VALUE2, VALUE3);
#else
        // Output version
        txtHelper.DrawTextLine(L"Version 1.0");
#endif
    }

    switch (GameMode) {
    case TRACK_MENU:
        HandleTrackMenu(txtHelper);
        txtHelper.End();
        break;

    case TRACK_PREVIEW:
        HandleTrackPreview(txtHelper);
        txtHelper.End();
        break;

    case GAME_IN_PROGRESS:
    case GAME_OVER:
        // Show car speed, damage and race details
        const D3DSURFACE_DESC* pd3dsdBackBuffer = GetBackBufferSurfaceDesc();
        WCHAR lapText[3] = L"  ";
        // Output opponent's name for four seconds at race start
        if (((GetTimeSeconds() - gameStartTime) < 4.0) && (opponentsID != NO_OPPONENT)) {
            txtHelper.SetInsertionPos(static_cast<int>((250 + (wideScreen ? 80 : 0)) * textScale),
                                      static_cast<int>(pd3dsdBackBuffer->Height - 15 * 20 * textScale));
            txtHelper.DrawFormattedTextLine(L"Opponent: " STRING, opponentNames[opponentsID]);
        }
        txtHelper.SetInsertionPos(static_cast<int>((2 + (wideScreen ? 80 : 0)) * textScale),
                                  static_cast<int>(pd3dsdBackBuffer->Height - 15 * 2 * textScale));
        if (lapNumber[PLAYER] > 0)
            StringCchPrintf(lapText, 3, L"%d", lapNumber[PLAYER]);
        txtHelper.SetForegroundColor(D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f));

        // Position text using base 800x480 coordinates, then scale
        float base_height = static_cast<float>(BASE_HEIGHT);
        float scaleY = static_cast<float>(pd3dsdBackBuffer->Height) / base_height;

        // Boost text - positioned in top dashboard box
        txtHelper.SetInsertionPos(static_cast<int>((88 + (wideScreen ? 80 : 0)) * textScale),
                                  static_cast<int>((BASE_HEIGHT - 48.0f) * scaleY));
        txtHelper.DrawFormattedTextLine(L"L" STRING L"       B%02d", lapText,
                                        boostReserve); // Distance text - positioned in bottom dashboard box
        txtHelper.SetInsertionPos(static_cast<int>((84 + (wideScreen ? 80 : 0)) * textScale),
                                  static_cast<int>((BASE_HEIGHT - 25.0f) * scaleY));
        txtHelper.DrawFormattedTextLine(L"        %+05d", CalculateOpponentsDistance());

        txtHelper.End();

        if (raceFinished) {
#ifdef linux
            static
#endif
                TextHelper txtHelperLarge(g_pFontLarge, g_pSprite, static_cast<int>(25 * textScale));

            txtHelperLarge.Begin();

            double currentTime = GetTimeSeconds(), diffTime;
            if (gameEndTime == 0.0)
                gameEndTime = currentTime;

            // Show race finished text for six seconds, then end the game
            diffTime = currentTime - gameEndTime;
            if (diffTime > 6.0) {
                GameMode = GAME_OVER;
            }

            if (GameMode == GAME_OVER) {
#ifdef linux
                txtHelperLarge.SetInsertionPos(static_cast<int>((250 + (wideScreen ? 80 : 0)) * textScale),
                                               static_cast<int>(pd3dsdBackBuffer->Height - 25 * 13 * textScale));
                txtHelperLarge.DrawTextLine(L"GAME OVER");
                txtHelperLarge.SetInsertionPos(static_cast<int>((132 + (wideScreen ? 80 : 0)) * textScale),
                                               static_cast<int>(pd3dsdBackBuffer->Height - 25 * 11 * textScale));
                txtHelperLarge.DrawTextLine(L"Press 'M' for track menu");
#else
                txtHelperLarge.SetInsertionPos(static_cast<int>((124 + (wideScreen ? 80 : 0)) * textScale),
                                               static_cast<int>(pd3dsdBackBuffer->Height - 25 * 12 * textScale));
                txtHelperLarge.DrawTextLine(L"GAME OVER: Press 'M' for track menu");
#endif
            } else {
                long intTime = static_cast<long>(diffTime);
                // Text flashes white/black, changing every half second
                if ((diffTime - (double)intTime) < 0.5)
                    txtHelperLarge.SetForegroundColor(D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f));
                else
                    txtHelperLarge.SetForegroundColor(D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f));

                txtHelperLarge.SetInsertionPos(static_cast<int>((250 + (wideScreen ? 80 : 0)) * textScale),
                                               static_cast<int>(pd3dsdBackBuffer->Height - 25 * 12 * textScale));

                if (raceWon)
                    txtHelperLarge.DrawTextLine(L"RACE WON");
                else
                    txtHelperLarge.DrawTextLine(L"RACE LOST");
            }

            txtHelperLarge.End();
        }
        break;
    }
    //    VALUE2 = raceFinished ? 1 : 0;
    //    VALUE3 = (long)gameEndTime;
}

#ifdef NOT_USED
//-----------------------------------------------------------------------------
// Name: SetupLights()
// Desc: Sets up the lights and materials for the scene.
//-----------------------------------------------------------------------------
void SetupLights(IDirect3DDevice9* pd3dDevice) {
    D3DXVECTOR3 vecDir;
    D3DLIGHT9 light;

    // Set up a material. The material here just has the diffuse and ambient
    // colors set to white. Note that only one material can be used at a time.
    D3DMATERIAL9 mtrl;
    ZeroMemory(&mtrl, sizeof(D3DMATERIAL9));
    mtrl.Diffuse.r = mtrl.Ambient.r = 1.0f;
    mtrl.Diffuse.g = mtrl.Ambient.g = 1.0f;
    mtrl.Diffuse.b = mtrl.Ambient.b = 1.0f;
    mtrl.Diffuse.a = mtrl.Ambient.a = 1.0f;
    pd3dDevice->SetMaterial(&mtrl);

    /*
    // Set up a white spotlight
    ZeroMemory( &light, sizeof(D3DLIGHT9) );
    light.Type       = D3DLIGHT_SPOT;
    light.Diffuse.r  = 1.0f;
    light.Diffuse.g  = 1.0f;
    light.Diffuse.b  = 1.0f;
    // Set position vector
//    light.Position = D3DXVECTOR3(32768.0f, 1000.0f, 32768.0f);
    if (GameMode == TRACK_MENU)
    {
        light.Position.x = 32768.0f;
        light.Position.y = 16384.0f;
        light.Position.z = 32768.0f;
    }
    else
    {
        light.Position.x = (player1_x>>LOG_PRECISION);
        light.Position.y = 16384.0f;
        light.Position.z = (player1_z>>LOG_PRECISION);
    }
    // Set direction vector to simulate sunlight
    vecDir = D3DXVECTOR3(0.0f, -1.0f, 0.0f);
    D3DXVec3Normalize( (D3DXVECTOR3*)&light.Direction, &vecDir );
    light.Range       = 32768;//((float)sqrt(FLT_MAX));
    light.Falloff = 1.0f;
    light.Attenuation0 = 1.0f;
    light.Attenuation1 = 0.0f;
    light.Attenuation2 = 0.0f;
    light.Theta = PI/3;
    light.Phi = PI/2;
    pd3dDevice->SetLight( 0, &light );
    pd3dDevice->LightEnable( 0, TRUE );
    */

    /**/
    // Set up four white, directional lights
    ZeroMemory(&light, sizeof(D3DLIGHT9));
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = 0.33f;
    light.Diffuse.g = 0.33f;
    light.Diffuse.b = 0.33f;
    // Set direction vector to simulate sunlight
    vecDir = D3DXVECTOR3(0.2f, -0.7f, 0.5f);
    D3DXVec3Normalize((D3DXVECTOR3*)&light.Direction, &vecDir);
    light.Range = 10000.0f;
    pd3dDevice->SetLight(1, &light);
    pd3dDevice->LightEnable(1, TRUE);
    /**/
    vecDir = D3DXVECTOR3(0.2f, -0.7f, -0.5f);
    D3DXVec3Normalize((D3DXVECTOR3*)&light.Direction, &vecDir);
    pd3dDevice->SetLight(2, &light);
    pd3dDevice->LightEnable(2, TRUE);
    /**/
    vecDir = D3DXVECTOR3(-0.2f, -0.7f, 0.5f);
    D3DXVec3Normalize((D3DXVECTOR3*)&light.Direction, &vecDir);
    pd3dDevice->SetLight(3, &light);
    pd3dDevice->LightEnable(3, TRUE);
    /**/
    vecDir = D3DXVECTOR3(-0.2f, -0.7f, -0.5f);
    D3DXVec3Normalize((D3DXVECTOR3*)&light.Direction, &vecDir);
    pd3dDevice->SetLight(4, &light);
    pd3dDevice->LightEnable(4, TRUE);
    /**/

    // Finally, turn on some ambient light and turn lighting on
    pd3dDevice->SetRenderState(D3DRS_AMBIENT, 0x00303030);
    pd3dDevice->SetRenderState(D3DRS_LIGHTING, TRUE);
}
#endif

//--------------------------------------------------------------------------------------
// Render the scene
//--------------------------------------------------------------------------------------

void CALLBACK OnFrameRender(IDirect3DDevice9* pd3dDevice, double fTime, float fElapsedTime, void* pUserContext) {
    HRESULT hr;

    //    // Clear the render target and the zbuffer
    //    V( pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 45, 50, 170), 1.0f, 0) );

    // Clear the zbuffer
    V(pd3dDevice->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, 1.0f, 0));

    // Render the scene
    if (SUCCEEDED(pd3dDevice->BeginScene())) {
        // Disable Z buffer and polygon culling, ready for DrawBackdrop()
        pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

        // Draw Backdrop
        DrawBackdrop(viewpoint1_y, viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle);

        //        SetupLights(pd3dDevice);

        // Draw Track
        pd3dDevice->SetTransform(D3DTS_WORLD, &matWorldTrack);
        DrawTrack(pd3dDevice);

        switch (GameMode) {
        case TRACK_MENU:
            break;

        case TRACK_PREVIEW:
            // Draw Opponent's Car
            pd3dDevice->SetTransform(D3DTS_WORLD, &matWorldOpponentsCar);
            DrawCar(pd3dDevice);
            break;

        case GAME_IN_PROGRESS:
        case GAME_OVER:
            // Draw Opponent's Car
            pd3dDevice->SetTransform(D3DTS_WORLD, &matWorldOpponentsCar);
            DrawCar(pd3dDevice);

            if (bOutsideView) {
                // Draw Player1's Car
                pd3dDevice->SetTransform(D3DTS_WORLD, &matWorldCar);
                DrawCar(pd3dDevice);
            } else {
                // draw cockpit...
                DrawCockpit(pd3dDevice);
            }
            break;
        }

        if (GameMode == GAME_IN_PROGRESS) {
            DrawOtherGraphics();

            //jsr    display.speed.bar
            if (bFrameMoved)
                UpdateDamage();

            UpdateLapData();
            //jsr    display.opponents.distance
        }

        RenderText(fTime);

        // End the scene
        pd3dDevice->EndScene();
    }
}

bool process_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYDOWN:
            keyPress = event.key.keysym.sym;
            // some special cases for French keyboards
            if ((event.key.keysym.mod & KMOD_SHIFT) == 0)
                switch (event.key.keysym.sym) {
                case SDLK_AMPERSAND:
                    keyPress = SDLK_1;
                    break;
                case 233:
                    keyPress = SDLK_2;
                    break;
                case SDLK_QUOTEDBL:
                    keyPress = SDLK_3;
                    break;
                case SDLK_QUOTE:
                    keyPress = SDLK_4;
                    break;
                case SDLK_LEFTPAREN:
                    keyPress = SDLK_5;
                    break;
                case SDLK_MINUS:
                    keyPress = SDLK_6;
                    break;
                case 232:
                    keyPress = SDLK_7;
                    break;
                case SDLK_UNDERSCORE:
                    keyPress = SDLK_8;
                    break;
                case 231:
                    keyPress = SDLK_9;
                    break;
                case 224:
                    keyPress = SDLK_0;
                    break;
                }
            switch (keyPress) {
#if defined(DEBUG) || defined(_DEBUG)
            case SDLK_F1:
                bTestKey = !bTestKey;
                break;
#endif
            case SDLK_F2:
                ++bTrackDrawMode;
                if (bTrackDrawMode > 1)
                    bTrackDrawMode = 0;
                ResetRenderEnvironment();
                break;

            case SDLK_F4:
                NextSceneryType();
                break;

            case SDLK_F5:
                bShowStats = !bShowStats;
                break;

            case SDLK_F6:
                bPlayerPaused = !bPlayerPaused;
                break;

            case SDLK_F7:
                bOpponentPaused = !bOpponentPaused;
                break;

#if defined(DEBUG) || defined(_DEBUG)
            case SDLK_BACKSPACE:
                bOutsideView = !bOutsideView;
                break;
#endif
            case SDLK_m:
                if (GameMode != TRACK_MENU) {
                    GameMode = TRACK_MENU;

                    opponentsID = NO_OPPONENT;

                    // reset all animated objects
                    ResetDrawBridge();
                }
                break;

            case SDLK_o:
                bPaused = FALSE;
                break;

            case SDLK_p:
                bPaused = TRUE;
                break;

            case SDLK_z:
                bNewGame = TRUE; // for testing to try stopping car positioning bug
                break;

            // controls for Car Behaviour, Player 1
            case SDLK_LEFT:
                lastInput |= KEY_P1_LEFT;
                break;

            case SDLK_RIGHT:
                lastInput |= KEY_P1_RIGHT;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_RCTRL:
#else
            case SDLK_SPACE:
            case SDLK_RSHIFT:
            case SDLK_LSHIFT:
#endif
                lastInput |= KEY_P1_BOOST;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_END:
#else
            case SDLK_DOWN:
#endif
                lastInput |= KEY_P1_BRAKE;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_PAGEDOWN:
#else
            case SDLK_UP:
#endif
                lastInput |= KEY_P1_ACCEL;
                break;

            case SDLK_ESCAPE:
                return false;
            }
            break;
        case SDL_KEYUP:
            keyPress = 0;
            switch (event.key.keysym.sym) {
            // controls for Car Behaviour, Player 1
            case SDLK_LEFT:
                lastInput &= ~KEY_P1_LEFT;
                break;

            case SDLK_RIGHT:
                lastInput &= ~KEY_P1_RIGHT;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_RCTRL:
#else
            case SDLK_SPACE:
            case SDLK_RSHIFT:
            case SDLK_LSHIFT:
#endif
                lastInput &= ~KEY_P1_BOOST;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_END:
#else
            case SDLK_DOWN:
#endif
                lastInput &= ~KEY_P1_BRAKE;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_PAGEDOWN:
#else
            case SDLK_UP:
#endif
                lastInput &= ~KEY_P1_ACCEL;
                break;
            }
            break;
        case SDL_QUIT:
            return false;
        }
    }
    return true;
}

IDirect3DDevice9 pd3dDevice;
#ifdef USE_SDL2
SDL_Window* window = NULL;
#endif

static void RenderCurrentFrame(double frameTime, float frameDelta) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    OnFrameRender(&pd3dDevice, frameTime, frameDelta, NULL);
#ifdef USE_SDL2
    SDL_GL_SwapWindow(window);
#else
    SDL_GL_SwapBuffers();
#endif
}

static bool RunFrame(double frameTime, bool allowQuit) {
    bool run = true;
    if (allowQuit)
        run = process_events();
    else
        process_events();

    if (g_lastFrameTime <= 0.0)
        g_lastFrameTime = frameTime;
    if (g_timingWindowStart <= 0.0)
        g_timingWindowStart = frameTime;

    double frameDelta = frameTime - g_lastFrameTime;
    if (frameDelta < 0.0)
        frameDelta = 0.0;
    if (frameDelta > 0.25)
        frameDelta = 0.25;
    g_lastFrameTime = frameTime;
    g_logicAccumulator += frameDelta;

    bool anyLogicFrameMoved = false;
    while (g_logicAccumulator >= LOGIC_STEP_SECONDS) {
        CapturePreviousCarState();
        OnFrameMove(&pd3dDevice, frameTime, static_cast<float>(LOGIC_STEP_SECONDS), NULL);
        ++g_logicTicksInWindow;
        ++g_logicTickTotal;
        if (bFrameMoved)
            anyLogicFrameMoved = true;
        g_logicAccumulator -= LOGIC_STEP_SECONDS;
    }
    bFrameMoved = anyLogicFrameMoved;

    if ((GameMode == TRACK_PREVIEW) || (GameMode == GAME_IN_PROGRESS)) {
        const float alpha = static_cast<float>(g_logicAccumulator / LOGIC_STEP_SECONDS);
        UpdateInterpolatedCarTransforms(alpha);
    }

    RenderCurrentFrame(frameTime, static_cast<float>(frameDelta));
    ++g_renderFramesInWindow;

    const double timingWindowElapsed = frameTime - g_timingWindowStart;
    if (timingWindowElapsed >= 0.5) {
        g_renderFpsDisplay = static_cast<double>(g_renderFramesInWindow) / timingWindowElapsed;
        g_logicTickRateDisplay = static_cast<double>(g_logicTicksInWindow) / timingWindowElapsed;
        g_renderFramesInWindow = 0;
        g_logicTicksInWindow = 0;
        g_timingWindowStart = frameTime;
    }
    return run;
}

#ifdef __EMSCRIPTEN__
extern "C" void initialize_gl4es();
void em_main_loop() {
    RunFrame(GetTimeSeconds(), false);
}
#endif

int GL_MSAA = 0;
int main(int argc, const char** argv) {
#ifdef __EMSCRIPTEN__
    initialize_gl4es();
#endif
    char maintitle[50] = {0};
    sprintf(maintitle, "StuntCarRemake v%d.%02d.%02d", V_MAJOR, V_MINOR, V_PATCH);
    printf("%s\n", maintitle);
    // Run from the executable directory so relative assets (data/Bitmap, data/Sounds, data/Tracks) resolve.
    char* basePath = SDL_GetBasePath();
    if (basePath && basePath[0] != '\0') {
        if (chdir(basePath) == 0)
            printf("chdir(\"%s\")\n", basePath);
        SDL_free(basePath);
    }
#ifdef USE_SDL2
    SDL_GLContext context = NULL;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK) == -1) {
        printf("Could not initialise SDL2: %s\n", SDL_GetError());
        exit(-1);
    }
#else
    SDL_Surface* screen = NULL;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTTHREAD) == -1) {
        printf("Could not initialise SDL: %s\n", SDL_GetError());
        exit(-1);
    }
#endif
    atexit(SDL_Quit);

    TTF_Init();

    // crude command line parameter reading
    int nomsaa = 0;
    int fullscreen = 0;
    int desktop = 0;
    int givehelp = 0;
    int customWidth = 0;
    int customHeight = 0;
    float customScale = 0.0f;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f"))
            fullscreen = 1;
        else if (!strcmp(argv[i], "--fullscreen"))
            fullscreen = 1;
        else if (!strcmp(argv[i], "-d"))
            desktop = 1;
        else if (!strcmp(argv[i], "--desktop"))
            desktop = 1;
        else if (!strcmp(argv[i], "-n"))
            nomsaa = 1;
        else if (!strcmp(argv[i], "--nomsaa"))
            nomsaa = 1;
        else if ((!strcmp(argv[i], "-w") || !strcmp(argv[i], "--width")) && i + 1 < argc) {
            customWidth = atoi(argv[++i]);
            if (customWidth <= 0) {
                printf("Error: Invalid width value\n");
                givehelp = 1;
            }
        } else if ((!strcmp(argv[i], "-h") || !strcmp(argv[i], "--height")) && i + 1 < argc) {
            customHeight = atoi(argv[++i]);
            if (customHeight <= 0) {
                printf("Error: Invalid height value\n");
                givehelp = 1;
            }
        } else if ((!strcmp(argv[i], "-s") || !strcmp(argv[i], "--scale")) && i + 1 < argc) {
            customScale = static_cast<float>(atof(argv[++i]));
            if (customScale <= 0.0f) {
                printf("Error: Invalid scale value\n");
                givehelp = 1;
            }
        } else
            givehelp = 1;
    }
    if (givehelp) {
        printf("Unrecognized parameter.\nOptions are:\n");
        printf("\t-f|--fullscreen\t\tUse fullscreen\n");
        printf("\t-d|--desktop\t\tUse desktop fullscreen\n");
        printf("\t-n|--nomsaa\t\tDisable MSAA\n");
        printf("\t-w|--width <pixels>\tSet window width (e.g., 640, 800, 1280)\n");
        printf("\t-h|--height <pixels>\tSet window height (e.g., 480, 600, 720)\n");
        printf("\t-s|--scale <factor>\tSet scale factor (e.g., 1.0, 1.5, 2.0)\n");
        exit(0);
    }

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

#if defined(PANDORA)
    int revision = 5;
    FILE* f = fopen("/etc/powervr-esrev", "r");
    if (f) {
        fscanf(f, "%d", &revision);
        fclose(f);
        printf("Pandora Model detected = %d\n", revision);
    }
    if (revision == 5 && !nomsaa) {
        // only do MSAA for Gigahertz model
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);
        GL_MSAA = 1;
    }
#else
    if (!nomsaa) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
        GL_MSAA = 1;
    }
#endif
    int flags = 0;
    wideScreen = 0;
    int screenH, screenW, screenX, screenY;
#ifdef USE_SDL2
    flags = SDL_WINDOW_OPENGL;
#else
    flags = SDL_OPENGL | SDL_DOUBLEBUF;
#endif
    if (fullscreen)
#ifdef USE_SDL2
        flags |= SDL_WINDOW_FULLSCREEN;
#else
        flags |= SDL_FULLSCREEN;
#endif
#ifdef PANDORA
#ifdef USE_SDL2
    flags |= SDL_WINDOW_FULLSCREEN;
#else
    flags |= SDL_FULLSCREEN;
#endif
    screenW = 800;
    screenH = 480;
#elif defined(CHIP)
#ifdef USE_SDL2
    flags |= SDL_WINDOW_FULLSCREEN;
#else
    flags |= SDL_FULLSCREEN;
#endif
    screenW = 480;
    screenH = 272;
#else
        // Use custom dimensions if provided
        if (customWidth > 0 && customHeight > 0) {
            screenW = customWidth;
            screenH = customHeight;
        } else if (desktop || fullscreen) {
#ifdef USE_SDL2
            flags |= (desktop) ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
#else
            flags |= SDL_FULLSCREEN;
#endif
            if (desktop) {
#ifdef USE_SDL2
                screenW = customWidth > 0 ? customWidth : 640;
                screenH = customHeight > 0 ? customHeight : 480;
#else
                const SDL_VideoInfo* infos = SDL_GetVideoInfo();
                screenW = customWidth > 0 ? customWidth : infos->current_w;
                screenH = customHeight > 0 ? customHeight : infos->current_h;
#endif
            } else {
                screenW = customWidth > 0 ? customWidth : 640;
                screenH = customHeight > 0 ? customHeight : 480;
            }
        } else {
            screenW = customWidth > 0 ? customWidth : 800;
            screenH = customHeight > 0 ? customHeight : 480;
        }
#endif
#ifdef USE_SDL2
    window = SDL_CreateWindow(maintitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screenW, screenH, flags);
    if (window == NULL && GL_MSAA) {
        // fallback to no MSAA
        GL_MSAA = 0;
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow(maintitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screenW, screenH, flags);
    }
    if (window == NULL) {
        printf("Couldn't create Window (%dx%d): %s\n", screenW, screenH, SDL_GetError());
        exit(-2);
    }
    context = SDL_GL_CreateContext(window);
    if (context == NULL) {
        printf("Couldn't create OpenGL Context: %s\n", SDL_GetError());
        exit(-3);
    }
    SDL_GetWindowSize(window, &screenW, &screenH);
    SDL_SetWindowTitle(window, maintitle);
#endif
    {
        // icon...
        int x, y, n;
        unsigned char* img = stbi_load("data/Bitmap/icon.png", &x, &y, &n, STBI_rgb_alpha);
        if (img) {
            SDL_Surface* icon =
#ifdef USE_SDL2
                SDL_CreateRGBSurfaceWithFormatFrom(img, x, y, 32, x * 4, SDL_PIXELFORMAT_RGBA32);
#else
                SDL_CreateRGBSurfaceFrom(img, x, y, 32, x * 4, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
#endif
            if (icon) {
#ifdef USE_SDL2
                SDL_SetWindowIcon(window, icon);
                SDL_FreeSurface(icon);
#else
                SDL_WM_SetIcon(icon, NULL);
#endif
            }
            free(img);
        }
    }
#ifndef USE_SDL2
    screen = SDL_SetVideoMode(screenW, screenH, 32, flags);
    if (screen == NULL) {
        // fallback to no MSAA
        GL_MSAA = 0;
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        screen = SDL_SetVideoMode(screenW, screenH, 32, flags);
        if (screen == NULL) {
#ifdef PANDORA
            printf("Couldn't set 800x480x16 video mode: %s\n", SDL_GetError());
#else
            printf("Couldn't set %dx%dx32 video mode: %s\n", screenW, screenH, SDL_GetError());
#endif
            exit(-2);
        }
    } else {
        glEnable(GL_MULTISAMPLE);
    }
    SDL_WM_SetCaption(maintitle, NULL);
#endif
    // automatic guess the scale or use custom scale
    float screenScale = 1.;
    if (customScale > 0.0f) {
        // Use custom scale factor
        screenScale = customScale;
    } else {
        // Automatic scaling based on window size
        if (screenW / 640. < screenH / 480.)
            screenScale = screenW / 640.;
        else
            screenScale = screenH / 480.;
    }
    // is it a Wide screen ratio?
    // Detect widescreen if width is significantly wider than 4:3 aspect ratio
    if ((screenW / screenScale - 640) >= 80)
        wideScreen = 1;
    screenX = (screenW - (wideScreen ? 800. : 640.) * screenScale) / 2.;
    screenY = (screenH - 480. * screenScale) / 2.;
    screenW = (wideScreen ? 800 : 640) * screenScale;
    screenH = 480 * screenScale;
    printf("Display mode: %s, Scale: %.2f, Resolution: %dx%d\n", wideScreen ? "Widescreen" : "Standard", screenScale,
           screenW, screenH);
#ifdef USE_SDL2
    if (flags & SDL_WINDOW_FULLSCREEN || flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
#else
    if (flags & SDL_FULLSCREEN)
#endif
        SDL_ShowCursor(SDL_DISABLE);
    glViewport(screenX, screenY, screenW, screenH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    screenH = 480;
    screenW = wideScreen ? 800 : 640;
    glOrtho(0, screenW, screenH, 0, 0, FURTHEST_Z);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    D3DXMATRIX matProj;
    FLOAT fAspect = screenW / 480.0f;
    D3DXMatrixPerspectiveFovLH(&matProj, D3DX_PI / 4, fAspect, 0.5f, FURTHEST_Z);
    pd3dDevice.SetTransform(D3DTS_PROJECTION, &matProj);

    glEnable(GL_DEPTH_TEST);
    glAlphaFunc(GL_NOTEQUAL, 0);
    //    glEnable(GL_ALPHA_TEST);
    //    glShadeModel(GL_FLAT);
    glDisable(GL_LIGHTING);
    // Disable texture mapping by default (only DrawTrack() enables it)
    pd3dDevice.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);

    sound_init();

    CreateFonts();
    LoadTextures();

    if (!InitialiseData()) {
        printf("Error initialising data\n");
        exit(-3);
    }

    CreateBuffers(&pd3dDevice);

    DSInit();
    DSSetMode();

    glClearColor(0, 0, 0, 1);
#ifdef __EMSCRIPTEN__
    CapturePreviousCarState();
    g_lastFrameTime = GetTimeSeconds();
    g_logicAccumulator = LOGIC_STEP_SECONDS;
    g_timingWindowStart = g_lastFrameTime;
    emscripten_set_main_loop(em_main_loop, 0, 1);
#else
    bool run = true;
    CapturePreviousCarState();
    g_lastFrameTime = GetTimeSeconds();
    g_logicAccumulator = LOGIC_STEP_SECONDS;
    g_timingWindowStart = g_lastFrameTime;
    while (run) {
        run = RunFrame(GetTimeSeconds(), true);
    }
#endif
    FreeData();

    CloseFonts();

    sound_destroy();
    TTF_Quit();
    SDL_Quit();

    exit(0);
}
