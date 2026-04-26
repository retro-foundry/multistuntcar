//--------------------------------------------------------------------------------------
// File: StuntCarRacer.cpp
//
// SDL/OpenGL runtime entry and game loop.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "platform_sdl_gl.h"

#include <iomanip>
#include <sstream>

#include "StuntCarRacer.h"
#include "3D_Engine.h"
#include "Backdrop.h"
#include "Track.h"
#include "Car.h"
#include "Car_Behaviour.h"
#include "Opponent_Behaviour.h"
#include "PhysicsConfig.h"
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
/* Perspective depth range: keep near/far ratio modest to avoid depth-buffer precision loss and z-fighting */
#define PERSPECTIVE_NEAR (5.0f)
#define PERSPECTIVE_FAR  (262144.0f)
/* Overlapping split ranges for depth partitioning (far pass first, then near pass). */
#define PERSPECTIVE_NEAR_PASS_FAR (8000.0f)
#define PERSPECTIVE_FAR_PASS_NEAR (2000.0f)
/* Cap physics steps per frame to avoid catch-up stutter and spiral of death when the game can't keep up. */
static const int MAX_PHYSICS_STEPS_PER_FRAME = 10;
/* Body-dynamics integration rate from PHYSICS_UPDATE_HZ (PhysicsConfig.h). */
static double g_physicsStepSeconds = 1.0 / PHYSICS_UPDATE_HZ;
/* Number of physics steps per logic period (for audio spread); game logic itself uses real-time accumulator. */
static int g_physicsSubstepsPerBaseLogic =
    static_cast<int>(PHYSICS_REFERENCE_STEP_SECONDS * PHYSICS_UPDATE_HZ + 0.5);
static const double g_logicTickInterval = (double)PHYSICS_REFERENCE_STEP_SECONDS;

GameModeType GameMode = TRACK_MENU;

// Both the following are used for keyboard input
UINT keyPress = '\0';
DWORD lastInput = 0;
static DWORD g_keyboardInput = 0;
static DWORD g_player2Input = 0;
#ifdef __EMSCRIPTEN__
static DWORD g_remotePlayer2Input = 0;
static bool g_webrtcGuestConnected = false;
static bool g_webrtcReturnToMenuRequested = false;

extern "C" {
/* Called from JS when WebRTC guest is connected; guest input is passed as player 2 only (in-game, not menus). */
EMSCRIPTEN_KEEPALIVE void SetWebRTCGuestConnected(int connected) {
    if (!connected) {
        if (g_webrtcGuestConnected && (GameMode == TRACK_PREVIEW || GameMode == GAME_IN_PROGRESS || GameMode == GAME_OVER))
            g_webrtcReturnToMenuRequested = true;
        g_remotePlayer2Input = 0;
    }
    g_webrtcGuestConnected = (connected != 0);
}
EMSCRIPTEN_KEEPALIVE void SetWebRTCGuestPlayer2Input(unsigned int input) {
    g_remotePlayer2Input = input;
}
}
#endif

static IDirectSound8* ds;
IDirectSoundBuffer8* WreckSoundBuffer = NULL;
IDirectSoundBuffer8* HitCarSoundBuffer = NULL;
IDirectSoundBuffer8* GroundedSoundBuffer = NULL;
IDirectSoundBuffer8* CreakSoundBuffer = NULL;
IDirectSoundBuffer8* SmashSoundBuffer = NULL;
IDirectSoundBuffer8* OffRoadSoundBuffer = NULL;
IDirectSoundBuffer8* EngineSoundBuffers[8] = {NULL};
IDirectSoundBuffer8* EngineSoundBuffers2[8] = {NULL};

GpuTexture* g_pAtlas = NULL;
GpuTexture* g_pCockpitAtlas = NULL;

/* Debug builds default to muted audio. Flip this to true to quickly re-enable while debugging. */
#if defined(DEBUG) || defined(_DEBUG)
static const bool kEnableAudioInDebug = false;
#else
static const bool kEnableAudioInDebug = true;
#endif

bool IsAudioEnabled(void) {
    return kEnableAudioInDebug;
}

bool IsWebRTCGuestConnected(void) {
#ifdef __EMSCRIPTEN__
    return g_webrtcGuestConnected;
#else
    return false;
#endif
}

int wideScreen = 0;

static bool bFrameMoved = FALSE;
static double g_logicAccumulator = 0.0;
/** Accumulates real time; game logic runs every g_logicTickInterval (PHYSICS_REFERENCE_STEP_SECONDS). */
static double g_logicTickAccumulator = 0.0;
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
#define MAX_LOCAL_PLAYERS 8
#define GAMEPAD_STEER_DEADZONE 12000
#define GAMEPAD_TRIGGER_THRESHOLD 16000

typedef struct {
    SDL_GameController* handle;
    SDL_JoystickID instanceId;
} GAMEPAD_SLOT;

static GAMEPAD_SLOT g_gamepadSlots[MAX_LOCAL_PLAYERS];
static DWORD g_gamepadInput[MAX_LOCAL_PLAYERS] = {0};
// In multiplayer, the device that confirms start on TRACK_PREVIEW becomes Player 1.
static bool g_pendingMultiplayerStarterIsKeyboard = false;
static SDL_JoystickID g_pendingMultiplayerStarterInstanceId = -1;
static bool g_multiplayerPlayer1IsKeyboard = false;
static SDL_JoystickID g_multiplayerPlayer1InstanceId = -1;
#endif

bool bShowStats = FALSE;
bool bNewGame = FALSE;
bool bPaused = FALSE;
bool bPlayerPaused = FALSE;
bool bOpponentPaused = FALSE;
bool bMultiplayerMode = FALSE;
bool bFauxMultiplayerMode = FALSE;
long bTrackDrawMode = 0;
bool bOutsideView = FALSE;
extern long engineSoundPlaying;
double gameStartTime, gameEndTime;
bool bSuperLeague = FALSE;
/* Split-screen orientation: true = horizontal (top/bottom), false = vertical (left/right). */
static bool g_splitScreenHorizontal = true;

static bool IsSplitScreenMode(void) {
    return bMultiplayerMode || bFauxMultiplayerMode;
}

static float GetPlayerCarRenderYOffset(void) {
    if (IsSplitScreenMode())
        return (VCAR_HEIGHT / 4.0f) + (VCAR_HEIGHT / 24.0f);
    return VCAR_HEIGHT / 3.0f;
}

static float GetOpponentCarRenderYOffset(void) {
    if (IsSplitScreenMode())
        return (VCAR_HEIGHT / 4.0f) + (VCAR_HEIGHT / 24.0f);
    return VCAR_HEIGHT / 4.0f;
}

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

static long ResolveDamagedLimitForTrackLeague(void) {
    // Original Amiga loads damaged.limit from per-track metadata bytes (B.1ca2a/B.1ca2b).
    // Converted PC track data does not currently carry those bytes, so use a league-aware
    // fallback that matches original broad pacing better than the legacy fixed value (10).
    static const unsigned char kStandardLeagueDamageLimitByTrack[NUM_TRACKS] = {7, 7, 7, 7, 7, 7, 7, 7};
    static const unsigned char kSuperLeagueDamageLimitByTrack[NUM_TRACKS] = {7, 3, 3, 3, 3, 3, 7, 3};

    const unsigned char* table = bSuperLeague ? kSuperLeagueDamageLimitByTrack : kStandardLeagueDamageLimitByTrack;
    if (TrackID < 0 || TrackID >= NUM_TRACKS)
        return bSuperLeague ? 3 : 7;
    return static_cast<long>(table[TrackID]);
}

//-----------------------------------------------------------------------------
// Static variables
//-----------------------------------------------------------------------------
// Player 1 orientation
static long player1_x = 0, player1_y = 0, player1_z = 0;

static long player1_x_angle = (0 << 6), player1_y_angle = (0 << 6), player1_z_angle = (0 << 6);
static long player1_render_x = 0, player1_render_y = 0, player1_render_z = 0;

// Opponent orientation
static long opponent_x = 0, opponent_y = 0, opponent_z = 0;

static float opponent_x_angle = 0.0f, opponent_y_angle = 0.0f, opponent_z_angle = 0.0f;
static long opponent_render_x = 0, opponent_render_y = 0, opponent_render_z = 0;

// Previous logic-tick state for render interpolation
static long prev_player1_x = 0, prev_player1_y = 0, prev_player1_z = 0;
static long prev_player1_x_angle = 0, prev_player1_y_angle = 0, prev_player1_z_angle = 0;
static long prev_opponent_x = 0, prev_opponent_y = 0, prev_opponent_z = 0;
static float prev_opponent_x_angle = 0.0f, prev_opponent_y_angle = 0.0f, prev_opponent_z_angle = 0.0f;
static long prev_player1_render_x = 0, prev_player1_render_y = 0, prev_player1_render_z = 0;
static long prev_opponent_render_x = 0, prev_opponent_render_y = 0, prev_opponent_render_z = 0;
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
static void InitialiseBoostStartStateForRace(long reserve);
static void DrawCenteredTextLine(TextHelper& txtHelper, const std::wstring& line, int y);

#ifdef USE_SDL2
static void ResetGamepadSlots(void);
static void OpenInitialGamepads(void);
static void HandleGamepadDeviceAdded(int deviceIndex);
static void HandleGamepadDeviceRemoved(SDL_JoystickID instanceId);
static DWORD BuildGamepadInputForPlayer(SDL_GameController* controller);
static void RefreshGamepadInput(void);
static void CloseAllGamepads(void);
extern SDL_Window* window;
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

    // Second set for player 2 (bottom split-screen) so both engines mix.
    if ((EngineSoundBuffers2[0] = MakeSoundBuffer(ds, L"TICKOVER")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers2[1] = MakeSoundBuffer(ds, L"ENGINEPITCH2")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers2[2] = MakeSoundBuffer(ds, L"ENGINEPITCH3")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers2[3] = MakeSoundBuffer(ds, L"ENGINEPITCH4")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers2[4] = MakeSoundBuffer(ds, L"ENGINEPITCH5")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers2[5] = MakeSoundBuffer(ds, L"ENGINEPITCH6")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers2[6] = MakeSoundBuffer(ds, L"ENGINEPITCH7")) == NULL)
        return FALSE;
    if ((EngineSoundBuffers2[7] = MakeSoundBuffer(ds, L"ENGINEPITCH8")) == NULL)
        return FALSE;

    for (i = 0; i < 8; i++) {
        EngineSoundBuffers2[i]->SetPan(DSBPAN_CENTER);
        EngineSoundBuffers2[i]->SetVolume(AmigaVolumeToMixerGain(48 / 2));
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
        if (EngineSoundBuffers2[i])
            EngineSoundBuffers2[i]->Release(), EngineSoundBuffers2[i] = NULL;
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
#ifdef USE_SDL2
    if (window) {
        /* Return ortho/logical dimensions so backdrop and 3D projection use the same space as glOrtho(0, projWidth, 480, 0). */
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        if (vp[2] > 0 && vp[3] > 0) {
            *screen_width = (480L * (long)vp[2]) / (long)vp[3];
            *screen_height = 480;
            return;
        }
    }
#endif
#ifdef linux
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

#define NUM_PALETTE_ENTRIES (42 + 7)
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

    // extra track colour (original orange pack)
    {0xff, 0x7a, 0x18}, // SCR_BASE_COLOUR+22
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
    const SurfaceDesc* desc = GetBackBufferSurfaceDesc();
    if (!desc || desc->Height <= 0)
        return 1.0f;
    return static_cast<float>(desc->Height) / 480.0f;
}
GLuint g_pSprite = 0; // Texture for batching text calls
// some helper functions....
void CreateFonts() {
    if (!TTF_WasInit() && TTF_Init() == -1) {
        printf("TTF_Init: %s\n", TTF_GetError());
        exit(1);
    }

    // Load at 2x base size so the glyph texture is high-res; we scale quads to display size
    if (g_pFont == NULL) {
        g_pFont = TTF_OpenFont("data/DejaVuSans-Bold.ttf", 36);
    }
    if (g_pFontLarge == NULL) {
        g_pFontLarge = TTF_OpenFont("data/DejaVuSans-Bold.ttf", 60);
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
    if (!g_pCockpitAtlas)
        g_pCockpitAtlas = new GpuTexture();
    g_pAtlas->LoadTexture("data/Bitmap/atlas.png");
    g_pCockpitAtlas->LoadTexture("data/Bitmap/atlas2.png");
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

/** angleIncrement: units per call (e.g. 128 per logic tick, or 128*stepSeconds/logicTickInterval per physics step). */
static void CalcTrackMenuViewpoint(float angleIncrement) {
    static double menu_circle_angle = 0.0;

    short sin, cos;
    long centre = (NUM_TRACK_CUBES * CUBE_SIZE) / 2;
    long radius = ((NUM_TRACK_CUBES - 2) * CUBE_SIZE) / PRECISION;

    // Target orientation - centre of world
    target_x = (NUM_TRACK_CUBES * CUBE_SIZE) / 2;
    target_y = 0;
    target_z = (NUM_TRACK_CUBES * CUBE_SIZE) / 2;

    // camera moves in a circle around the track
    if (!bPaused) {
        menu_circle_angle += (double)angleIncrement;
        while (menu_circle_angle >= (double)MAX_ANGLE)
            menu_circle_angle -= (double)MAX_ANGLE;
        while (menu_circle_angle < 0.0)
            menu_circle_angle += (double)MAX_ANGLE;
    }
    long circle_y_angle = (long)menu_circle_angle;

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

static void CalcGameViewpointForCar(long car_x, long car_y, long car_z, long car_x_angle, long car_y_angle,
                                    long car_z_angle, long* viewpoint_x, long* viewpoint_y, long* viewpoint_z,
                                    long* viewpoint_x_angle, long* viewpoint_y_angle, long* viewpoint_z_angle) {
    long x_offset, y_offset, z_offset;

    if (bOutsideView) {
        // set Viewpoint 1 to behind Player 1
        // 04/11/1998 - would probably need to do a final rotation (i.e. of the trig. coefficients)
        //                to allow a viewpoint with e.g. a different X angle to that of the player.
        //                For the car this would mean the following rotations: Y,X,Z, Y,X,Z, X
        //                For the viewpoint this would mean the following rotations: Y,X,Z, X (possibly!)
        CalcYXZTrigCoefficients(car_x_angle, car_y_angle, car_z_angle);

        // vector from centre of car
        x_offset = 0;
        y_offset = 0xc0;
        z_offset = 0x300;
        WorldOffset(&x_offset, &y_offset, &z_offset);
        *viewpoint_x = (car_x - x_offset);
        *viewpoint_y = (car_y - y_offset);
        *viewpoint_z = (car_z - z_offset);

        *viewpoint_x_angle = car_x_angle;
        //viewpoint1_x_angle = (player1_x_angle + (48<<6)) & (MAX_ANGLE-1);
        *viewpoint_y_angle = car_y_angle;
        //viewpoint1_y_angle = (player1_y_angle - (64<<6)) & (MAX_ANGLE-1);
        *viewpoint_z_angle = car_z_angle;
        //viewpoint1_x_angle = 0;
        //viewpoint1_z_angle = 0;
    } else {
        *viewpoint_x = car_x;
        *viewpoint_y = car_y - (HEIGHT_ABOVE_ROAD << LOG_PRECISION);
        //        viewpoint1_y = player1_y - (90 << LOG_PRECISION);
        *viewpoint_z = car_z;

        *viewpoint_x_angle = car_x_angle;
        *viewpoint_y_angle = car_y_angle;
        *viewpoint_z_angle = car_z_angle;
    }
}

static void CalcGameViewpoint(void) {
    CalcGameViewpointForCar(player1_x, player1_y, player1_z, player1_x_angle, player1_y_angle, player1_z_angle,
                            &viewpoint1_x, &viewpoint1_y, &viewpoint1_z, &viewpoint1_x_angle, &viewpoint1_y_angle,
                            &viewpoint1_z_angle);
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

static long RadiansToPlayerAngle(float angle) {
    const float wrapped = NormalizeRadians(angle);
    const long units = static_cast<long>((wrapped * 65536.0f) / (2.0f * PI));
    return units & (MAX_ANGLE - 1);
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

static void UpdateProjectedRenderPositions(void) {
    player1_render_x = player1_x;
    player1_render_y = player1_y;
    player1_render_z = player1_z;
    ProjectCarRenderPositionToRoadNormalForInstance(0, &player1_render_x, &player1_render_y, &player1_render_z);

    opponent_render_x = opponent_x;
    opponent_render_y = opponent_y;
    opponent_render_z = opponent_z;
    if (bMultiplayerMode)
        ProjectCarRenderPositionToRoadNormalForInstance(1, &opponent_render_x, &opponent_render_y, &opponent_render_z);
}

static void PrimeMultiplayerPlayer2StartFromSinglePlayerOpponent(void) {
    if (!bMultiplayerMode || TrackID == NO_TRACK)
        return;

    const bool savedNewGame = bNewGame;
    bNewGame = TRUE;

    float spawnXa = 0.0f, spawnYa = 0.0f, spawnZa = 0.0f;
    OpponentBehaviour(&opponent_x, &opponent_y, &opponent_z, &spawnXa, &spawnYa, &spawnZa, true,
                      static_cast<float>(g_physicsStepSeconds));
    opponent_x_angle = spawnXa;
    opponent_y_angle = spawnYa;
    opponent_z_angle = spawnZa;

    bNewGame = savedNewGame;
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
    prev_player1_render_x = player1_render_x;
    prev_player1_render_y = player1_render_y;
    prev_player1_render_z = player1_render_z;
    prev_opponent_render_x = opponent_render_x;
    prev_opponent_render_y = opponent_render_y;
    prev_opponent_render_z = opponent_render_z;
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
    CapturePreviousPlayerShadowForInstance(0);
    if (bMultiplayerMode)
        CapturePreviousPlayerShadowForInstance(1);

    have_prev_car_state = true;
}

static void UpdateInterpolatedCarTransforms(RenderDevice* pDevice, float alpha) {
    if (!have_prev_car_state)
        CapturePreviousCarState();

    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    // TRACK_MENU now updates viewpoint every physics step and captures prev, so we can interpolate.
    const float backdropY = LerpLong(prev_viewpoint1_y, viewpoint1_y, alpha);
    const float backdropXa = LerpWrappedAngleUnits(prev_viewpoint1_x_angle, viewpoint1_x_angle, alpha);
    const float backdropYa = LerpWrappedAngleUnits(prev_viewpoint1_y_angle, viewpoint1_y_angle, alpha);
    const float backdropZa = LerpWrappedAngleUnits(prev_viewpoint1_z_angle, viewpoint1_z_angle, alpha);
    render_backdrop_viewpoint_y = static_cast<long>(backdropY);
    render_backdrop_viewpoint_x_angle = static_cast<long>(backdropXa) & (MAX_ANGLE - 1);
    render_backdrop_viewpoint_y_angle = static_cast<long>(backdropYa) & (MAX_ANGLE - 1);
    render_backdrop_viewpoint_z_angle = static_cast<long>(backdropZa) & (MAX_ANGLE - 1);

    const float playerX = LerpFixedCoord(prev_player1_render_x, player1_render_x, alpha);
    const float playerY = LerpFixedCoord(prev_player1_render_y, player1_render_y, alpha);
    const float playerZ = LerpFixedCoord(prev_player1_render_z, player1_render_z, alpha);
    const float playerXa = LerpWrappedPlayerAngle(prev_player1_x_angle, player1_x_angle, alpha);
    const float playerYa = LerpWrappedPlayerAngle(prev_player1_y_angle, player1_y_angle, alpha);
    const float playerZa = LerpWrappedPlayerAngle(prev_player1_z_angle, player1_z_angle, alpha);
    const float playerCarYOffset = GetPlayerCarRenderYOffset();
    BuildCarWorldTransform(&matWorldCar, playerX, playerY, playerZ, playerXa, playerYa, playerZa,
                           playerCarYOffset);

    const float opponentX = LerpFixedCoord(prev_opponent_render_x, opponent_render_x, alpha);
    const float opponentY = LerpFixedCoord(prev_opponent_render_y, opponent_render_y, alpha);
    const float opponentZ = LerpFixedCoord(prev_opponent_render_z, opponent_render_z, alpha);
    const float opponentXa = LerpWrappedRadians(prev_opponent_x_angle, opponent_x_angle, alpha);
    const float opponentYa = LerpWrappedRadians(prev_opponent_y_angle, opponent_y_angle, alpha);
    const float opponentZa = LerpWrappedRadians(prev_opponent_z_angle, opponent_z_angle, alpha);
    float opponentCarYOffset = GetOpponentCarRenderYOffset();
    BuildCarWorldTransform(&matWorldOpponentsCar, opponentX, opponentY, opponentZ, opponentXa, opponentYa, opponentZa,
                           opponentCarYOffset);

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
    } else if (GameMode == TRACK_MENU) {
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
    } else if (GameMode == TRACK_PREVIEW) {
        // Use current viewpoint/target only; lerping with prev (from menu or prior frame) caused spinning.
        const float viewX = static_cast<float>(viewpoint1_x);
        const float viewY = -static_cast<float>(viewpoint1_y) / static_cast<float>(1 << LOG_PRECISION);
        const float viewZ = static_cast<float>(viewpoint1_z);
        const float lookX = static_cast<float>(target_x);
        const float lookY = static_cast<float>(target_y);
        const float lookZ = static_cast<float>(target_z);
        glm::vec3 vEyePt(viewX, viewY, viewZ);
        glm::vec3 vLookatPt(lookX, lookY, lookZ);
        mat4LookAt(&matView, &vEyePt, &vLookatPt, &vUpVec);
        pDevice->SetTransform(TS_VIEW, &matView);
    }
}

static void SetCarWorldTransform(void) {
    const float playerCarYOffset = GetPlayerCarRenderYOffset();
    BuildCarWorldTransform(&matWorldCar, FixedPointToWorldCoord(player1_render_x), FixedPointToWorldCoord(player1_render_y),
                           FixedPointToWorldCoord(player1_render_z), PlayerAngleToRadians(player1_x_angle),
                           PlayerAngleToRadians(player1_y_angle), PlayerAngleToRadians(player1_z_angle),
                           playerCarYOffset);
}

static void SetOpponentsCarWorldTransform(void) {
    float opponentCarYOffset = GetOpponentCarRenderYOffset();
    BuildCarWorldTransform(&matWorldOpponentsCar, FixedPointToWorldCoord(opponent_render_x),
                           FixedPointToWorldCoord(opponent_render_y), FixedPointToWorldCoord(opponent_render_z),
                           opponent_x_angle, opponent_y_angle, opponent_z_angle, opponentCarYOffset);
}

static void RestartEngineAudioBuffers(bool resetEngineModel) {
    const bool splitScreen = IsSplitScreenMode();

    // Stop all active engine buffer sets regardless of audio-enabled flag.
    for (int i = 0; i < 8; ++i) {
        if (EngineSoundBuffers[i]) {
            EngineSoundBuffers[i]->Stop();
            EngineSoundBuffers[i]->SetCurrentPosition(0);
        }
        if (splitScreen && EngineSoundBuffers2[i]) {
            EngineSoundBuffers2[i]->Stop();
            EngineSoundBuffers2[i]->SetCurrentPosition(0);
        }
    }

    // Reset engineSoundPlaying (and optionally the engine model) per instance
    // so the per-instance state stays consistent.
    if (splitScreen) {
        const long prev0 = PushCarBehaviourInstance(0);
        engineSoundPlaying = FALSE;
        if (resetEngineModel) ResetEngineAudioState();
        PopCarBehaviourInstance(prev0);
        const long prev1 = PushCarBehaviourInstance(1);
        engineSoundPlaying = FALSE;
        if (resetEngineModel) ResetEngineAudioState();
        PopCarBehaviourInstance(prev1);
    } else {
        engineSoundPlaying = FALSE;
        if (resetEngineModel) ResetEngineAudioState();
    }
}

void RequestRestartEngineAudioOnFirstInput(void) {
    g_restartEngineAudioOnFirstInput = true;
}

static void StopEngineSound(void) {
    if (engineSoundPlaying) {
        RestartEngineAudioBuffers(true);
    }
}

void CALLBACK OnFrameMove(RenderDevice* pDevice, double fTime, float fElapsedTime, void* pUserContext) {
    static glm::vec3 vUpVec(0.0f, 1.0f, 0.0f);
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
        // Engine/audio is stepped from the physics substep loop in RunFrame().
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

    // CarBehaviour, OpponentBehaviour, and LimitViewpointY for GAME_IN_PROGRESS are
    // now stepped in the physics substep loop inside RunFrame() at g_physicsStepSeconds
    // intervals.  For TRACK_PREVIEW (no per-substep physics) the call stays here.
    if (GameMode == TRACK_PREVIEW) {
        LimitViewpointY(&player1_y);
    }

    if ((GameMode == GAME_IN_PROGRESS) && (!bPaused)) {
        UpdateLapData();
        AdvanceFourteenFrameTiming();
    }

    if ((GameMode == TRACK_MENU) || (GameMode == TRACK_PREVIEW)) {
        if (GameMode == TRACK_MENU) {
            // Viewpoint/target updated every physics step in RunFrame() for smooth interpolation.
        } else {
            // TRACK_PREVIEW: viewpoint/target updated per physics step in RunFrame(); only set opponent transform here.
            SetOpponentsCarWorldTransform();
        }

        // Set Direct3D transforms (TRACK_MENU and TRACK_PREVIEW viewpoint/target already scaled in RunFrame substep loop)

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
        // CalcGameViewpoint() and viewpoint scaling are now done inside the
        // per-substep physics block in RunFrame(), so viewpoint1_x/z are already
        // in render-coordinate scale here.

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
#define STARTMENU SDLK_RETURN
#define LEAGUEMENU SDLK_l

static long GetTotalTrackMenuSelections(void) {
    long total = 0;
    for (long pack = 0; pack < NUM_TRACK_PACKS; ++pack)
        total += GetTrackPackTrackCount(static_cast<TrackPack>(pack));
    return total;
}

static long GetTrackMenuSelection(TrackPack pack, long track) {
    long selection = 0;
    for (long p = 0; p < NUM_TRACK_PACKS; ++p) {
        const TrackPack currentPack = static_cast<TrackPack>(p);
        const long count = GetTrackPackTrackCount(currentPack);
        if (currentPack == pack) {
            if (track < 0)
                track = 0;
            if (track >= count)
                track = count - 1;
            return selection + track;
        }
        selection += count;
    }
    return 0;
}

static void ResolveTrackMenuSelection(long selection, TrackPack* pack, long* track) {
    for (long p = 0; p < NUM_TRACK_PACKS; ++p) {
        const TrackPack currentPack = static_cast<TrackPack>(p);
        const long count = GetTrackPackTrackCount(currentPack);
        if (selection < count) {
            *pack = currentPack;
            *track = selection;
            return;
        }
        selection -= count;
    }

    *pack = TRACK_PACK_CLASSIC;
    *track = 0;
}

static void HandleTrackMenu(TextHelper& txtHelper) {
    long track_number;
    float textScale = GetTextScale();
    const SurfaceDesc* pd3dsdBackBuffer = GetBackBufferSurfaceDesc();
    int titleSize = static_cast<int>(30 * textScale);
    int regularSize = static_cast<int>(15 * textScale);
    if (titleSize < 1)
        titleSize = 1;
    if (regularSize < 1)
        regularSize = 1;

    const int titleY = static_cast<int>(15 * 3 * textScale);
    const int subtitleY = titleY + titleSize;
    const int trackY = static_cast<int>(15 * 9 * textScale);
    const int packY = trackY + regularSize;
    const int bottomInfoY = static_cast<int>(pd3dsdBackBuffer->Height - 15 * 9 * textScale);

    txtHelper.SetDisplaySize(titleSize);
    DrawCenteredTextLine(txtHelper, L"Multi Stunt Car", titleY);

    txtHelper.SetDisplaySize(regularSize);
    DrawCenteredTextLine(txtHelper, L"Alpha Version - WIP", subtitleY);
    {
        std::wstringstream ss;
        ss << L"Track: " << (TrackID == NO_TRACK ? L"None" : GetTrackName(TrackID));
        DrawCenteredTextLine(txtHelper, ss.str(), trackY);
    }
    {
        std::wstringstream ss;
        ss << L"Track Pack: " << GetTrackPackName();
        DrawCenteredTextLine(txtHelper, ss.str(), packY);
    }
    DrawCenteredTextLine(txtHelper, L"Left/Right or D-pad = change track.  Enter or A = select.  Escape = quit.", bottomInfoY);
    DrawCenteredTextLine(txtHelper, L"'L' to switch Super League On/Off", bottomInfoY + regularSize);

    const bool goPrev = (keyPress == SDLK_LEFT);
    const bool goNext = (keyPress == SDLK_RIGHT);
    if (goPrev || goNext || (keyPress == LEAGUEMENU)) {
        static long menuTrackSelection = 0;
        TrackPack previous_pack = GetTrackPack();
        bool changed_pack = false;

        if (TrackID != NO_TRACK)
            menuTrackSelection = GetTrackMenuSelection(GetTrackPack(), TrackID);

        if (keyPress == LEAGUEMENU) {
            bSuperLeague = !bSuperLeague;
            track_number = TrackID;
            CreateCarVertexBuffer(GetRenderDevice()); // recreate car
        } else {
            const long totalTracks = GetTotalTrackMenuSelections();
            if (goNext)
                menuTrackSelection = (menuTrackSelection + 1) % totalTracks;
            else
                menuTrackSelection = (menuTrackSelection + totalTracks - 1) % totalTracks;

            TrackPack desiredPack;
            ResolveTrackMenuSelection(menuTrackSelection, &desiredPack, &track_number);
            if (desiredPack != previous_pack) {
                if (!SetTrackPack(desiredPack)) {
#if defined(DEBUG) || defined(_DEBUG)
                    fprintf(out, "Failed to switch track pack\n");
#endif
                    MessageBox(NULL, L"Failed to switch track pack", L"Error", MB_OK);
                    keyPress = '\0';
                    return;
                }
                changed_pack = true;
            }
        }

        if (!ConvertAmigaTrack(track_number)) {
            if (changed_pack)
                SetTrackPack(previous_pack);
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
#ifdef USE_SDL2
        // Starting a fresh preview from the menu should always re-run multiplayer starter selection.
        g_pendingMultiplayerStarterIsKeyboard = false;
        g_pendingMultiplayerStarterInstanceId = -1;
        g_multiplayerPlayer1IsKeyboard = false;
        g_multiplayerPlayer1InstanceId = -1;
#endif
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
    const int leftX = static_cast<int>((2 + (wideScreen ? 10 : 0)) * textScale);
    int lineStep = static_cast<int>(15 * textScale);
    if (lineStep < 1)
        lineStep = 1;

    const bool selectSinglePlayer = (keyPress == SDLK_LEFT);
    const bool selectMultiplayer = (keyPress == SDLK_RIGHT);
    if (selectSinglePlayer || selectMultiplayer) {
        bMultiplayerMode = selectMultiplayer;
        // Left/Right explicitly selects single vs multiplayer; faux mode remains a hidden F8 toggle.
        bFauxMultiplayerMode = false;
#ifdef USE_SDL2
        // Re-pick Player 1 controller when multiplayer is selected and started.
        g_pendingMultiplayerStarterIsKeyboard = false;
        g_pendingMultiplayerStarterInstanceId = -1;
        g_multiplayerPlayer1IsKeyboard = false;
        g_multiplayerPlayer1InstanceId = -1;
#endif
        keyPress = '\0';
    }

    const int topStartY = lineStep;
    std::wstringstream trackLine;
    trackLine << L"Selected track - " << (TrackID == NO_TRACK ? L"None" : GetTrackName(TrackID));
    DrawCenteredTextLine(txtHelper, trackLine.str(), topStartY);
    DrawCenteredTextLine(txtHelper, L"Press Enter or A to start game", topStartY + lineStep);
    DrawCenteredTextLine(txtHelper, L"'M', Select or Escape = back to track menu", topStartY + lineStep * 2);
    DrawCenteredTextLine(txtHelper, L"(Press F4 to change scenery)", topStartY + lineStep * 3);

    int controlsLineCount = 6;
    if (bMultiplayerMode)
        ++controlsLineCount;
    if (bFauxMultiplayerMode)
        ++controlsLineCount;
    const int controlsStartY = pd3dsdBackBuffer->Height - lineStep - controlsLineCount * lineStep;

    const int topSectionEndY = topStartY + lineStep * 4;
    int multiplayerToggleY = ((topSectionEndY + controlsStartY) / 2) - lineStep;
    if (multiplayerToggleY < topSectionEndY)
        multiplayerToggleY = topSectionEndY;
    const int multiplayerHintY = multiplayerToggleY + lineStep;
    const std::wstring multiplayerModeLine = bMultiplayerMode ? L"Mode: Multiplayer" : L"Mode: Single Player";
    DrawCenteredTextLine(txtHelper, multiplayerModeLine, multiplayerToggleY);
    DrawCenteredTextLine(txtHelper, L"Left/Right = select Single Player / Multiplayer", multiplayerHintY);

    txtHelper.SetInsertionPos(leftX, controlsStartY);
    txtHelper.DrawTextLine(L"Keyboard controls during game :-");
#if defined(PANDORA) || defined(PYRA)
    txtHelper.DrawTextLine(L"  DPad = Steer, (X) = Accelerate, (B) = Brake, (R) = Nitro");
#else
    txtHelper.DrawTextLine(
        L"  Arrow left = Steer left, Arrow right = Steer right, Space = Accelerate, Arrow Down = Brake");
#endif
    txtHelper.DrawTextLine(L"Gamepad controls :-");
    txtHelper.DrawTextLine(L"  Left stick/D-Pad = Steer, RT = Accelerate, LT or B = Brake, A/X/RB = Boost");
    if (bMultiplayerMode)
        txtHelper.DrawTextLine(L"Multiplayer controls: Use gamepads or keyboard.");
    if (bFauxMultiplayerMode)
        txtHelper.DrawTextLine(L"Faux multiplayer: normal controls for Player 1, AI drives Player 2");
    txtHelper.DrawTextLine(L"  R = Point car in opposite direction, P = Pause, O = Unpause");
    txtHelper.DrawTextLine(L"  M, Select or Escape = Back to track menu");

    if (keyPress == STARTMENU) {
#ifdef USE_SDL2
        if (bMultiplayerMode) {
            g_multiplayerPlayer1IsKeyboard = g_pendingMultiplayerStarterIsKeyboard;
            g_multiplayerPlayer1InstanceId = g_pendingMultiplayerStarterInstanceId;
        } else {
            g_multiplayerPlayer1IsKeyboard = false;
            g_multiplayerPlayer1InstanceId = -1;
        }
        g_pendingMultiplayerStarterIsKeyboard = false;
        g_pendingMultiplayerStarterInstanceId = -1;
#endif
        RestartEngineAudioBuffers(true);
        PrimeMultiplayerPlayer2StartFromSinglePlayerOpponent();
        bNewGame = TRUE;
        GameMode = GAME_IN_PROGRESS;
        g_restartEngineAudioOnFirstInput = true;
        // Trigger an immediate logic tick on first frame of race.
        g_logicTickAccumulator = g_logicTickInterval;
        g_logicAccumulator = g_physicsStepSeconds;
        g_logicInput = lastInput;
        ResetControlSamplingWindow();
        // initialise game data
        ResetFourteenFrameTiming();
        ResetLapData(OPPONENT);
        ResetLapData(PLAYER);
        gameStartTime = GetTimeSeconds();
        gameEndTime = 0;
        long initialBoostReserve = StandardBoost;
        if (bSuperLeague) {
            initialBoostReserve = SuperBoost;
            road_cushion_value = 1;
            engine_power = 320;
            boost_unit_value = 12;
            opp_engine_power = 314;
        } else {
            road_cushion_value = 0;
            engine_power = 240;
            boost_unit_value = 16;
            opp_engine_power = 236;
        }
        damaged_limit = ResolveDamagedLimitForTrackLeague();
        InitialiseBoostStartStateForRace(initialBoostReserve);
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
extern long NumTrackPieces;
extern TRACK_PIECE Track[];
extern long player_current_piece;
extern long players_distance_into_section;

static long CalculateTwoPlayerDistanceForHudFromPlayer1(void) {
    if (TrackID == NO_TRACK || NumTrackPieces <= 0)
        return 0;

    static long distancesAroundRoad[MAX_PIECES_PER_TRACK] = {0};
    static long totalRoadDistance = 0;
    static long previousTrackID = NO_TRACK;

    if (previousTrackID != TrackID) {
        long distance = 0;
        for (long piece = 0; piece < NumTrackPieces; ++piece) {
            distancesAroundRoad[piece] = distance << 5;
            distance += Track[piece].numSegments;
        }
        totalRoadDistance = distance << 5;
        previousTrackID = TrackID;
    }

    long p1Piece = 0, p1DistanceIntoSection = 0;
    long p2Piece = 0, p2DistanceIntoSection = 0;

    {
        const long previousInstance = PushCarBehaviourInstance(0);
        p1Piece = player_current_piece;
        p1DistanceIntoSection = players_distance_into_section;
        PopCarBehaviourInstance(previousInstance);
    }
    {
        const long previousInstance = PushCarBehaviourInstance(1);
        p2Piece = player_current_piece;
        p2DistanceIntoSection = players_distance_into_section;
        PopCarBehaviourInstance(previousInstance);
    }

    if (p1Piece < 0 || p1Piece >= NumTrackPieces || p2Piece < 0 || p2Piece >= NumTrackPieces || totalRoadDistance <= 0)
        return 0;

    long diff = ((p2DistanceIntoSection - p1DistanceIntoSection) >> 3);
    diff += distancesAroundRoad[p2Piece] - distancesAroundRoad[p1Piece];

    long absDiff = abs(diff);
    long opposite = totalRoadDistance - absDiff;
    long smallestDistanceBetweenPlayers = opposite;
    if (absDiff < opposite) {
        smallestDistanceBetweenPlayers = absDiff;
        diff = -diff;
    }

    const bool opponentBehindPlayer = (diff > 0);
    long dist = smallestDistanceBetweenPlayers;
    dist += (dist >> 2);
    dist >>= 2;

    if (opponentBehindPlayer)
        dist = -dist;

    return dist;
}

static void QueueMultiplayerCarCollisionImpulsesForStep(void) {
    ClearMultiplayerCarCollisionImpulses();

    if (!bMultiplayerMode || GameMode != GAME_IN_PROGRESS || bPaused || bPlayerPaused || bOpponentPaused)
        return;
    if (TrackID == NO_TRACK || NumTrackPieces <= 0)
        return;

    CarRoadCollisionState p1 = {};
    CarRoadCollisionState p2 = {};
    if (!GetCarRoadCollisionStateForInstance(0, &p1) || !GetCarRoadCollisionStateForInstance(1, &p2))
        return;

    if (!p1.dropStartDone || !p2.dropStartDone)
        return;
    if (p1.piece < 0 || p1.piece >= NumTrackPieces || p2.piece < 0 || p2.piece >= NumTrackPieces)
        return;

    static long distancesAroundRoad[MAX_PIECES_PER_TRACK] = {0};
    static long totalRoadDistance = 0;
    static long previousTrackID = NO_TRACK;

    if (previousTrackID != TrackID) {
        long distance = 0;
        for (long piece = 0; piece < NumTrackPieces; ++piece) {
            distancesAroundRoad[piece] = distance << 5;
            distance += Track[piece].numSegments;
        }
        totalRoadDistance = distance << 5;
        previousTrackID = TrackID;
    }
    if (totalRoadDistance <= 0)
        return;

    long diff = ((p2.distanceIntoSection - p1.distanceIntoSection) >> 3);
    diff += distancesAroundRoad[p2.piece] - distancesAroundRoad[p1.piece];

    long absDiff = abs(diff);
    if (absDiff > totalRoadDistance)
        absDiff %= totalRoadDistance;

    long opposite = totalRoadDistance - absDiff;
    long smallestDistanceBetweenPlayers = opposite;
    if (absDiff < opposite) {
        smallestDistanceBetweenPlayers = absDiff;
        diff = -diff;
    }

    // Compare like-for-like lateral positions (both in surface-space 0..255).
    long xDelta = p2.rearWheelSurfaceXPosition - p1.rearWheelSurfaceXPosition;
    long xDifference = abs(xDelta);
    if (xDifference >= 45)
        return;

    // Use full wrapped road distance, not just low byte, to avoid false hits far away.
    if (smallestDistanceBetweenPlayers > 8)
        return;

    long yImpulse = 0;
    if (!(p1.touchingRoad && p2.touchingRoad)) {
        long d4 = (p1.playerY >> 11) - (p2.playerY >> 11);
        long d0 = d4 + 40;
        if (d0 < 0)
            d0 = -d0;
        if (d0 >= 192)
            return;

        long d3 = 256 - d0;
        if (d4 < 0)
            d3 = -d3;
        yImpulse = d3 << 4;
    }

    long xImpulse = (xDelta < 0) ? WALL_CONTACT_IMPULSE : -WALL_CONTACT_IMPULSE;
    long zImpulse = (p2.playerZSpeed - p1.playerZSpeed) >> 1;
    zImpulse += (zImpulse < 0) ? -3 : 3;

    QueueMultiplayerCarCollisionImpulse(0, xImpulse, yImpulse, zImpulse, true);
    QueueMultiplayerCarCollisionImpulse(1, -xImpulse, -yImpulse, -zImpulse, true);
}

static void SetBoostStartStateForInstance(long instanceIndex, long reserve) {
    const long previousInstance = PushCarBehaviourInstance(instanceIndex);
    boostReserve = reserve;
    boostUnit = 0;
    PopCarBehaviourInstance(previousInstance);
}

static void InitialiseBoostStartStateForRace(long reserve) {
    SetBoostStartStateForInstance(0, reserve);
    if (bMultiplayerMode)
        SetBoostStartStateForInstance(1, reserve);
}

static void BeginLogicTickDamagePeriodForActiveCars(void) {
    {
        const long previousInstance = PushCarBehaviourInstance(0);
        BeginLogicTickDamagePeriod();
        PopCarBehaviourInstance(previousInstance);
    }

    if (!bMultiplayerMode)
        return;

    const long previousInstance = PushCarBehaviourInstance(1);
    BeginLogicTickDamagePeriod();
    PopCarBehaviourInstance(previousInstance);
}

static void UpdateDamageForActiveCars(void) {
    {
        const long previousInstance = PushCarBehaviourInstance(0);
        UpdateDamage();
        PopCarBehaviourInstance(previousInstance);
    }

    if (!bMultiplayerMode)
        return;

    const long previousInstance = PushCarBehaviourInstance(1);
    UpdateDamage();
    PopCarBehaviourInstance(previousInstance);
}

static void UpdateMultiplayerRaceFinishFromWrecks(void) {
    if (!bMultiplayerMode || GameMode != GAME_IN_PROGRESS || raceFinished)
        return;

    const bool player1Wrecked = IsCarWreckedForInstance(0);
    const bool player2Wrecked = IsCarWreckedForInstance(1);
    if (player1Wrecked == player2Wrecked)
        return;

    raceFinished = true;
    // raceWon is "player 1 won".
    raceWon = player2Wrecked;
}

static void DrawGameplayCockpitHud(TextHelper& txtHelper, long lapValue, long opponentsDistance) {
    WCHAR lapText[3] = L"  ";
    if (lapValue > 0)
        StringCchPrintf(lapText, 3, L"%d", lapValue);

    float textScale = GetTextScale();
    float base_width = wideScreen ? static_cast<float>(BASE_WIDTH_WIDESCREEN) : static_cast<float>(BASE_WIDTH_STANDARD);
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    float projWidth = (vp[3] > 0) ? (480.0f * static_cast<float>(vp[2]) / static_cast<float>(vp[3])) : base_width;
    float cockpitOffsetX = (projWidth - base_width) * 0.5f;

    txtHelper.SetForegroundColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    // Boost text - positioned in top dashboard box
    txtHelper.SetInsertionPos(static_cast<int>((88 + (wideScreen ? 80 : 0)) * textScale + cockpitOffsetX),
                              static_cast<int>(BASE_HEIGHT - 48.0f));
    {
        std::wstringstream ss;
        ss << L"L" << lapText << L"       B" << std::setw(2) << std::setfill(L'0') << boostReserve;
        txtHelper.DrawFormattedTextLine(ss.str());
    }

    // Distance text - positioned in bottom dashboard box
    txtHelper.SetInsertionPos(static_cast<int>((84 + (wideScreen ? 80 : 0)) * textScale + cockpitOffsetX),
                              static_cast<int>(BASE_HEIGHT - 25.0f));
    {
        std::wstringstream ss;
        ss << L"        " << std::showpos << std::setw(5) << std::setfill(L'0') << opponentsDistance;
        txtHelper.DrawFormattedTextLine(ss.str());
    }
}

static void DrawGameplayCockpitHudForInstance(TextHelper& txtHelper, long carBehaviourInstanceIndex, long lapValue,
                                              long opponentsDistance) {
    const long previousInstance = PushCarBehaviourInstance(carBehaviourInstanceIndex);
    DrawGameplayCockpitHud(txtHelper, lapValue, opponentsDistance);
    PopCarBehaviourInstance(previousInstance);
}

static int GetCurrentTextProjectionWidth(void) {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[3] <= 0)
        return (vp[2] > 0) ? vp[2] : BASE_WIDTH_STANDARD;
    return static_cast<int>((480.0f * static_cast<float>(vp[2]) / static_cast<float>(vp[3])) + 0.5f);
}

static void DrawCenteredTextLine(TextHelper& txtHelper, const std::wstring& line, int y) {
    int centeredX = (GetCurrentTextProjectionWidth() - txtHelper.MeasureTextWidth(line.c_str())) / 2;
    if (centeredX < 0)
        centeredX = 0;
    txtHelper.SetInsertionPos(centeredX, y);
    txtHelper.DrawFormattedTextLine(line);
}

void RenderText(double fTime) {
    // The helper object simply helps keep track of text position, and color
    // and then it calls pFont->DrawText( m_pSprite, strMsg, -1, &rc, DT_NOCLIP, m_clr );
    // If NULL is passed in as the sprite object, then it will work fine however the
    // pFont->DrawText() will not be batched together.  Batching calls will improve perf.
    float textScale = GetTextScale();
    static TextHelper txtHelper(g_pFont, g_pSprite, 15);
    txtHelper.SetDisplaySize(static_cast<int>(15 * textScale));

    // Output statistics
    txtHelper.Begin();
    txtHelper.SetForegroundColor(glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
    if (bShowStats) {
        txtHelper.SetInsertionPos(static_cast<int>((2 + (wideScreen ? 10 : 0)) * textScale), 0);
        {
            std::wstringstream ss;
            ss << std::fixed << L"fTime: " << std::setprecision(1) << fTime << L"  sin(fTime): "
               << std::setprecision(4) << sin(fTime);
            txtHelper.DrawFormattedTextLine(ss.str());
        }
        {
            std::wstringstream ss;
            ss << std::fixed << L"Render FPS: " << std::setprecision(1) << g_renderFpsDisplay
               << L"  Body: " << std::setprecision(1) << g_physicsTickRateDisplay << L" Hz  Logic: "
               << std::setprecision(2) << g_baseLogicTickRateDisplay << L" Hz";
            txtHelper.DrawFormattedTextLine(ss.str());
        }
        {
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(0)
               << L"Ticks  Physics: " << g_physicsTickTotal << L"  Logic: " << g_baseLogicTickTotal;
            txtHelper.DrawFormattedTextLine(ss.str());
        }

#if defined(DEBUG) || defined(_DEBUG)
        // Output VALUE1, VALUE, VALUE3
        {
            std::wstringstream ss;
            ss << std::hex << std::setfill(L'0')
               << L"V1: " << std::setw(8) << VALUE1 << L", V2: " << std::setw(8) << VALUE2
               << L", V3: " << std::setw(8) << VALUE3;
            txtHelper.DrawFormattedTextLine(ss.str());
        }
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
        // Output opponent's name for four seconds at race start (single-player and faux multiplayer only).
        if (!bMultiplayerMode && ((GetTimeSeconds() - gameStartTime) < 4.0) && (opponentsID != NO_OPPONENT)) {
            std::wstring opponentName = opponentNames[opponentsID] ? opponentNames[opponentsID] : L"";
            while (!opponentName.empty() && opponentName.back() == L' ')
                opponentName.pop_back();

            std::wstringstream ss;
            ss << L"Opponent: " << opponentName;
            const std::wstring opponentLabel = ss.str();

            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            const float projectionWidth = (vp[3] > 0) ? (480.0f * static_cast<float>(vp[2]) / static_cast<float>(vp[3]))
                                                       : static_cast<float>(pd3dsdBackBuffer->Width);
            int centeredX = static_cast<int>((projectionWidth - txtHelper.MeasureTextWidth(opponentLabel.c_str())) * 0.5f);
            if (centeredX < 0)
                centeredX = 0;

            txtHelper.SetInsertionPos(centeredX, static_cast<int>(pd3dsdBackBuffer->Height - 15 * 20 * textScale));
            txtHelper.DrawFormattedTextLine(opponentLabel);
        }
        if (!IsSplitScreenMode())
            DrawGameplayCockpitHudForInstance(txtHelper, 0, lapNumber[PLAYER], CalculateOpponentsDistance());

        txtHelper.End();

        if ((GameMode == GAME_IN_PROGRESS) && bPaused && !raceFinished) {
            static TextHelper pausedTextHelper(g_pFontLarge, g_pSprite, 25);
            const int pausedDisplaySize = static_cast<int>(25 * textScale);
            const int centeredY = (BASE_HEIGHT - pausedDisplaySize) / 2;

            pausedTextHelper.SetDisplaySize(pausedDisplaySize);
            pausedTextHelper.Begin();
            pausedTextHelper.SetForegroundColor(glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
            DrawCenteredTextLine(pausedTextHelper, L"paused", centeredY);
            pausedTextHelper.End();
        }

        if (raceFinished) {
            double currentTime = GetTimeSeconds(), diffTime;
            if (gameEndTime == 0.0)
                gameEndTime = currentTime;

            // Show race finished text for six seconds, then end the game
            diffTime = currentTime - gameEndTime;
            if (diffTime > 6.0) {
                GameMode = GAME_OVER;
            }

            if (!IsSplitScreenMode()) {
#ifdef linux
                static
#endif
                    TextHelper txtHelperLarge(g_pFontLarge, g_pSprite, static_cast<int>(25 * textScale));

                txtHelperLarge.Begin();

                if (GameMode == GAME_OVER) {
#ifdef linux
                    DrawCenteredTextLine(txtHelperLarge, L"GAME OVER",
                                         static_cast<int>(pd3dsdBackBuffer->Height - 25 * 13 * textScale));
                    DrawCenteredTextLine(txtHelperLarge, L"Press 'M' or A for track menu",
                                         static_cast<int>(pd3dsdBackBuffer->Height - 25 * 11 * textScale));
#else
                    DrawCenteredTextLine(txtHelperLarge, L"GAME OVER: Press 'M' or A for track menu",
                                         static_cast<int>(pd3dsdBackBuffer->Height - 25 * 12 * textScale));
#endif
                } else {
                    long intTime = static_cast<long>(diffTime);
                    // Text flashes white/black, changing every half second
                    if ((diffTime - (double)intTime) < 0.5)
                        txtHelperLarge.SetForegroundColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
                    else
                        txtHelperLarge.SetForegroundColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

                    const std::wstring resultLabel = raceWon ? L"RACE WON" : L"RACE LOST";
                    const int resultDisplaySize = static_cast<int>(25 * textScale);
                    const int centeredY = (BASE_HEIGHT - resultDisplaySize) / 2;
                    DrawCenteredTextLine(txtHelperLarge, resultLabel, centeredY);
                }

                txtHelperLarge.End();
            }
        }
        break;
    }
    //    VALUE2 = raceFinished ? 1 : 0;
    //    VALUE3 = (long)gameEndTime;
}

//--------------------------------------------------------------------------------------
// Render the scene
//--------------------------------------------------------------------------------------

static void SetPerspectiveDepthRange(RenderDevice* pDevice, FLOAT nearPlane, FLOAT farPlane) {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    const FLOAT fAspect = (vp[3] > 0) ? (static_cast<FLOAT>(vp[2]) / static_cast<FLOAT>(vp[3])) : (4.0f / 3.0f);

    glm::mat4 matProj;
    mat4PerspectiveFov(&matProj, PI / 4, fAspect, nearPlane, farPlane);
    pDevice->SetTransform(TS_PROJECTION, &matProj);
}

static float ComputeRenderInterpolationAlpha(void) {
    if (g_physicsStepSeconds <= 0.0)
        return 0.0f;

    // When paused, gameplay state is intentionally not integrated; lock interpolation
    // to the latest state to avoid blending between stale prev/current snapshots.
    if (bPaused)
        return 1.0f;

    float alpha = static_cast<float>(g_logicAccumulator / g_physicsStepSeconds);
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    return alpha;
}

static void PrepareInterpolatedShadowsForView(long viewedCarInstance, float alpha) {
    RemoveShadowTriangles();

    if (GameMode == TRACK_PREVIEW) {
        UpdateInterpolatedOpponentShadow(alpha);
        return;
    }

    if (GameMode != GAME_IN_PROGRESS && GameMode != GAME_OVER)
        return;

    if (bMultiplayerMode) {
        if (viewedCarInstance != 0)
            UpdateInterpolatedPlayerShadowForInstance(0, alpha);
        if (viewedCarInstance != 1)
            UpdateInterpolatedPlayerShadowForInstance(1, alpha);
        return;
    }

    if (viewedCarInstance != 1)
        UpdateInterpolatedOpponentShadow(alpha);

    if (IsSplitScreenMode() && viewedCarInstance != 0)
        UpdateInterpolatedPlayerShadowForInstance(0, alpha);
}

static void RenderWorldGeometry(RenderDevice* pDevice, bool drawPlayer1Car, bool drawPlayer2Car) {
    // Draw Track
    pDevice->SetTransform(TS_WORLD, &matWorldTrack);
    DrawGroundPlane(pDevice);
    DrawBackdropScenery3D(pDevice);
    DrawTrack(pDevice);

    switch (GameMode) {
    case TRACK_MENU:
        break;

    case TRACK_PREVIEW:
        if (drawPlayer2Car) {
            // Draw player 2 / opponent car
            pDevice->SetTransform(TS_WORLD, &matWorldOpponentsCar);
            DrawCar(pDevice);
        }
        break;

    case GAME_IN_PROGRESS:
    case GAME_OVER:
        if (drawPlayer2Car) {
            pDevice->SetTransform(TS_WORLD, &matWorldOpponentsCar);
            DrawCar(pDevice);
        }
        if (drawPlayer1Car) {
            pDevice->SetTransform(TS_WORLD, &matWorldCar);
            DrawCar(pDevice);
        }
        break;
    }
}

static void SetGameplayViewTransform(RenderDevice* pDevice, long viewpoint_x, long viewpoint_y, long viewpoint_z,
                                     long viewpoint_x_angle, long viewpoint_y_angle, long viewpoint_z_angle) {
    glm::mat4 matRot, matTemp, matTrans, matView;

    const long viewX = viewpoint_x >> LOG_PRECISION;
    const long viewZ = viewpoint_z >> LOG_PRECISION;
    mat4Translation(&matTrans, static_cast<float>(-viewX), static_cast<float>(viewpoint_y >> LOG_PRECISION),
                    static_cast<float>(-viewZ));
    mat4Identity(&matRot);
    float xa = ((static_cast<float>(-viewpoint_x_angle) * 2 * PI) / 65536.0f);
    float ya = ((static_cast<float>(-viewpoint_y_angle) * 2 * PI) / 65536.0f);
    float za = ((static_cast<float>(-viewpoint_z_angle) * 2 * PI) / 65536.0f);
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
}

static void RenderGameplayViewport(RenderDevice* pDevice, int viewportX, int viewportY, int viewportW, int viewportH,
                                   long viewpoint_x, long viewpoint_y, long viewpoint_z, long viewpoint_x_angle,
                                   long viewpoint_y_angle, long viewpoint_z_angle, bool drawPlayer1Car,
                                   bool drawPlayer2Car, bool drawCockpitOverlay, long cockpitStateInstanceIndex) {
    const long previousInstance = PushCarBehaviourInstance(cockpitStateInstanceIndex);

    glViewport(viewportX, viewportY, viewportW, viewportH);
    V(pDevice->Clear(0, NULL, CLEAR_ZBUFFER, 0, 1.0f, 0));

    SetGameplayViewTransform(pDevice, viewpoint_x, viewpoint_y, viewpoint_z, viewpoint_x_angle, viewpoint_y_angle,
                             viewpoint_z_angle);
    SetPerspectiveDepthRange(pDevice, PERSPECTIVE_FAR_PASS_NEAR, PERSPECTIVE_FAR);
    DrawBackdropSkyDome3D(pDevice);
    RenderWorldGeometry(pDevice, drawPlayer1Car, drawPlayer2Car);

    V(pDevice->Clear(0, NULL, CLEAR_ZBUFFER, 0, 1.0f, 0));
    SetPerspectiveDepthRange(pDevice, PERSPECTIVE_NEAR, PERSPECTIVE_NEAR_PASS_FAR);
    RenderWorldGeometry(pDevice, drawPlayer1Car, drawPlayer2Car);

    if (drawCockpitOverlay)
        DrawCockpit(pDevice);

    PopCarBehaviourInstance(previousInstance);
}

void CALLBACK OnFrameRender(RenderDevice* pDevice, double fTime, float fElapsedTime, void* pUserContext) {
    HRESULT hr;

    //    // Clear the render target and the zbuffer
    //    V( pDevice->Clear(0, NULL, CLEAR_TARGET | CLEAR_ZBUFFER, COLOR_RGB(0, 45, 50, 170), 1.0f, 0) );

    // Clear the zbuffer
    V(pDevice->Clear(0, NULL, CLEAR_ZBUFFER, 0, 1.0f, 0));

    // Render the scene
    if (SUCCEEDED(pDevice->BeginScene())) {
        const float alpha = ComputeRenderInterpolationAlpha();

        if (IsSplitScreenMode() && (GameMode == GAME_IN_PROGRESS || GameMode == GAME_OVER)) {
            GLint fullVp[4];
            glGetIntegerv(GL_VIEWPORT, fullVp);
            int player1ViewportX = fullVp[0];
            int player1ViewportY = fullVp[1];
            int player1ViewportW = fullVp[2];
            int player1ViewportH = fullVp[3];
            int player2ViewportX = fullVp[0];
            int player2ViewportY = fullVp[1];
            int player2ViewportW = fullVp[2];
            int player2ViewportH = fullVp[3];
            if (g_splitScreenHorizontal) {
                const int lowerHeight = fullVp[3] / 2;
                const int upperHeight = fullVp[3] - lowerHeight;
                player1ViewportY = fullVp[1] + lowerHeight;
                player1ViewportH = upperHeight;
                player2ViewportY = fullVp[1];
                player2ViewportH = lowerHeight;
            } else {
                const int leftWidth = fullVp[2] / 2;
                const int rightWidth = fullVp[2] - leftWidth;
                player1ViewportX = fullVp[0];
                player1ViewportW = leftWidth;
                player2ViewportX = fullVp[0] + leftWidth;
                player2ViewportW = rightWidth;
            }
            const long opponentsDistanceFromPlayer1 =
                bMultiplayerMode ? CalculateTwoPlayerDistanceForHudFromPlayer1() : CalculateOpponentsDistance();
            float textScale = GetTextScale();
            static TextHelper splitHudTextHelper(g_pFont, g_pSprite, 15);
            splitHudTextHelper.SetDisplaySize(static_cast<int>(15 * textScale));
            splitHudTextHelper.Begin();

            const bool showSplitRaceResult = raceFinished;
            static TextHelper splitResultTextHelper(g_pFontLarge, g_pSprite, 25);
            const int splitResultDisplaySize = static_cast<int>(25 * textScale);
            const int splitResultY = (BASE_HEIGHT - splitResultDisplaySize) / 2;
            const bool player1WonRace = raceWon;
            const std::wstring player1ResultLabel = player1WonRace ? L"RACE WON" : L"RACE LOST";
            const std::wstring player2ResultLabel = player1WonRace ? L"RACE LOST" : L"RACE WON";
            glm::vec4 splitResultColor(0.0f, 0.0f, 0.0f, 1.0f);
            if (showSplitRaceResult) {
                if (GameMode != GAME_OVER) {
                    const double currentTime = GetTimeSeconds();
                    const double finishDisplayTime = (gameEndTime > 0.0) ? (currentTime - gameEndTime) : 0.0;
                    const long intTime = static_cast<long>(finishDisplayTime);
                    if ((finishDisplayTime - static_cast<double>(intTime)) < 0.5)
                        splitResultColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                }
                splitResultTextHelper.SetDisplaySize(splitResultDisplaySize);
                splitResultTextHelper.Begin();
            }

            const long p1x = static_cast<long>(LerpLong(prev_player1_x, player1_x, alpha));
            const long p1y = static_cast<long>(LerpLong(prev_player1_y, player1_y, alpha));
            const long p1z = static_cast<long>(LerpLong(prev_player1_z, player1_z, alpha));
            const long p1xa = (static_cast<long>(LerpWrappedAngleUnits(prev_player1_x_angle, player1_x_angle, alpha)) &
                               (MAX_ANGLE - 1));
            const long p1ya = (static_cast<long>(LerpWrappedAngleUnits(prev_player1_y_angle, player1_y_angle, alpha)) &
                               (MAX_ANGLE - 1));
            const long p1za = (static_cast<long>(LerpWrappedAngleUnits(prev_player1_z_angle, player1_z_angle, alpha)) &
                               (MAX_ANGLE - 1));

            const long p2x = static_cast<long>(LerpLong(prev_opponent_x, opponent_x, alpha));
            const long p2y = static_cast<long>(LerpLong(prev_opponent_y, opponent_y, alpha));
            const long p2z = static_cast<long>(LerpLong(prev_opponent_z, opponent_z, alpha));
            const long p2xa = RadiansToPlayerAngle(LerpWrappedRadians(prev_opponent_x_angle, opponent_x_angle, alpha));
            const long p2ya = RadiansToPlayerAngle(LerpWrappedRadians(prev_opponent_y_angle, opponent_y_angle, alpha));
            const long p2za = RadiansToPlayerAngle(LerpWrappedRadians(prev_opponent_z_angle, opponent_z_angle, alpha));

            long topViewX, topViewY, topViewZ, topViewXa, topViewYa, topViewZa;
            CalcGameViewpointForCar(p1x, p1y, p1z, p1xa, p1ya, p1za, &topViewX, &topViewY, &topViewZ, &topViewXa,
                                    &topViewYa, &topViewZa);

            long bottomViewX, bottomViewY, bottomViewZ, bottomViewXa, bottomViewYa, bottomViewZa;
            CalcGameViewpointForCar(p2x, p2y, p2z, p2xa, p2ya, p2za, &bottomViewX, &bottomViewY, &bottomViewZ,
                                    &bottomViewXa, &bottomViewYa, &bottomViewZa);

            // Draw backdrop once for the full frame; per-viewport passes only render 3D + overlays.
            pDevice->SetRenderState(RS_ZENABLE, FALSE);
            pDevice->SetRenderState(RS_CULLMODE, CULL_NONE);
            DrawBackdrop(render_backdrop_viewpoint_y, render_backdrop_viewpoint_x_angle, render_backdrop_viewpoint_y_angle,
                         render_backdrop_viewpoint_z_angle);

            // Player 1 viewport: always draw player 2; draw player 1 only when outside view.
            PrepareInterpolatedShadowsForView(0, alpha);
            RenderGameplayViewport(pDevice, player1ViewportX, player1ViewportY, player1ViewportW, player1ViewportH, topViewX,
                                   topViewY, topViewZ, topViewXa, topViewYa, topViewZa, bOutsideView, true,
                                   !bOutsideView, 0);
            DrawGameplayCockpitHudForInstance(splitHudTextHelper, 0, lapNumber[PLAYER], opponentsDistanceFromPlayer1);
            if (showSplitRaceResult) {
                splitResultTextHelper.SetForegroundColor(splitResultColor);
                DrawCenteredTextLine(splitResultTextHelper, player1ResultLabel, splitResultY);
            }

            // Player 2 viewport: always draw player 1; draw player 2 only when outside view.
            PrepareInterpolatedShadowsForView(1, alpha);
            RenderGameplayViewport(pDevice, player2ViewportX, player2ViewportY, player2ViewportW, player2ViewportH, bottomViewX, bottomViewY,
                                   bottomViewZ, bottomViewXa, bottomViewYa, bottomViewZa, true,
                                   bOutsideView, !bOutsideView, 1);
            DrawGameplayCockpitHudForInstance(splitHudTextHelper, 1, lapNumber[OPPONENT], -opponentsDistanceFromPlayer1);
            splitHudTextHelper.End();
            if (showSplitRaceResult) {
                splitResultTextHelper.SetForegroundColor(splitResultColor);
                DrawCenteredTextLine(splitResultTextHelper, player2ResultLabel, splitResultY);
                splitResultTextHelper.End();
            }

            // Restore full viewport for text rendering and subsequent frames.
            glViewport(fullVp[0], fullVp[1], fullVp[2], fullVp[3]);
            SetPerspectiveDepthRange(pDevice, PERSPECTIVE_NEAR, PERSPECTIVE_FAR);
        } else {
        // Disable Z buffer and polygon culling, ready for DrawBackdrop()
        pDevice->SetRenderState(RS_ZENABLE, FALSE);
        pDevice->SetRenderState(RS_CULLMODE, CULL_NONE);

        // Draw Backdrop
        DrawBackdrop(render_backdrop_viewpoint_y, render_backdrop_viewpoint_x_angle, render_backdrop_viewpoint_y_angle,
                     render_backdrop_viewpoint_z_angle);

        PrepareInterpolatedShadowsForView(0, alpha);

        // Render world geometry with split depth ranges for improved precision.
        SetPerspectiveDepthRange(pDevice, PERSPECTIVE_FAR_PASS_NEAR, PERSPECTIVE_FAR);
        DrawBackdropSkyDome3D(pDevice);
        RenderWorldGeometry(pDevice, bOutsideView, true);

        // Clear depth and redraw near range so close geometry wins cleanly.
        V(pDevice->Clear(0, NULL, CLEAR_ZBUFFER, 0, 1.0f, 0));
        SetPerspectiveDepthRange(pDevice, PERSPECTIVE_NEAR, PERSPECTIVE_NEAR_PASS_FAR);
        RenderWorldGeometry(pDevice, bOutsideView, true);
        }

        if ((GameMode == GAME_IN_PROGRESS) || (GameMode == GAME_OVER)) {
            if (!IsSplitScreenMode() && !bOutsideView) {
                // draw cockpit...
                DrawCockpit(pDevice);
            }
        }

        if (GameMode == GAME_IN_PROGRESS) {
            if (!IsSplitScreenMode())
                DrawOtherGraphics();

            //jsr    display.speed.bar
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

static int FindFirstConnectedGamepadSlot(void) {
    for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
        if (g_gamepadSlots[i].handle && SDL_GameControllerGetAttached(g_gamepadSlots[i].handle))
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
    if (bMultiplayerMode) {
        DWORD player1Input = 0;
        DWORD player2Input = 0;

        if (g_multiplayerPlayer1IsKeyboard) {
            player1Input = g_keyboardInput;
            for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
                if (!g_gamepadSlots[i].handle || !SDL_GameControllerGetAttached(g_gamepadSlots[i].handle))
                    continue;
                player2Input |= g_gamepadInput[i];
            }
        } else {
            int player1Slot = -1;
            if (g_multiplayerPlayer1InstanceId >= 0) {
                player1Slot = FindGamepadSlotByInstance(g_multiplayerPlayer1InstanceId);
                if ((player1Slot >= 0) &&
                    (!g_gamepadSlots[player1Slot].handle ||
                     !SDL_GameControllerGetAttached(g_gamepadSlots[player1Slot].handle)))
                    player1Slot = -1;
            }
            if (player1Slot < 0) {
                player1Slot = FindFirstConnectedGamepadSlot();
                g_multiplayerPlayer1InstanceId =
                    (player1Slot >= 0) ? g_gamepadSlots[player1Slot].instanceId : -1;
            }

            player2Input = g_keyboardInput;
            for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
                if (!g_gamepadSlots[i].handle || !SDL_GameControllerGetAttached(g_gamepadSlots[i].handle))
                    continue;
                if (i == player1Slot)
                    player1Input |= g_gamepadInput[i];
                else
                    player2Input |= g_gamepadInput[i];
            }

            // Fallback: if no controllers are attached, keep multiplayer playable from keyboard.
            if (player1Slot < 0) {
                player1Input = g_keyboardInput;
                player2Input = 0;
            }
        }

#ifdef __EMSCRIPTEN__
        if (g_webrtcGuestConnected) {
            /* Player 1 (top) = local input only; player 2 (bottom) = remote input only. No mixing.
             *
             * Gamepad note: the browser Gamepad API exposes the same physical gamepad to every tab
             * on the machine, so SDL will see the guest's gamepad even on the host page.
             * Guard: only use local gamepads when this tab is visible (Page Visibility API).
             * When the user switches to the guest tab the host tab becomes hidden, so we ignore
             * local gamepads and the guest's gamepad drives only player 2 via WebRTC.
             * We use visibilityState instead of document.hasFocus() because hasFocus() can
             * remain false when the tab is focused but the user is using a gamepad. */
            DWORD localOnly = g_keyboardInput;
            const int hostTabVisible = EM_ASM_INT({
                return (typeof document.visibilityState !== 'undefined' && document.visibilityState === 'visible') ? 1 : 0;
            });
            if (hostTabVisible) {
                for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i)
                    localOnly |= g_gamepadInput[i];
            }
            lastInput = localOnly;
            g_player2Input = g_remotePlayer2Input;
        } else {
            /* No WebRTC guest: starter device = player 1, rest = player 2. */
            lastInput = player1Input;
            g_player2Input = player2Input;
        }
#else
        lastInput = player1Input;
        g_player2Input = player2Input;
#endif
    } else {
        DWORD combinedInput = g_keyboardInput;
        for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i)
            combinedInput |= g_gamepadInput[i];
        lastInput = combinedInput;
        g_player2Input = 0;
    }
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
    if (g_pendingMultiplayerStarterInstanceId == instanceId) {
        g_pendingMultiplayerStarterIsKeyboard = false;
        g_pendingMultiplayerStarterInstanceId = -1;
    }
    if (g_multiplayerPlayer1InstanceId == instanceId) {
        g_multiplayerPlayer1IsKeyboard = false;
        g_multiplayerPlayer1InstanceId = -1;
    }
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
    g_pendingMultiplayerStarterIsKeyboard = false;
    g_pendingMultiplayerStarterInstanceId = -1;
    g_multiplayerPlayer1IsKeyboard = false;
    g_multiplayerPlayer1InstanceId = -1;
}
#else
static void RefreshCombinedInput(void) {
    if (bMultiplayerMode) {
        // No gamepad API in this build; keep multiplayer controllable from keyboard.
        lastInput = g_keyboardInput;
        g_player2Input = 0;
    } else {
        lastInput = g_keyboardInput;
        g_player2Input = 0;
    }
}
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
#ifdef USE_SDL2
            if ((GameMode == TRACK_PREVIEW) && bMultiplayerMode && (keyPress == SDLK_RETURN)) {
                g_pendingMultiplayerStarterIsKeyboard = true;
                g_pendingMultiplayerStarterInstanceId = -1;
            }
#endif
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

            case SDLK_F9:
                // Track preview now uses Left/Right to pick single vs multiplayer.
                if (GameMode != TRACK_PREVIEW) {
                    bMultiplayerMode = !bMultiplayerMode;
                    if (bMultiplayerMode) {
                        bFauxMultiplayerMode = FALSE;
                        opponentsID = NO_OPPONENT;
                    } else {
#ifdef USE_SDL2
                        g_pendingMultiplayerStarterIsKeyboard = false;
                        g_pendingMultiplayerStarterInstanceId = -1;
                        g_multiplayerPlayer1IsKeyboard = false;
                        g_multiplayerPlayer1InstanceId = -1;
#endif
                    }
                }
                break;

            case SDLK_F8:
                bFauxMultiplayerMode = !bFauxMultiplayerMode;
                if (bFauxMultiplayerMode) {
                    bMultiplayerMode = FALSE;
#ifdef USE_SDL2
                    g_pendingMultiplayerStarterIsKeyboard = false;
                    g_pendingMultiplayerStarterInstanceId = -1;
                    g_multiplayerPlayer1IsKeyboard = false;
                    g_multiplayerPlayer1InstanceId = -1;
#endif
                }
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

            case SDLK_w:
                // Debug/testing shortcut: force player 1 through the same wreck state as full damage.
                if (GameMode == GAME_IN_PROGRESS)
                    ForceCarWreckForInstance(0);
                break;

            // controls for Car Behaviour, Player 1 (Left/Right = track change when in track menu)
            case SDLK_LEFT:
                if (GameMode != TRACK_MENU)
                    g_keyboardInput |= KEY_P1_LEFT;
                break;

            case SDLK_RIGHT:
                if (GameMode != TRACK_MENU)
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
                if (GameMode == TRACK_MENU)
                    return false; /* quit only on main menu */
                if (GameMode == TRACK_PREVIEW || GameMode == GAME_OVER || GameMode == GAME_IN_PROGRESS) {
                    GameMode = TRACK_MENU;
                    g_restartEngineAudioOnFirstInput = false;
                    opponentsID = NO_OPPONENT;
                    ResetDrawBridge();
                }
                break;
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
        case SDL_CONTROLLERBUTTONDOWN: {
            const Uint8 btn = event.cbutton.button;
            const SDL_JoystickID controllerInstanceId = event.cbutton.which;
            const bool inMenu = (GameMode == TRACK_MENU || GameMode == TRACK_PREVIEW || GameMode == GAME_OVER);
            if (btn == SDL_CONTROLLER_BUTTON_START && GameMode == GAME_IN_PROGRESS) {
                bPaused = !bPaused;
                break;
            }
            /* Select (Back) only = back to menu during race; B is brake and must not exit */
            if (btn == SDL_CONTROLLER_BUTTON_BACK && GameMode == GAME_IN_PROGRESS) {
                GameMode = TRACK_MENU;
                g_restartEngineAudioOnFirstInput = false;
                opponentsID = NO_OPPONENT;
                ResetDrawBridge();
                break;
            }
            if (inMenu) {
                if (btn == SDL_CONTROLLER_BUTTON_A) {
                    if ((GameMode == TRACK_PREVIEW) && bMultiplayerMode) {
                        g_pendingMultiplayerStarterIsKeyboard = false;
                        g_pendingMultiplayerStarterInstanceId = controllerInstanceId;
                    }
                    /* A = Confirm / Next (Xbox standard) */
                    if (GameMode == GAME_OVER) {
                        GameMode = TRACK_MENU;
                        g_restartEngineAudioOnFirstInput = false;
                        opponentsID = NO_OPPONENT;
                        ResetDrawBridge();
                    } else
                        keyPress = SDLK_RETURN;
                } else if (btn == SDL_CONTROLLER_BUTTON_B || btn == SDL_CONTROLLER_BUTTON_BACK) {
                    /* B or Select (Back) = back to menu from preview/game over; never quit */
                    if (GameMode == TRACK_PREVIEW || GameMode == GAME_OVER) {
                        GameMode = TRACK_MENU;
                        g_restartEngineAudioOnFirstInput = false;
                        opponentsID = NO_OPPONENT;
                        ResetDrawBridge();
                    }
                } else if (((GameMode == TRACK_MENU) || (GameMode == TRACK_PREVIEW)) &&
                           (btn == SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
                    keyPress = SDLK_LEFT;
                } else if (((GameMode == TRACK_MENU) || (GameMode == TRACK_PREVIEW)) &&
                           (btn == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
                    keyPress = SDLK_RIGHT;
                }
            }
            break;
        }
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

    /* Use the whole rendering area instead of letterboxing */
    int viewportW = windowWidth;
    int viewportH = windowHeight;
    int viewportX = 0;
    int viewportY = 0;

    glViewport(viewportX, viewportY, viewportW, viewportH);

    SetPerspectiveDepthRange(&pDevice, PERSPECTIVE_NEAR, PERSPECTIVE_FAR);

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

#ifdef __EMSCRIPTEN__
    if (g_webrtcReturnToMenuRequested) {
        g_webrtcReturnToMenuRequested = false;
        GameMode = TRACK_MENU;
        g_restartEngineAudioOnFirstInput = false;
        opponentsID = NO_OPPONENT;
        ResetDrawBridge();
    }
#endif

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
    g_logicTickAccumulator += frameDelta;
    /* Clamp accumulators to avoid spiral of death. */
    const double maxAccumulator = MAX_PHYSICS_STEPS_PER_FRAME * g_physicsStepSeconds;
    if (g_logicAccumulator > maxAccumulator)
        g_logicAccumulator = maxAccumulator;
    if (g_logicTickAccumulator > 2.0 * g_logicTickInterval)
        g_logicTickAccumulator = g_logicTickInterval;

    /* Reset interpolation when menu/game mode changes or delta is large so we don't lerp from stale state. */
    {
        static GameModeType s_prevGameMode = TRACK_MENU;
        const double interpolationResetDelta = 2.0 * g_physicsStepSeconds;  // e.g. ~2 physics steps
        if (GameMode != s_prevGameMode || frameDelta > interpolationResetDelta) {
            have_prev_car_state = false;
            s_prevGameMode = GameMode;
        }
    }

    // Split-screen audio: two modes.
    // - WebRTC connected: full L/R pan only; JS ChannelSplitter sends left (P1) to host speakers
    //   and right (P2) to a separate audio track for the guest. No volume reduction or detune.
    // - Local split-screen only: mild ±4000 pan, reduced volume (48/3), and P2 detune (0.98f)
    //   so both cars mix nicely on one device.
    {
        static bool s_prevSplitScreen = false;
#ifdef __EMSCRIPTEN__
        const bool webrtcActive = g_webrtcGuestConnected;
#else
        const bool webrtcActive = false;
#endif
        static bool s_prevWebRTC = false;
        const bool splitScreenGameplay = IsSplitScreenMode() && (GameMode == GAME_IN_PROGRESS);
        if (splitScreenGameplay != s_prevSplitScreen || webrtcActive != s_prevWebRTC) {
            if (splitScreenGameplay) {
                const long p1Pan = webrtcActive ? DSBPAN_LEFT : -4000;
                const long p2Pan = webrtcActive ? DSBPAN_RIGHT : 4000;
                const long vol = webrtcActive ? AmigaVolumeToMixerGain(48 / 2) : AmigaVolumeToMixerGain(48 / 3);
                for (int i = 0; i < 8; ++i) {
                    if (EngineSoundBuffers[i])  { EngineSoundBuffers[i]->SetPan(p1Pan);  EngineSoundBuffers[i]->SetVolume(vol); }
                    if (EngineSoundBuffers2[i]) { EngineSoundBuffers2[i]->SetPan(p2Pan); EngineSoundBuffers2[i]->SetVolume(vol); }
                }
                // In WebRTC mode, default shared non-engine SFX to P1 (left). Individual
                // callsites can override pan at play time for per-instance routing.
                const long p1SfxPan = webrtcActive ? DSBPAN_LEFT : DSBPAN_RIGHT;
                const long smashPan  = webrtcActive ? DSBPAN_LEFT : DSBPAN_LEFT;
                if (WreckSoundBuffer)    WreckSoundBuffer->SetPan(p1SfxPan);
                if (GroundedSoundBuffer) GroundedSoundBuffer->SetPan(p1SfxPan);
                if (CreakSoundBuffer)    CreakSoundBuffer->SetPan(p1SfxPan);
                if (SmashSoundBuffer)    SmashSoundBuffer->SetPan(smashPan);
                if (OffRoadSoundBuffer)  OffRoadSoundBuffer->SetPan(p1SfxPan);
                if (HitCarSoundBuffer)   HitCarSoundBuffer->SetPan(p1SfxPan);
            } else {
                for (int i = 0; i < 8; ++i) {
                    if (EngineSoundBuffers[i])  { EngineSoundBuffers[i]->SetPan(DSBPAN_CENTER);  EngineSoundBuffers[i]->SetVolume(AmigaVolumeToMixerGain(48 / 2)); }
                    if (EngineSoundBuffers2[i]) { EngineSoundBuffers2[i]->SetPan(DSBPAN_CENTER); EngineSoundBuffers2[i]->SetVolume(AmigaVolumeToMixerGain(48 / 2)); }
                }
                if (WreckSoundBuffer)    WreckSoundBuffer->SetPan(DSBPAN_RIGHT);
                if (GroundedSoundBuffer) GroundedSoundBuffer->SetPan(DSBPAN_RIGHT);
                if (CreakSoundBuffer)    CreakSoundBuffer->SetPan(DSBPAN_RIGHT);
                if (SmashSoundBuffer)    SmashSoundBuffer->SetPan(DSBPAN_LEFT);
                if (OffRoadSoundBuffer)  OffRoadSoundBuffer->SetPan(DSBPAN_RIGHT);
                if (HitCarSoundBuffer)   HitCarSoundBuffer->SetPan(DSBPAN_RIGHT);
            }
            s_prevSplitScreen = splitScreenGameplay;
            s_prevWebRTC = webrtcActive;
        }
    }

    bool anyLogicFrameMoved = false;
    int stepsThisFrame = 0;
    while (g_logicAccumulator >= g_physicsStepSeconds && stepsThisFrame < MAX_PHYSICS_STEPS_PER_FRAME) {
        g_logicAccumulator -= g_physicsStepSeconds;
        ++stepsThisFrame;
        ++g_physicsTicksInWindow;
        ++g_physicsTickTotal;

        // --- Audio (once per physics step) ---
        if ((GameMode == GAME_IN_PROGRESS) && (!bPaused)) {
            // lastInput is player-1 driving input (gamepad in multiplayer mode).
            const DWORD drivingInputMask = KEY_P1_LEFT | KEY_P1_RIGHT | KEY_P1_ACCEL | KEY_P1_BRAKE | KEY_P1_BOOST;
            const bool splitScreen = IsSplitScreenMode();
            if (g_restartEngineAudioOnFirstInput) {
                // Keep engine audio fully silent until the first gameplay input (keyboard or gamepad),
                // but continue advancing engine state so revs are prewarmed.
                if (splitScreen) {
                    const long prev0 = PushCarBehaviourInstance(0);
                    StepEngineAudioStateSubstep(g_physicsSubstepsPerBaseLogic);
                    PopCarBehaviourInstance(prev0);
                    const long prev1 = PushCarBehaviourInstance(1);
                    StepEngineAudioStateSubstep(g_physicsSubstepsPerBaseLogic);
                    PopCarBehaviourInstance(prev1);
                } else {
                    StepEngineAudioStateSubstep(g_physicsSubstepsPerBaseLogic);
                }
                if ((lastInput & drivingInputMask) != 0) {
                    RestartEngineAudioBuffers(false);
                    if (splitScreen) {
                        const long prev0 = PushCarBehaviourInstance(0);
                        PrimeEngineAudioForGameplayStart();
                        PopCarBehaviourInstance(prev0);
                        const long prev1 = PushCarBehaviourInstance(1);
                        PrimeEngineAudioForGameplayStart();
                        PopCarBehaviourInstance(prev1);
                    } else {
                        PrimeEngineAudioForGameplayStart();
                    }
                    g_restartEngineAudioOnFirstInput = false;
                    FramesWheelsEngineSubstep(EngineSoundBuffers, g_physicsSubstepsPerBaseLogic);
                    if (splitScreen) {
                        const long prev1 = PushCarBehaviourInstance(1);
#ifdef __EMSCRIPTEN__
                        const float p2Pitch = g_webrtcGuestConnected ? 1.0f : 0.98f;
#else
                        const float p2Pitch = 0.98f;
#endif
                        FramesWheelsEngineSubstep(EngineSoundBuffers2, g_physicsSubstepsPerBaseLogic, p2Pitch);
                        PopCarBehaviourInstance(prev1);
                    }
                } else if (engineSoundPlaying) {
                    RestartEngineAudioBuffers(false);
                }
            } else {
                FramesWheelsEngineSubstep(EngineSoundBuffers, g_physicsSubstepsPerBaseLogic);
                if (splitScreen) {
                    const long prev1 = PushCarBehaviourInstance(1);
#ifdef __EMSCRIPTEN__
                    const float p2Pitch = g_webrtcGuestConnected ? 1.0f : 0.98f;
#else
                    const float p2Pitch = 0.98f;
#endif
                    FramesWheelsEngineSubstep(EngineSoundBuffers2, g_physicsSubstepsPerBaseLogic, p2Pitch);
                    PopCarBehaviourInstance(prev1);
                }
            }
        }

        // --- Input sampling (once per physics step) ---
        SampleControlsForLogicSubstep(lastInput);
        QueueMultiplayerCarCollisionImpulsesForStep();

        // --- Body-dynamics integrator (once per physics step, decoupled from game logic) ---
        if ((GameMode == TRACK_MENU) || (GameMode == TRACK_PREVIEW) || (GameMode == GAME_IN_PROGRESS)) {
            if (GameMode == TRACK_MENU) {
                CapturePreviousCarState();
                if (!bPaused) {
                    const float menuAnglePerStep =
                        128.0f * static_cast<float>(g_physicsStepSeconds / g_logicTickInterval);
                    CalcTrackMenuViewpoint(menuAnglePerStep);
                    viewpoint1_x >>= LOG_PRECISION;
                    viewpoint1_z >>= LOG_PRECISION;
                    target_x >>= LOG_PRECISION;
                    target_y = -target_y;
                    target_y >>= LOG_PRECISION;
                    target_z >>= LOG_PRECISION;
                }
            } else {
                if (!bPaused) {
                    CapturePreviousCarState();
                    if ((GameMode == GAME_IN_PROGRESS) && (!bPlayerPaused))
                        CarBehaviour(lastInput, &player1_x, &player1_y, &player1_z, &player1_x_angle,
                                     &player1_y_angle, &player1_z_angle, (float)g_physicsStepSeconds);
                    const bool useMultiplayerCarBehaviourForOpponent =
                        bMultiplayerMode && (GameMode == GAME_IN_PROGRESS);
                    if (useMultiplayerCarBehaviourForOpponent) {
                        if (!bOpponentPaused || bNewGame) {
                            long opponent_x_angle_units = RadiansToPlayerAngle(opponent_x_angle);
                            long opponent_y_angle_units = RadiansToPlayerAngle(opponent_y_angle);
                            long opponent_z_angle_units = RadiansToPlayerAngle(opponent_z_angle);
                            const DWORD player2Input = (GameMode == GAME_IN_PROGRESS) ? g_player2Input : 0;
                            CarBehaviourForInstance(1, player2Input, &opponent_x, &opponent_y, &opponent_z,
                                                    &opponent_x_angle_units, &opponent_y_angle_units,
                                                    &opponent_z_angle_units, (float)g_physicsStepSeconds);
                            opponent_x_angle = PlayerAngleToRadians(opponent_x_angle_units);
                            opponent_y_angle = PlayerAngleToRadians(opponent_y_angle_units);
                            opponent_z_angle = PlayerAngleToRadians(opponent_z_angle_units);
                        }
                        if (bNewGame)
                            bNewGame = FALSE;
                    } else {
                        OpponentBehaviour(&opponent_x, &opponent_y, &opponent_z, &opponent_x_angle,
                                          &opponent_y_angle, &opponent_z_angle, bOpponentPaused,
                                          (float)g_physicsStepSeconds);
                        if (bFauxMultiplayerMode) {
                            long opponentPiece = 0, opponentDistanceIntoSection = 0;
                            GetOpponentRoadState(&opponentPiece, &opponentDistanceIntoSection);
                            SetCarRoadStateForInstance(1, opponentPiece, opponentDistanceIntoSection);
                        }
                    }
                    if (GameMode == GAME_IN_PROGRESS) {
                        UpdatePlayerShadowForInstance(0);
                        if (bMultiplayerMode)
                            UpdatePlayerShadowForInstance(1);
                    }
                    UpdateProjectedRenderPositions();
                }
                if (GameMode == GAME_IN_PROGRESS) {
                    // Snap the car back above the road surface before computing the camera.
                    LimitViewpointY(&player1_y);
                    if (bMultiplayerMode)
                        LimitViewpointYForInstance(1, &opponent_y);
                    if (!bPaused) {
                        CalcGameViewpoint();
                        viewpoint1_x >>= LOG_PRECISION;
                        viewpoint1_z >>= LOG_PRECISION;
                    }
                } else if (GameMode == TRACK_PREVIEW && !bPaused) {
                    // Update preview camera every physics step so prev/current stay in sync;
                    // otherwise we interpolate from TRACK_MENU (or stale) viewpoint and the camera goes crazy.
                    CalcTrackPreviewViewpoint();
                    viewpoint1_x >>= LOG_PRECISION;
                    viewpoint1_z >>= LOG_PRECISION;
                    target_x >>= LOG_PRECISION;
                    target_y = -target_y;
                    target_y >>= LOG_PRECISION;
                    target_z >>= LOG_PRECISION;
                }
            }
        }
    }

    // --- Game-logic tick at fixed PHYSICS_REFERENCE_STEP_SECONDS (real time) ---
    while (g_logicTickAccumulator >= g_logicTickInterval) {
        g_logicTickAccumulator -= g_logicTickInterval;
        g_logicInput = BuildLogicInputFromSamples(lastInput);
        AdvanceBoostReserve(g_logicInput);  // drain boost once per logic tick (was 50x/sec in BoostPower)
        ResetControlSamplingWindow();
        OnFrameMove(&pDevice, frameTime, static_cast<float>(g_logicTickInterval), NULL);
        if ((GameMode == GAME_IN_PROGRESS) && bFrameMoved) {
            UpdateDamageForActiveCars();
            UpdateMultiplayerRaceFinishFromWrecks();
        }
        BeginLogicTickDamagePeriodForActiveCars(); // allow damage to be applied again (once per wheel per logic tick)
        ++g_baseLogicTicksInWindow;
        ++g_baseLogicTickTotal;
        if (bFrameMoved)
            anyLogicFrameMoved = true;
    }
    bFrameMoved = anyLogicFrameMoved;

    // If no physics step ran this frame (e.g. first frame), set TRACK_MENU viewpoint once so it's valid.
    if (GameMode == TRACK_MENU && stepsThisFrame == 0) {
        CalcTrackMenuViewpoint(0.0f);
        viewpoint1_x >>= LOG_PRECISION;
        viewpoint1_z >>= LOG_PRECISION;
        target_x >>= LOG_PRECISION;
        target_y = -target_y;
        target_y >>= LOG_PRECISION;
        target_z >>= LOG_PRECISION;
    }

    if ((GameMode == TRACK_MENU) || (GameMode == TRACK_PREVIEW) || (GameMode == GAME_IN_PROGRESS)) {
        // Alpha is the fractional time remaining in the current physics step.
        // Since CapturePreviousCarState() is called once per physics step, this
        // correctly interpolates between the last two integration states.
        const float alpha = ComputeRenderInterpolationAlpha();
        UpdateInterpolatedCarTransforms(&pDevice, alpha);
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
void em_main_loop() {
    /* Poll canvas/window size when JS resize() has updated the canvas dimensions */
    if (window) {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        static int s_lastW = 0, s_lastH = 0;
        if ((s_lastW != w || s_lastH != h) && w > 0 && h > 0) {
            s_lastW = w;
            s_lastH = h;
            ApplyWindowLayout(w, h, false);
        }
    }
    RunFrame(GetTimeSeconds(), false);
}
#endif

int GL_MSAA = 0;
int main(int argc, const char** argv) {
    char maintitle[50] = {0};
    sprintf(maintitle, "MultiStuntCar v%d.%02d.%02d", V_MAJOR, V_MINOR, V_PATCH);
    printf("%s\n", maintitle);
    // Run from the executable directory so relative assets (data/Bitmap, data/Sounds, data/Tracks) resolve.
    char* basePath = SDL_GetBasePath();
    if (basePath && basePath[0] != '\0') {
        if (chdir(basePath) == 0)
            printf("chdir(\"%s\")\n", basePath);
        SDL_free(basePath);
    }
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "0");
#endif
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
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef USE_SDL2
#if defined(__EMSCRIPTEN__) || defined(HAVE_GLES)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
#endif

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
            int defaultW = 1920;
            int defaultH = 1080;
#if defined(NDEBUG)
#ifdef USE_SDL2
            SDL_DisplayMode desktopMode;
            if (SDL_GetDesktopDisplayMode(0, &desktopMode) == 0 && desktopMode.w > 0 && desktopMode.h > 0) {
                defaultW = desktopMode.w;
                defaultH = desktopMode.h;
            }
#else
            const SDL_VideoInfo* infos = SDL_GetVideoInfo();
            if (infos && infos->current_w > 0 && infos->current_h > 0) {
                defaultW = infos->current_w;
                defaultH = infos->current_h;
            }
#endif
#endif
            screenW = customWidth > 0 ? customWidth : defaultW;
            screenH = customHeight > 0 ? customHeight : defaultH;
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
#ifdef __EMSCRIPTEN__
    /* Ask the shell to resize the canvas to the container so the first frame fits the page */
    emscripten_run_script("if (typeof window.triggerResize === 'function') window.triggerResize();");
#endif
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glEnable(GL_DEPTH_TEST);
    // Disable texture mapping by default (only DrawTrack() enables it)
    pDevice.SetTextureStageState(0, TSS_COLOROP, TOP_DISABLE);

    if (IsAudioEnabled())
        sound_init();

    CreateFonts();
    LoadTextures();

    if (!InitialiseData()) {
        printf("Error initialising data\n");
        exit(-3);
    }

    CreateBuffers(&pDevice);

    if (IsAudioEnabled()) {
        DSInit();
        DSSetMode();
    }

    /* Clear to sky colour so any unfilled pixels (e.g. right edge) match the backdrop */
    {
        const DWORD sky = SCRGB(SCR_BASE_COLOUR + 7); /* SKY_COLOUR */
        glClearColor(((sky >> 0) & 0xff) / 255.0f, ((sky >> 8) & 0xff) / 255.0f,
                     ((sky >> 16) & 0xff) / 255.0f, 1.0f);
    }
    RefreshCombinedInput();
    ResetFourteenFrameTiming();
#ifdef __EMSCRIPTEN__
    UpdateProjectedRenderPositions();
    CapturePreviousCarState();
    g_lastFrameTime = GetTimeSeconds();
    g_logicAccumulator = g_physicsStepSeconds;
    g_logicTickAccumulator = g_logicTickInterval;
    g_logicInput = lastInput;
    ResetControlSamplingWindow();
    g_timingWindowStart = g_lastFrameTime;
    emscripten_set_main_loop(em_main_loop, 0, 1);
#else
    bool run = true;
    UpdateProjectedRenderPositions();
    CapturePreviousCarState();
    g_lastFrameTime = GetTimeSeconds();
    g_logicAccumulator = g_physicsStepSeconds;
    g_logicTickAccumulator = g_logicTickInterval;
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
    if (IsAudioEnabled())
        sound_destroy();
    TTF_Quit();
    SDL_Quit();

    exit(0);
}
