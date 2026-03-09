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
static const double BASE_LOGIC_STEP_SECONDS = 0.14; // original coarse game step (~1/7.14s)
static const int PHYSICS_SUBSTEPS_PER_BASE_LOGIC = 7;
static const double PHYSICS_STEP_SECONDS = BASE_LOGIC_STEP_SECONDS / PHYSICS_SUBSTEPS_PER_BASE_LOGIC;
/* Cap physics steps per frame to avoid catch-up stutter and spiral of death when the game can't keep up. */
static const int MAX_PHYSICS_STEPS_PER_FRAME = 10;

GameModeType GameMode = TRACK_MENU;

// Both the following are used for keyboard input
UINT keyPress = '\0';
DWORD lastInput = 0;
static DWORD g_keyboardInput = 0;

static IDirectSound8* ds;
IDirectSoundBuffer8* WreckSoundBuffer = NULL;
IDirectSoundBuffer8* HitCarSoundBuffer = NULL;
IDirectSoundBuffer8* GroundedSoundBuffer = NULL;
IDirectSoundBuffer8* CreakSoundBuffer = NULL;
IDirectSoundBuffer8* SmashSoundBuffer = NULL;
IDirectSoundBuffer8* OffRoadSoundBuffer = NULL;
IDirectSoundBuffer8* EngineSoundBuffers[8] = {NULL};

GpuTexture* g_pAtlas = NULL;

int wideScreen = 0;

static bool bFrameMoved = FALSE;
static double g_logicAccumulator = 0.0;
static double g_lastFrameTime = 0.0;
static double g_timingWindowStart = 0.0;
static uint64_t g_renderFramesInWindow = 0;
static uint64_t g_physicsTicksInWindow = 0;
static uint64_t g_physicsTickTotal = 0;
static uint64_t g_baseLogicTicksInWindow = 0;
static uint64_t g_baseLogicTickTotal = 0;
static double g_renderFpsDisplay = 0.0;
static double g_physicsTickRateDisplay = 0.0;
static double g_baseLogicTickRateDisplay = 0.0;
static int g_baseLogicSubstepCounter = 0;
static DWORD g_logicInput = 0;
static int g_controlSampleCount = 0;
static int g_leftSampleCount = 0;
static int g_rightSampleCount = 0;
static int g_accelSampleCount = 0;
static int g_brakeSampleCount = 0;
static int g_boostSampleCount = 0;
static bool g_restartEngineAudioOnFirstInput = false;
static float g_requestedScreenScale = 0.0f;

#ifdef USE_SDL2
#define MAX_LOCAL_PLAYERS 2
#define GAMEPAD_STEER_DEADZONE 12000
#define GAMEPAD_TRIGGER_THRESHOLD 16000

typedef struct {
    SDL_GameController* handle;
    SDL_JoystickID instanceId;
} GAMEPAD_SLOT;

static GAMEPAD_SLOT g_gamepadSlots[MAX_LOCAL_PLAYERS];
static DWORD g_gamepadInput[MAX_LOCAL_PLAYERS] = {0, 0};
#endif

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
static long prev_viewpoint1_x = 0, prev_viewpoint1_y = 0, prev_viewpoint1_z = 0;
static long prev_viewpoint1_x_angle = 0, prev_viewpoint1_y_angle = 0, prev_viewpoint1_z_angle = 0;
static long prev_target_x = 0, prev_target_y = 0, prev_target_z = 0;
static bool have_prev_car_state = false;

// Viewpoint 1 orientation
static long viewpoint1_x, viewpoint1_y, viewpoint1_z;
static long viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle;

// Target (lookat) point
static long target_x, target_y, target_z;

// Backdrop viewpoint used by OnFrameRender (interpolated for smooth horizon/scenery motion).
static long render_backdrop_viewpoint_y = 0;
static long render_backdrop_viewpoint_x_angle = 0;
static long render_backdrop_viewpoint_y_angle = 0;
static long render_backdrop_viewpoint_z_angle = 0;

static void ResetControlSamplingWindow(void);
static void ApplyWindowLayout(int windowWidth, int windowHeight, bool logLayout);
static void RefreshCombinedInput(void);

#ifdef USE_SDL2
static void ResetGamepadSlots(void);
static void OpenInitialGamepads(void);
static void HandleGamepadDeviceAdded(int deviceIndex);
static void HandleGamepadDeviceRemoved(SDL_JoystickID instanceId);
static DWORD BuildGamepadInputForPlayer(SDL_GameController* controller);
static void RefreshGamepadInput(void);
static void CloseAllGamepads(void);
#endif

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
        // Keep engine centered for balanced stereo output on modern devices.
        EngineSoundBuffers[i]->SetPan(DSBPAN_CENTER);
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
    const SurfaceDesc* desc;
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
        COLOR_RGB(SCPalette[colour_index].peRed, SCPalette[colour_index].peGreen, SCPalette[colour_index].peBlue));
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
            reducedSCPalette[i] = COLOR_RGB((5*SCPalette[i].peRed)/8,
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
        g_pAtlas = new GpuTexture();
    g_pAtlas->LoadTexture("data/Bitmap/atlas.png");
    InitAtlasCoord();
    printf("Texture loaded\n");
}
void CreateBuffers(RenderDevice* pDevice) {
    if (CreatePolygonVertexBuffer(pDevice) != S_OK)
        printf("Error creating PolygonVertexBuffer\n");
    if (CreateTrackVertexBuffer(pDevice) != S_OK)
        printf("Error creating TrackVertexBuffer\n");
    if (CreateGroundPlaneVertexBuffer(pDevice) != S_OK)
        printf("Error creating GroundPlaneVertexBuffer\n");
    if (CreateShadowVertexBuffer(pDevice) != S_OK)
        printf("Error creating ShadowVertexBuffer\n");
    if (CreateCarVertexBuffer(pDevice) != S_OK)
        printf("Error creating CarVertexBuffer\n");
    if (CreateCockpitVertexBuffer(pDevice) != S_OK)
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
static glm::mat4 matWorldTrack, matWorldCar, matWorldOpponentsCar;

static float FixedPointToWorldCoord(long value) {
    return static_cast<float>(value) / static_cast<float>(1 << LOG_PRECISION);
}

static float PlayerAngleToRadians(long angle) {
    return (static_cast<float>(angle) * 2.0f * PI) / 65536.0f;
}

static long WrappedAngleDelta(long from, long to) {
    long delta = (to - from) & (MAX_ANGLE - 1);
    if (delta > (MAX_ANGLE / 2))
        delta -= MAX_ANGLE;
    return delta;
}

static float LerpWrappedAngleUnits(long from, long to, float alpha) {
    return static_cast<float>(from) + static_cast<float>(WrappedAngleDelta(from, to)) * alpha;
}

static float LerpWrappedPlayerAngle(long from, long to, float alpha) {
    const float interpolated = LerpWrappedAngleUnits(from, to, alpha);
    return (interpolated * 2.0f * PI) / 65536.0f;
}

static float NormalizeRadians(float angle) {
    while (angle > PI)
        angle -= 2.0f * PI;
    while (angle < -PI)
        angle += 2.0f * PI;
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

static float LerpLong(long from, long to, float alpha) {
    return static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * alpha;
}

static void BuildCarWorldTransform(glm::mat4* out, float x, float y, float z, float xa, float ya, float za,
                                   float yOffset) {
    glm::mat4 matRot, matTemp, matTrans;

    mat4Identity(&matRot);
    mat4RotationZ(&matTemp, za);
    mat4Multiply(&matRot, &matRot, &matTemp);
    mat4RotationX(&matTemp, xa);
    mat4Multiply(&matRot, &matRot, &matTemp);
    mat4RotationY(&matTemp, ya);
    mat4Multiply(&matRot, &matRot, &matTemp);

    mat4Translation(&matTrans, x, -y + yOffset, z);
    mat4Multiply(out, &matRot, &matTrans);
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
    prev_viewpoint1_x = viewpoint1_x;
    prev_viewpoint1_y = viewpoint1_y;
    prev_viewpoint1_z = viewpoint1_z;
    prev_viewpoint1_x_angle = viewpoint1_x_angle;
    prev_viewpoint1_y_angle = viewpoint1_y_angle;
    prev_viewpoint1_z_angle = viewpoint1_z_angle;
    prev_target_x = target_x;
    prev_target_y = target_y;
    prev_target_z = target_z;
    CapturePreviousOpponentShadow();

    have_prev_car_state = true;
}

static void UpdateInterpolatedCarTransforms(RenderDevice* pDevice, float alpha) {
    if (!have_prev_car_state)
        CapturePreviousCarState();

    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    const float backdropY = LerpLong(prev_viewpoint1_y, viewpoint1_y, alpha);
    const float backdropXa = LerpWrappedAngleUnits(prev_viewpoint1_x_angle, viewpoint1_x_angle, alpha);
    const float backdropYa = LerpWrappedAngleUnits(prev_viewpoint1_y_angle, viewpoint1_y_angle, alpha);
    const float backdropZa = LerpWrappedAngleUnits(prev_viewpoint1_z_angle, viewpoint1_z_angle, alpha);
    render_backdrop_viewpoint_y = static_cast<long>(backdropY);
    render_backdrop_viewpoint_x_angle = static_cast<long>(backdropXa) & (MAX_ANGLE - 1);
    render_backdrop_viewpoint_y_angle = static_cast<long>(backdropYa) & (MAX_ANGLE - 1);
    render_backdrop_viewpoint_z_angle = static_cast<long>(backdropZa) & (MAX_ANGLE - 1);

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
    const float opponentXa = LerpWrappedRadians(prev_opponent_x_angle, opponent_x_angle, alpha);
    const float opponentYa = LerpWrappedRadians(prev_opponent_y_angle, opponent_y_angle, alpha);
    const float opponentZa = LerpWrappedRadians(prev_opponent_z_angle, opponent_z_angle, alpha);
    BuildCarWorldTransform(&matWorldOpponentsCar, opponentX, opponentY, opponentZ, opponentXa, opponentYa, opponentZa,
                           VCAR_HEIGHT / 4.0f);

    glm::mat4 matRot, matTemp, matTrans, matView;
    static glm::vec3 vUpVec(0.0f, 1.0f, 0.0f);
    if (GameMode == GAME_IN_PROGRESS) {
        const float viewX = LerpLong(prev_viewpoint1_x, viewpoint1_x, alpha);
        const float viewY = LerpLong(prev_viewpoint1_y, viewpoint1_y, alpha) / static_cast<float>(1 << LOG_PRECISION);
        const float viewZ = LerpLong(prev_viewpoint1_z, viewpoint1_z, alpha);

        const float xaUnits = LerpWrappedAngleUnits(prev_viewpoint1_x_angle, viewpoint1_x_angle, alpha);
        const float yaUnits = LerpWrappedAngleUnits(prev_viewpoint1_y_angle, viewpoint1_y_angle, alpha);
        const float zaUnits = LerpWrappedAngleUnits(prev_viewpoint1_z_angle, viewpoint1_z_angle, alpha);
        const float xa = ((-xaUnits) * 2.0f * PI) / 65536.0f;
        const float ya = ((-yaUnits) * 2.0f * PI) / 65536.0f;
        const float za = ((-zaUnits) * 2.0f * PI) / 65536.0f;

        mat4Translation(&matTrans, -viewX, viewY, -viewZ);
        mat4Identity(&matRot);
#ifdef linux
        mat4RotationY(&matTemp, ya + PI);
        mat4Multiply(&matRot, &matRot, &matTemp);
        mat4RotationX(&matTemp, -xa);
        mat4Multiply(&matRot, &matRot, &matTemp);
        mat4RotationZ(&matTemp, -za);
        mat4Multiply(&matRot, &matRot, &matTemp);
#else
        mat4RotationY(&matTemp, ya);
        mat4Multiply(&matRot, &matRot, &matTemp);
        mat4RotationX(&matTemp, xa);
        mat4Multiply(&matRot, &matRot, &matTemp);
        mat4RotationZ(&matTemp, za);
        mat4Multiply(&matRot, &matRot, &matTemp);
#endif
        mat4Multiply(&matView, &matTrans, &matRot);
#ifdef linux
        mat4Scaling(&matTrans, +1, -1, +1);
        mat4Multiply(&matView, &matView, &matTrans);
#endif
        pDevice->SetTransform(TS_VIEW, &matView);
    } else if ((GameMode == TRACK_PREVIEW) || (GameMode == TRACK_MENU)) {
        const float viewX = LerpLong(prev_viewpoint1_x, viewpoint1_x, alpha);
        const float viewY = -LerpLong(prev_viewpoint1_y, viewpoint1_y, alpha) / static_cast<float>(1 << LOG_PRECISION);
        const float viewZ = LerpLong(prev_viewpoint1_z, viewpoint1_z, alpha);
        const float lookX = LerpLong(prev_target_x, target_x, alpha);
        const float lookY = LerpLong(prev_target_y, target_y, alpha);
        const float lookZ = LerpLong(prev_target_z, target_z, alpha);
        glm::vec3 vEyePt(viewX, viewY, viewZ);
        glm::vec3 vLookatPt(lookX, lookY, lookZ);
        mat4LookAt(&matView, &vEyePt, &vLookatPt, &vUpVec);
        pDevice->SetTransform(TS_VIEW, &matView);
    }
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

static void RestartEngineAudioBuffers(bool resetEngineModel) {
    for (int i = 0; i < 8; ++i) {
        EngineSoundBuffers[i]->Stop();
        EngineSoundBuffers[i]->SetCurrentPosition(0);
    }
    engineSoundPlaying = FALSE;
    if (resetEngineModel)
        ResetEngineAudioState();
}

static void StopEngineSound(void) {
    if (engineSoundPlaying) {
        RestartEngineAudioBuffers(true);
    }
}

void CALLBACK OnFrameMove(RenderDevice* pDevice, double fTime, float fElapsedTime, void* pUserContext) {
    static glm::vec3 vUpVec(0.0f, 1.0f, 0.0f);
    DWORD input = (GameMode == GAME_IN_PROGRESS) ? g_logicInput : lastInput;
    glm::mat4 matRot, matTemp, matTrans, matView;
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
        // Engine/audio is stepped from the 50Hz substep loop in RunFrame().
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

    if ((GameMode == GAME_IN_PROGRESS) && (!bPaused)) {
        UpdateLapData();
        AdvanceFourteenFrameTiming();
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
        mat4Identity(&matWorldTrack);

        //
        // Set the view transform matrix
        //
        // Set the eye point
        glm::vec3 vEyePt(static_cast<float>(viewpoint1_x), static_cast<float>(-viewpoint1_y >> LOG_PRECISION),
                           static_cast<float>(viewpoint1_z));
        // Set the lookat point
        glm::vec3 vLookatPt(static_cast<float>(target_x), static_cast<float>(target_y), static_cast<float>(target_z));
        mat4LookAt(&matView, &vEyePt, &vLookatPt, &vUpVec);
        pDevice->SetTransform(TS_VIEW, &matView);
    } else if (GameMode == GAME_IN_PROGRESS) {
        CalcGameViewpoint();

        // Set Direct3D transforms, ready for OnFrameRender
        viewpoint1_x >>= LOG_PRECISION;
        // NOTE: viewpoint1_y must be preserved for use by DrawBackdrop
        viewpoint1_z >>= LOG_PRECISION;

        // Set the track's world transform matrix
        mat4Identity(&matWorldTrack);

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
        mat4Translation(&matTrans, static_cast<float>(-viewpoint1_x),
                              static_cast<float>(viewpoint1_y >> LOG_PRECISION), static_cast<float>(-viewpoint1_z));
        mat4Identity(&matRot);
        float xa = ((static_cast<float>(-viewpoint1_x_angle) * 2 * PI) / 65536.0f);
        float ya = ((static_cast<float>(-viewpoint1_y_angle) * 2 * PI) / 65536.0f);
        float za = ((static_cast<float>(-viewpoint1_z_angle) * 2 * PI) / 65536.0f);
        // Produce and combine the rotation matrices
#ifdef linux
        mat4RotationY(&matTemp, ya + PI);
        mat4Multiply(&matRot, &matRot, &matTemp);
        mat4RotationX(&matTemp, -xa);
        mat4Multiply(&matRot, &matRot, &matTemp);
        mat4RotationZ(&matTemp, -za);
        mat4Multiply(&matRot, &matRot, &matTemp);
#else
        mat4RotationY(&matTemp, ya);
        mat4Multiply(&matRot, &matRot, &matTemp);
        mat4RotationX(&matTemp, xa);
        mat4Multiply(&matRot, &matRot, &matTemp);
        mat4RotationZ(&matTemp, za);
        mat4Multiply(&matRot, &matRot, &matTemp);
#endif
        // Combine the rotation and translation matrices to complete the world matrix
        mat4Multiply(&matView, &matTrans, &matRot);
#ifdef linux
        mat4Scaling(&matTrans, +1, -1, +1);
        mat4Multiply(&matView, &matView, &matTrans);
#endif
        pDevice->SetTransform(TS_VIEW, &matView);
    }

    render_backdrop_viewpoint_y = viewpoint1_y;
    render_backdrop_viewpoint_x_angle = viewpoint1_x_angle;
    render_backdrop_viewpoint_y_angle = viewpoint1_y_angle;
    render_backdrop_viewpoint_z_angle = viewpoint1_z_angle;

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
    const SurfaceDesc* pd3dsdBackBuffer = GetBackBufferSurfaceDesc();
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

        if (CreateGroundPlaneVertexBuffer(GetRenderDevice()) != S_OK) {
#if defined(DEBUG) || defined(_DEBUG)
            fprintf(out, "Failed to create ground plane vertex buffer %d\n", track_number);
#endif
            MessageBox(NULL, L"Failed to create ground plane vertex buffer", L"Error", MB_OK);
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
    const SurfaceDesc* pd3dsdBackBuffer = GetBackBufferSurfaceDesc();
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
    txtHelper.DrawTextLine(L"Gamepad controls :-");
    txtHelper.DrawTextLine(L"  Left stick/D-Pad = Steer, RT = Accelerate, LT or B = Brake, A/X/RB = Boost");
    txtHelper.DrawTextLine(L"  R = Point car in opposite direction, P = Pause, O = Unpause");
    txtHelper.DrawTextLine(L"  M = Back to track menu, Escape = Quit");

    if (keyPress == STARTMENU) {
        RestartEngineAudioBuffers(true);
        bNewGame = TRUE;
        GameMode = GAME_IN_PROGRESS;
        g_restartEngineAudioOnFirstInput = true;
        // Reduce start-of-race input/audio latency: make next substep trigger
        // a full logic tick immediately.
        g_baseLogicSubstepCounter = PHYSICS_SUBSTEPS_PER_BASE_LOGIC - 1;
        g_logicAccumulator = PHYSICS_STEP_SECONDS;
        g_logicInput = lastInput;
        ResetControlSamplingWindow();
        // initialise game data
        ResetFourteenFrameTiming();
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
    txtHelper.SetForegroundColor(glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
    if (bShowStats) {
        txtHelper.SetInsertionPos(static_cast<int>((2 + (wideScreen ? 10 : 0)) * textScale), 0);
        txtHelper.DrawFormattedTextLine(L"fTime: %0.1f  sin(fTime): %0.4f", fTime, sin(fTime));
        txtHelper.DrawFormattedTextLine(L"Render FPS: %5.1f  Physics: %5.1f Hz  Logic: %4.2f Hz", g_renderFpsDisplay,
                                        g_physicsTickRateDisplay, g_baseLogicTickRateDisplay);
        txtHelper.DrawFormattedTextLine(L"Ticks  Physics: %.0f  Logic: %.0f", static_cast<double>(g_physicsTickTotal),
                                        static_cast<double>(g_baseLogicTickTotal));

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
        const SurfaceDesc* pd3dsdBackBuffer = GetBackBufferSurfaceDesc();
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
        txtHelper.SetForegroundColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

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
                    txtHelperLarge.SetForegroundColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
                else
                    txtHelperLarge.SetForegroundColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

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

//--------------------------------------------------------------------------------------
// Render the scene
//--------------------------------------------------------------------------------------

void CALLBACK OnFrameRender(RenderDevice* pDevice, double fTime, float fElapsedTime, void* pUserContext) {
    HRESULT hr;

    //    // Clear the render target and the zbuffer
    //    V( pDevice->Clear(0, NULL, CLEAR_TARGET | CLEAR_ZBUFFER, COLOR_RGB(0, 45, 50, 170), 1.0f, 0) );

    // Clear the zbuffer
    V(pDevice->Clear(0, NULL, CLEAR_ZBUFFER, 0, 1.0f, 0));

    // Render the scene
    if (SUCCEEDED(pDevice->BeginScene())) {
        // Disable Z buffer and polygon culling, ready for DrawBackdrop()
        pDevice->SetRenderState(RS_ZENABLE, FALSE);
        pDevice->SetRenderState(RS_CULLMODE, CULL_NONE);

        // Draw Backdrop
        DrawBackdrop(render_backdrop_viewpoint_y, render_backdrop_viewpoint_x_angle, render_backdrop_viewpoint_y_angle,
                     render_backdrop_viewpoint_z_angle);

        //        SetupLights(pDevice);

        // Draw Track
        pDevice->SetTransform(TS_WORLD, &matWorldTrack);
        DrawGroundPlane(pDevice);
        DrawTrack(pDevice);

        switch (GameMode) {
        case TRACK_MENU:
            break;

        case TRACK_PREVIEW:
            // Draw Opponent's Car
            pDevice->SetTransform(TS_WORLD, &matWorldOpponentsCar);
            DrawCar(pDevice);
            break;

        case GAME_IN_PROGRESS:
        case GAME_OVER:
            // Draw Opponent's Car
            pDevice->SetTransform(TS_WORLD, &matWorldOpponentsCar);
            DrawCar(pDevice);

            if (bOutsideView) {
                // Draw Player1's Car
                pDevice->SetTransform(TS_WORLD, &matWorldCar);
                DrawCar(pDevice);
            } else {
                // draw cockpit...
                DrawCockpit(pDevice);
            }
            break;
        }

        if (GameMode == GAME_IN_PROGRESS) {
            DrawOtherGraphics();

            //jsr    display.speed.bar
            if (bFrameMoved)
                UpdateDamage();
            //jsr    display.opponents.distance
        }

        RenderText(fTime);

        // End the scene
        pDevice->EndScene();
    }
}

#ifdef USE_SDL2
static void ResetGamepadSlots(void) {
    for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
        g_gamepadSlots[i].handle = NULL;
        g_gamepadSlots[i].instanceId = -1;
        g_gamepadInput[i] = 0;
    }
}

static int FindGamepadSlotByInstance(SDL_JoystickID instanceId) {
    for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
        if (g_gamepadSlots[i].handle && (g_gamepadSlots[i].instanceId == instanceId))
            return i;
    }
    return -1;
}

static int FindFreeGamepadSlot(void) {
    for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
        if (g_gamepadSlots[i].handle == NULL)
            return i;
    }
    return -1;
}

static DWORD BuildGamepadInputForPlayer(SDL_GameController* controller) {
    DWORD input = 0;
    if (controller == NULL)
        return input;

    const Sint16 steerX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    const Sint16 leftTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    const Sint16 rightTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

    if ((steerX <= -GAMEPAD_STEER_DEADZONE) ||
        (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0))
        input |= KEY_P1_LEFT;

    if ((steerX >= GAMEPAD_STEER_DEADZONE) ||
        (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0))
        input |= KEY_P1_RIGHT;

    if ((rightTrigger >= GAMEPAD_TRIGGER_THRESHOLD) ||
        (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0))
        input |= KEY_P1_ACCEL;

    if ((leftTrigger >= GAMEPAD_TRIGGER_THRESHOLD) ||
        (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B) != 0) ||
        (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0))
        input |= KEY_P1_BRAKE;

    if ((SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) != 0) ||
        (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X) != 0) ||
        (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0))
        input |= KEY_P1_BOOST;

    return input;
}

static void RefreshGamepadInput(void) {
    for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
        g_gamepadInput[i] = 0;
        if (g_gamepadSlots[i].handle == NULL)
            continue;
        if (!SDL_GameControllerGetAttached(g_gamepadSlots[i].handle))
            continue;
        g_gamepadInput[i] = BuildGamepadInputForPlayer(g_gamepadSlots[i].handle);
    }
}

static void RefreshCombinedInput(void) {
    RefreshGamepadInput();
    // Current gameplay consumes player 1 input; keep player-indexed slots for future 2P.
    lastInput = g_keyboardInput | g_gamepadInput[0];
}

static void HandleGamepadDeviceAdded(int deviceIndex) {
    if (!SDL_IsGameController(deviceIndex))
        return;

    const SDL_JoystickID instanceId = SDL_JoystickGetDeviceInstanceID(deviceIndex);
    if (instanceId < 0)
        return;

    if (FindGamepadSlotByInstance(instanceId) >= 0)
        return;

    const int slot = FindFreeGamepadSlot();
    if (slot < 0)
        return;

    SDL_GameController* controller = SDL_GameControllerOpen(deviceIndex);
    if (controller == NULL) {
        printf("SDL gamepad open failed for device %d: %s\n", deviceIndex, SDL_GetError());
        return;
    }

    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
    g_gamepadSlots[slot].handle = controller;
    g_gamepadSlots[slot].instanceId = SDL_JoystickInstanceID(joystick);

    const char* name = SDL_GameControllerName(controller);
    if (name == NULL)
        name = "Unknown Controller";
    printf("Gamepad P%d connected: %s\n", slot + 1, name);
}

static void HandleGamepadDeviceRemoved(SDL_JoystickID instanceId) {
    const int slot = FindGamepadSlotByInstance(instanceId);
    if (slot < 0)
        return;

    SDL_GameControllerClose(g_gamepadSlots[slot].handle);
    g_gamepadSlots[slot].handle = NULL;
    g_gamepadSlots[slot].instanceId = -1;
    g_gamepadInput[slot] = 0;
    printf("Gamepad P%d disconnected\n", slot + 1);
}

static void OpenInitialGamepads(void) {
    const int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; ++i)
        HandleGamepadDeviceAdded(i);
}

static void CloseAllGamepads(void) {
    for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
        if (g_gamepadSlots[i].handle) {
            SDL_GameControllerClose(g_gamepadSlots[i].handle);
            g_gamepadSlots[i].handle = NULL;
        }
        g_gamepadSlots[i].instanceId = -1;
        g_gamepadInput[i] = 0;
    }
}
#else
static void RefreshCombinedInput(void) { lastInput = g_keyboardInput; }
#endif

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
                    g_restartEngineAudioOnFirstInput = false;

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
                g_keyboardInput |= KEY_P1_LEFT;
                break;

            case SDLK_RIGHT:
                g_keyboardInput |= KEY_P1_RIGHT;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_RCTRL:
#else
            case SDLK_SPACE:
            case SDLK_RSHIFT:
            case SDLK_LSHIFT:
#endif
                g_keyboardInput |= KEY_P1_BOOST;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_END:
#else
            case SDLK_DOWN:
#endif
                g_keyboardInput |= KEY_P1_BRAKE;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_PAGEDOWN:
#else
            case SDLK_UP:
#endif
                g_keyboardInput |= KEY_P1_ACCEL;
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
                g_keyboardInput &= ~KEY_P1_LEFT;
                break;

            case SDLK_RIGHT:
                g_keyboardInput &= ~KEY_P1_RIGHT;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_RCTRL:
#else
            case SDLK_SPACE:
            case SDLK_RSHIFT:
            case SDLK_LSHIFT:
#endif
                g_keyboardInput &= ~KEY_P1_BOOST;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_END:
#else
            case SDLK_DOWN:
#endif
                g_keyboardInput &= ~KEY_P1_BRAKE;
                break;

#if defined(PANDORA) || defined(PYRA)
            case SDLK_PAGEDOWN:
#else
            case SDLK_UP:
#endif
                g_keyboardInput &= ~KEY_P1_ACCEL;
                break;
            }
            break;
        case SDL_QUIT:
            return false;
#ifdef USE_SDL2
        case SDL_CONTROLLERDEVICEADDED:
            HandleGamepadDeviceAdded(event.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            HandleGamepadDeviceRemoved(event.cdevice.which);
            break;
#endif
#ifdef USE_SDL2
        case SDL_WINDOWEVENT:
            if ((event.window.event == SDL_WINDOWEVENT_RESIZED) || (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
                ApplyWindowLayout(event.window.data1, event.window.data2, false);
            break;
#endif
        }
    }
    RefreshCombinedInput();
    return true;
}

RenderDevice pDevice;
#ifdef USE_SDL2
SDL_Window* window = NULL;
#endif

static void ApplyWindowLayout(int windowWidth, int windowHeight, bool logLayout) {
    if ((windowWidth <= 0) || (windowHeight <= 0))
        return;

    float screenScale = g_requestedScreenScale;
    if (screenScale <= 0.0f) {
        float scaleX = static_cast<float>(windowWidth) / 640.0f;
        float scaleY = static_cast<float>(windowHeight) / 480.0f;
        screenScale = (scaleX < scaleY) ? scaleX : scaleY;
    }
    if (screenScale <= 0.0f)
        screenScale = 1.0f;

    wideScreen = (((static_cast<float>(windowWidth) / screenScale) - 640.0f) >= 80.0f) ? 1 : 0;

    const float virtualWidth = wideScreen ? 800.0f : 640.0f;
    const float virtualHeight = 480.0f;
    
    /* Use the whole rendering area instead of letterboxing */
    int viewportW = windowWidth;
    int viewportH = windowHeight;
    int viewportX = 0;
    int viewportY = 0;

    glViewport(viewportX, viewportY, viewportW, viewportH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    
    /* Adjust orthographic projection to match the new aspect ratio */
    float projWidth = virtualHeight * (static_cast<float>(windowWidth) / static_cast<float>(windowHeight));
    glOrtho(0, projWidth, virtualHeight, 0, 0, FURTHEST_Z);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glm::mat4 matProj;
    FLOAT fAspect = projWidth / virtualHeight;
    mat4PerspectiveFov(&matProj, PI / 4, fAspect, 0.5f, FURTHEST_Z);
    pDevice.SetTransform(TS_PROJECTION, &matProj);

    if (logLayout) {
        printf("Display mode: %s, Scale: %.2f, Viewport: %dx%d @ (%d,%d)\n", wideScreen ? "Widescreen" : "Standard",
               screenScale, viewportW, viewportH, viewportX, viewportY);
    }
}

static void RenderCurrentFrame(double frameTime, float frameDelta) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    OnFrameRender(&pDevice, frameTime, frameDelta, NULL);
#ifdef USE_SDL2
    SDL_GL_SwapWindow(window);
#else
    SDL_GL_SwapBuffers();
#endif
}

static void ResetControlSamplingWindow(void) {
    g_controlSampleCount = 0;
    g_leftSampleCount = 0;
    g_rightSampleCount = 0;
    g_accelSampleCount = 0;
    g_brakeSampleCount = 0;
    g_boostSampleCount = 0;
}

static void SampleControlsForLogicSubstep(DWORD input) {
    ++g_controlSampleCount;
    if (input & KEY_P1_LEFT)
        ++g_leftSampleCount;
    if (input & KEY_P1_RIGHT)
        ++g_rightSampleCount;
    if (input & KEY_P1_ACCEL)
        ++g_accelSampleCount;
    if (input & KEY_P1_BRAKE)
        ++g_brakeSampleCount;
    if (input & KEY_P1_BOOST)
        ++g_boostSampleCount;
}

static DWORD BuildLogicInputFromSamples(DWORD latestInput) {
    const int sampleCount = (g_controlSampleCount > 0) ? g_controlSampleCount : 1;
    DWORD logicInput = 0;

    // Steer according to dominant input over the 50Hz substeps.
    if (g_leftSampleCount > g_rightSampleCount) {
        logicInput |= KEY_P1_LEFT;
    } else if (g_rightSampleCount > g_leftSampleCount) {
        logicInput |= KEY_P1_RIGHT;
    }

    // Keep brake precedence (matches existing CarControl behavior when both are active).
    if (g_brakeSampleCount > g_accelSampleCount) {
        logicInput |= KEY_P1_BRAKE;
    } else if (g_accelSampleCount > g_brakeSampleCount) {
        logicInput |= KEY_P1_ACCEL;
    } else if ((latestInput & KEY_P1_BRAKE) != 0) {
        logicInput |= KEY_P1_BRAKE;
    } else if ((latestInput & KEY_P1_ACCEL) != 0) {
        logicInput |= KEY_P1_ACCEL;
    }

    // Boost if held for at least half the substeps in this logic window.
    if ((g_boostSampleCount * 2) >= sampleCount)
        logicInput |= KEY_P1_BOOST;

    return logicInput;
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
    /* Clamp accumulator so we never run more than MAX_PHYSICS_STEPS_PER_FRAME per frame; drop excess time to avoid spiral of death. */
    const double maxAccumulator = MAX_PHYSICS_STEPS_PER_FRAME * PHYSICS_STEP_SECONDS;
    if (g_logicAccumulator > maxAccumulator)
        g_logicAccumulator = maxAccumulator;

    bool anyLogicFrameMoved = false;
    int stepsThisFrame = 0;
    while (g_logicAccumulator >= PHYSICS_STEP_SECONDS && stepsThisFrame < MAX_PHYSICS_STEPS_PER_FRAME) {
        g_logicAccumulator -= PHYSICS_STEP_SECONDS;
        ++stepsThisFrame;
        ++g_physicsTicksInWindow;
        ++g_physicsTickTotal;
        if ((GameMode == GAME_IN_PROGRESS) && (!bPaused)) {
            const DWORD drivingInputMask = KEY_P1_LEFT | KEY_P1_RIGHT | KEY_P1_ACCEL | KEY_P1_BRAKE | KEY_P1_BOOST;
            if (g_restartEngineAudioOnFirstInput) {
                // Keep engine audio fully silent until the first gameplay input,
                // but continue advancing engine state so revs are prewarmed.
                StepEngineAudioStateSubstep(PHYSICS_SUBSTEPS_PER_BASE_LOGIC);
                if ((lastInput & drivingInputMask) != 0) {
                    RestartEngineAudioBuffers(false);
                    PrimeEngineAudioForGameplayStart();
                    g_restartEngineAudioOnFirstInput = false;
                    FramesWheelsEngineSubstep(EngineSoundBuffers, PHYSICS_SUBSTEPS_PER_BASE_LOGIC);
                } else if (engineSoundPlaying) {
                    RestartEngineAudioBuffers(false);
                }
            } else {
                FramesWheelsEngineSubstep(EngineSoundBuffers, PHYSICS_SUBSTEPS_PER_BASE_LOGIC);
            }
        }
        SampleControlsForLogicSubstep(lastInput);

        ++g_baseLogicSubstepCounter;
        if (g_baseLogicSubstepCounter >= PHYSICS_SUBSTEPS_PER_BASE_LOGIC) {
            g_baseLogicSubstepCounter = 0;
            g_logicInput = BuildLogicInputFromSamples(lastInput);
            ResetControlSamplingWindow();
            CapturePreviousCarState();
            OnFrameMove(&pDevice, frameTime, static_cast<float>(BASE_LOGIC_STEP_SECONDS), NULL);
            ++g_baseLogicTicksInWindow;
            ++g_baseLogicTickTotal;
            if (bFrameMoved)
                anyLogicFrameMoved = true;
        }
    }
    bFrameMoved = anyLogicFrameMoved;

    if ((GameMode == TRACK_MENU) || (GameMode == TRACK_PREVIEW) || (GameMode == GAME_IN_PROGRESS)) {
        const double substepFraction = g_logicAccumulator / PHYSICS_STEP_SECONDS;
        const double baseProgress = (static_cast<double>(g_baseLogicSubstepCounter) + substepFraction) /
                                    static_cast<double>(PHYSICS_SUBSTEPS_PER_BASE_LOGIC);
        const float alpha = static_cast<float>(baseProgress);
        UpdateInterpolatedCarTransforms(&pDevice, alpha);
        if ((GameMode == TRACK_PREVIEW) || (GameMode == GAME_IN_PROGRESS))
            UpdateInterpolatedOpponentShadow(alpha);
    }

    RenderCurrentFrame(frameTime, static_cast<float>(frameDelta));
    ++g_renderFramesInWindow;

    const double timingWindowElapsed = frameTime - g_timingWindowStart;
    if (timingWindowElapsed >= 0.5) {
        g_renderFpsDisplay = static_cast<double>(g_renderFramesInWindow) / timingWindowElapsed;
        g_physicsTickRateDisplay = static_cast<double>(g_physicsTicksInWindow) / timingWindowElapsed;
        g_baseLogicTickRateDisplay = static_cast<double>(g_baseLogicTicksInWindow) / timingWindowElapsed;
        g_renderFramesInWindow = 0;
        g_physicsTicksInWindow = 0;
        g_baseLogicTicksInWindow = 0;
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == -1) {
        printf("Could not initialise SDL2: %s\n", SDL_GetError());
        exit(-1);
    }
#else
    SDL_Surface* screen = NULL;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTTHREAD) == -1) {
        printf("Could not initialise SDL: %s\n", SDL_GetError());
        exit(-1);
    }
#endif
    atexit(SDL_Quit);

#ifdef USE_SDL2
    ResetGamepadSlots();
    SDL_GameControllerEventState(SDL_ENABLE);
    OpenInitialGamepads();
#endif

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
    int screenH, screenW;
#ifdef USE_SDL2
    flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
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
            screenW = customWidth > 0 ? customWidth : 1920;
            screenH = customHeight > 0 ? customHeight : 1080;
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
    g_requestedScreenScale = customScale;
#ifdef USE_SDL2
    if (flags & SDL_WINDOW_FULLSCREEN || flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
#else
    if (flags & SDL_FULLSCREEN)
#endif
        SDL_ShowCursor(SDL_DISABLE);
    ApplyWindowLayout(screenW, screenH, true);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glEnable(GL_DEPTH_TEST);
    glAlphaFunc(GL_NOTEQUAL, 0);
    //    glEnable(GL_ALPHA_TEST);
    //    glShadeModel(GL_FLAT);
    glDisable(GL_LIGHTING);
    // Disable texture mapping by default (only DrawTrack() enables it)
    pDevice.SetTextureStageState(0, TSS_COLOROP, TOP_DISABLE);

    sound_init();

    CreateFonts();
    LoadTextures();

    if (!InitialiseData()) {
        printf("Error initialising data\n");
        exit(-3);
    }

    CreateBuffers(&pDevice);

    DSInit();
    DSSetMode();

    glClearColor(0, 0, 0, 1);
    RefreshCombinedInput();
    ResetFourteenFrameTiming();
#ifdef __EMSCRIPTEN__
    CapturePreviousCarState();
    g_lastFrameTime = GetTimeSeconds();
    g_logicAccumulator = BASE_LOGIC_STEP_SECONDS;
    g_baseLogicSubstepCounter = 0;
    g_logicInput = lastInput;
    ResetControlSamplingWindow();
    g_timingWindowStart = g_lastFrameTime;
    emscripten_set_main_loop(em_main_loop, 0, 1);
#else
    bool run = true;
    CapturePreviousCarState();
    g_lastFrameTime = GetTimeSeconds();
    g_logicAccumulator = BASE_LOGIC_STEP_SECONDS;
    g_baseLogicSubstepCounter = 0;
    g_logicInput = lastInput;
    ResetControlSamplingWindow();
    g_timingWindowStart = g_lastFrameTime;
    while (run) {
        run = RunFrame(GetTimeSeconds(), true);
    }
#endif
    FreeData();

    CloseFonts();

#ifdef USE_SDL2
    CloseAllGamepads();
#endif
    sound_destroy();
    TTF_Quit();
    SDL_Quit();

    exit(0);
}
