#ifndef _PHYSICS_CONFIG
#define _PHYSICS_CONFIG

/**
 * Central tuning constants for vehicle suspension and car contact physics.
 *
 * PHYSICS_UPDATE_HZ controls how many times per second the body-dynamics
 * integrator (CarBehaviour / OpponentBehaviour) is stepped.  This is
 * independent of the game-logic tick rate (~7.14 Hz).
 * Default: 60 Hz  ->  ~16.67 ms integration step.  Change this define and rebuild.
 *
 * The physics engine uses fixed-point integers, so parameters are integer
 * multipliers rather than physical Hz / ratios. The comments show what each
 * constant actually controls and which direction to move it.
 *
 * All values here are wired directly into the physics code; change them and
 * rebuild to feel the effect.
 */

#ifndef PHYSICS_UPDATE_HZ
#define PHYSICS_UPDATE_HZ 60
#endif

/** Reference timestep (seconds) the integration was tuned for (original ~7.14 Hz logic tick).
 *  Integrator deltas are scaled by (actual_step / PHYSICS_REFERENCE_STEP_SECONDS). */
#define PHYSICS_REFERENCE_STEP_SECONDS 0.14

/* ============================================================================
 * Front suspension  (front-left and front-right wheels)
 *
 * The wheel-collision response for one tick is:
 *   force = (velocity_term * SPRING >> 8) + penetration_depth
 * where velocity_term = (current_penetration - last_penetration).
 *
 * SPRING  is in reference-tick units (tuned for PHYSICS_REFERENCE_STEP_SECONDS).
 *           Effective per-step spring is computed as spring * (dt_ref/dt) in code.
 *           Raise to make the front end return to road height faster / stiffer.
 *           256 = neutral; original shared value = 276.
 *
 * DAMPING controls how much of the spring force is fed into the final
 *           response on top of the raw penetration depth.
 *           Raise to increase stiffness; lower to soften.
 *           256 = scale factor of 1.0 (pass through unchanged).
 * ============================================================================ */
#define FRONT_SUSPENSION_SPRING   320   /* velocity gain (reference-tick units, scaled by dt_ref/dt) */
#define FRONT_SUSPENSION_DAMPING  200   /* penetration scale (256/256 = 1.0) */

/* ============================================================================
 * Rear suspension  (single rear wheel)
 *
 * Same formula as front; independent values let you tune rear separately.
 * ============================================================================ */
#define REAR_SUSPENSION_SPRING    320   /* velocity gain (reference-tick units, scaled by dt_ref/dt) */
#define REAR_SUSPENSION_DAMPING   200   /* penetration scale (256/256 = 1.0) */

/* ============================================================================
 * Car-to-car (wall) contact
 *
 * WALL_CONTACT_IMPULSE  - lateral (X-axis) impulse applied to player when
 *                         the two cars are in contact side-by-side.
 *                         Original Amiga code uses +/-8 here.
 *                         Raise for a harder shove; lower for a softer nudge.
 *
 * WALL_CONTACT_DAMPING  - right-shift applied to the vertical (Y) impulse
 *                         transferred between cars on contact (e.g. 4 => impulse >> 4).
 *                         Lower shift = more vertical bounce on contact;
 *                         higher shift = less.
 * ============================================================================ */
#define WALL_CONTACT_IMPULSE      8     /* lateral shove on car-to-car contact */
#define WALL_CONTACT_DAMPING      4     /* right-shift for vertical impulse     */

#endif /* _PHYSICS_CONFIG */
