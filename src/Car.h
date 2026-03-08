
#ifndef	_CAR
#define	_CAR

#include "dx_linux.h"

/*	========= */
/*	Constants */
/*	========= */
// VCAR is short for VISIBLE_CAR
#define	VCAR_WIDTH	162		// ((width 27+27 * segment width 384) / surface factor 256) * PC_FACTOR
#define	VCAR_LENGTH	256		// ((length 128 * segment length 256) / surface factor 256) * PC_FACTOR
#define	VCAR_HEIGHT	162		// chosen to look ok with the above

// Cockpit rendering constants (320x200 base space)
#define COCKPIT_WIDESCREEN_OFFSET   40.0f   // Additional X offset for widescreen mode
#define COCKPIT_WHEEL_WIDTH         24.0f   // Width of wheel graphic (half)
#define COCKPIT_WHEEL_HEIGHT        56.0f   // Height of wheel graphic
#define COCKPIT_WHEEL_BOTTOM_GAP    20.0f   // Gap from bottom of screen
#define COCKPIT_WHEEL_LEFT_OFFSET   31.0f   // Left wheel X offset from edge
#define COCKPIT_ENGINE_X_OFFSET     42.0f   // Engine flame X offset
#define COCKPIT_ENGINE_Y_OFFSET     123.0f  // Engine flame Y offset
#define COCKPIT_ENGINE_WIDTH        235.0f  // Engine flame width
#define COCKPIT_ENGINE_HEIGHT       35.0f   // Engine flame height
#define COCKPIT_TOP_X_OFFSET        41.0f   // Top panel X offset
#define COCKPIT_TOP_WIDTH           238.0f  // Top panel width
#define COCKPIT_TOP_HEIGHT          16.0f   // Top panel height
#define COCKPIT_SIDE_HEIGHT         153.0f  // Side panel height
#define COCKPIT_RIGHT_X_OFFSET      279.0f  // Right panel X offset
#define COCKPIT_DAMAGE_HEIGHT       8.0f    // Damage indicator height
#define COCKPIT_HOLE_X_OFFSET       47.0f   // First hole X offset
#define COCKPIT_HOLE_SPACING        24.0f   // Spacing between holes
#define COCKPIT_HOLE_WIDTH          12.0f   // Width of hole graphic (half)
#define COCKPIT_SPEEDBAR_X_OFFSET   196.0f  // Speed bar X offset
#define COCKPIT_SPEEDBAR_Y_OFFSET   61.0f   // Speed bar Y offset from bottom
#define COCKPIT_SPEEDBAR_WIDTH      242.0f  // Speed bar maximum width
#define COCKPIT_SPEEDBAR_HEIGHT     3.0f    // Speed bar height
#define COCKPIT_SPEEDBAR_MAX        240     // Maximum speed value for normal color
#define COCKPIT_WLEFT_X_OFFSET      40.0f   // Widescreen left panel width
#define COCKPIT_WRIGHT_X_OFFSET     82.0f   // Widescreen right panel offset from edge
#define COCKPIT_WRIGHT_Y_OFFSET     98.0f   // Widescreen right panel Y offset
#define COCKPIT_WLEFT_Y_OFFSET      99.0f   // Widescreen left panel Y offset

/*	===================== */
/*	Structure definitions */
/*	===================== */

/*	============================== */
/*	External function declarations */
/*	============================== */
extern HRESULT CreateCarVertexBuffer (IDirect3DDevice9 *pd3dDevice);

extern void FreeCarVertexBuffer (void);

extern void DrawCar (IDirect3DDevice9 *pd3dDevice);

extern HRESULT CreateCockpitVertexBuffer (IDirect3DDevice9 *pd3dDevice);

extern void FreeCockpitVertexBuffer (void);

extern void DrawCockpit (IDirect3DDevice9 *pd3dDevice);

#endif	/* _CAR */
