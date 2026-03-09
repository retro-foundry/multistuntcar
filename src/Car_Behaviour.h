
#ifndef _CAR_BEHAVIOUR
#define _CAR_BEHAVIOUR

/*    ========= */
/*    Constants */
/*    ========= */
#define CAR_WIDTH 64
#define CAR_LENGTH 128

// Wheel rotation speed constants
#define WHEEL_SPEED_LOW_THRESHOLD 0x800 // Threshold for low speed wheel calculation
#define WHEEL_SPEED_HIGH_OFFSET 0x3000  // Added to high speed wheel calculation
#define WHEEL_SPEED_MAX 0xffff          // Maximum wheel rotation speed
#define WHEEL_SPEED_MAX_CLAMPED 0xff00  // Clamped maximum wheel speed
#define WHEEL_ANGLE_MASK 0xfffff        // Mask for wheel angle wrapping

// new new controls for Car Behaviour, Player 1
// must not clash with other KEY definitions
#define KEY_P1_LEFT 0x00000001l
#define KEY_P1_RIGHT 0x00000002l
#define KEY_P1_ACCEL 0x00000004l
#define KEY_P1_BRAKE 0x00000008l
#define KEY_P1_BOOST 0x00000010l

#define AMIGA_PAL_HZ (3546895)

#define REDUCTION 238 // (238/256)
#define INCREASE 276  // (276/256) - kept for opponent physics; player uses PhysicsConfig.h

#include "PhysicsConfig.h"

typedef enum { OPPONENT = 0, PLAYER, NUM_CARS } CarType;

/*    ===================== */
/*    Structure definitions */
/*    ===================== */

/*    ============================== */
/*    External function declarations */
/*    ============================== */
extern void ResetPlayer(void);

extern void CarBehaviour(DWORD input, long* x, long* y, long* z, long* x_angle, long* y_angle, long* z_angle);

extern void LimitViewpointY(long* y);

extern long AmigaVolumeToMixerGain(long amiga_volume);

extern long CalculateDisplaySpeed(void);

extern void FramesWheelsEngine(IDirectSoundBuffer8* engineSoundBuffers[]);
extern void FramesWheelsEngineSubstep(IDirectSoundBuffer8* engineSoundBuffers[], int substeps_per_logic);
extern void StepEngineAudioStateSubstep(int substeps_per_logic);
extern void EngineSoundStopped(void);
extern void ResetEngineAudioState(void);
extern void PrimeEngineAudioForGameplayStart(void);

extern void CalculatePlayersRoadPosition(void);

extern void DrawOtherGraphics(void);
extern void UpdateDamage(void);
extern void ResetFourteenFrameTiming(void);
extern void AdvanceFourteenFrameTiming(void);

extern void ResetLapData(long car);
extern void UpdateLapData(void);

#ifdef USE_AMIGA_RECORDING
// Following only used for testing against Amiga
extern bool GetRecordedAmigaWord(long* value_out);
extern bool GetRecordedAmigaLong(long* value_out);
extern void CompareAmigaWord(char* name, long amiga_value, long* value);
extern void CompareRecordedAmigaWord(char* name, long* value);
extern void CloseAmigaRecording(void);
#endif

#endif /* _CAR_BEHAVIOUR */
