#ifndef _PHYSICS_CONFIG
#define _PHYSICS_CONFIG

/**
 * Central tuning constants for vehicle suspension and car contact physics.
 *
 * The physics engine uses fixed-point integers, so parameters are integer
 * multipliers rather than physical Hz / ratios. The comments show what each
 * constant actually controls and which direction to move it.
 *
 * All values here are wired directly into the physics code; change them and
 * rebuild to feel the effect.
 */

/* ============================================================================
 * Front suspension  (front-left and front-right wheels)
 *
 * The wheel-collision response for one tick is:
 *   force = (velocity_term * SPRING >> 8) + penetration_depth
 * where velocity_term = (current_penetration - last_penetration).
 *
 * SPRING  controls the damping / velocity feedback gain.
 *           Raise to make the front end return to road height faster / stiffer.
 *           Lower for a softer, floatier front.
 *           256 = neutral (no velocity boost); original shared value = 276.
 *
 * DAMPING controls how much of the spring force is fed into the final
 *           response on top of the raw penetration depth.
 *           Raise to increase stiffness; lower to soften.
 *           256 = scale factor of 1.0 (pass through unchanged).
 * ============================================================================ */
#define FRONT_SUSPENSION_SPRING   276   /* velocity gain (276/256 ≈ 1.08)  */
#define FRONT_SUSPENSION_DAMPING  256   /* penetration scale (256/256 = 1.0) */

/* ============================================================================
 * Rear suspension  (single rear wheel)
 *
 * Same formula as front; independent values let you tune rear separately.
 * ============================================================================ */
#define REAR_SUSPENSION_SPRING    276   /* velocity gain (276/256 ≈ 1.08)  */
#define REAR_SUSPENSION_DAMPING   256   /* penetration scale (256/256 = 1.0) */

/* ============================================================================
 * Car-to-car (wall) contact
 *
 * WALL_CONTACT_IMPULSE  - lateral (X-axis) impulse applied to player when
 *                         the two cars are in contact side-by-side.
 *                         Original hardcoded value = 0x800 (2048).
 *                         Raise for a harder shove; lower for a softer nudge.
 *
 * WALL_CONTACT_DAMPING  - right-shift applied to the vertical (Y) impulse
 *                         transferred between cars on contact.
 *                         Original = 4  (i.e. impulse >> 4, so ~6% transfer).
 *                         Lower shift = more vertical bounce on contact;
 *                         higher shift = less.
 * ============================================================================ */
#define WALL_CONTACT_IMPULSE      0x800  /* lateral shove on car-to-car contact */
#define WALL_CONTACT_DAMPING      4      /* right-shift for vertical impulse     */

#endif /* _PHYSICS_CONFIG */
