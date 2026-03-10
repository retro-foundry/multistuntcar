/**************************************************************************

    Backdrop.cpp - Functions for manipulating backdrop

 **************************************************************************/

/*    ============= */
/*    Include files */
/*    ============= */
#include "platform_sdl_gl.h"

#include "StuntCarRacer.h"
#include "Backdrop.h"
#include "3D_Engine.h"
#include "Track.h"

/*    ===== */
/*    Debug */
/*    ===== */
#if defined(DEBUG) || defined(_DEBUG)
extern FILE* out;
#endif

/*    ========= */
/*    Constants */
/*    ========= */
#define SKY_COLOUR (SCR_BASE_COLOUR + 7)
#define GROUND_COLOUR (SCR_BASE_COLOUR + 13)

// Scenery type ranges 0-4
#define MIN_SCENERY_TYPE 0
#define MAX_SCENERY_TYPE 4

/*    =========== */
/*    Static data */
/*    =========== */
static long current_scenery_type = MAX_SCENERY_TYPE;
static VertexBuffer* pScenery3DVB = NULL;
static long scenery3DVertexCount = 0;
static long scenery3DBuiltType = -1;

/*    ===================== */
/*    Function declarations */
/*    ===================== */
static void DrawHorizon(long viewpoint_y, long viewpoint_x_angle, long viewpoint_z_angle);

static void DrawScenery3D(RenderDevice* pDevice);

static HRESULT RebuildScenery3DVertexBuffer(RenderDevice* pDevice);

static long BuildGroundPlanePolygon(long x1, long y1, long x2, long y2, long screen_width, long screen_height,
                                    long upside_down, long clip_bottom_y, POINT* out_points);

static long ClipLine(long* x1ptr, long* y1ptr, long* x2ptr, long* y2ptr, long screen_width, long screen_height);

/*    ======================================================================================= */
/*    Function:        DrawBackdrop                                                            */
/*                                                                                            */
/*    Description:    Draw the backdrop using the supplied viewpoint                            */
/*    ======================================================================================= */

void DrawBackdrop(long viewpoint_y, long viewpoint_x_angle, long viewpoint_y_angle, long viewpoint_z_angle) {
    (void)viewpoint_y_angle;
    /* Clear screen to sky colour first so any unfilled pixels match the backdrop */
    {
        const DWORD sky = SCRGB(SKY_COLOUR);
        const float r = ((sky >> 0) & 0xff) / 255.0f;
        const float g = ((sky >> 8) & 0xff) / 255.0f;
        const float b = ((sky >> 16) & 0xff) / 255.0f;
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    DrawHorizon(viewpoint_y, viewpoint_x_angle, viewpoint_z_angle);
}

void DrawBackdropScenery3D(RenderDevice* pDevice) { DrawScenery3D(pDevice); }

/*    ======================================================================================= */
/*    Function:        NextSceneryType                                                            */
/*                                                                                            */
/*    Description:    Change to the next type of scenery                                        */
/*    ======================================================================================= */

void NextSceneryType(void) {
    current_scenery_type++;

    if (current_scenery_type > MAX_SCENERY_TYPE)
        current_scenery_type = MIN_SCENERY_TYPE;

    scenery3DBuiltType = -1;
}

/*    ======================================================================================= */
/*    Function:        DrawHorizon                                                                */
/*                                                                                            */
/*    Description:    Draw the horizon using the supplied viewpoint                            */
/*                    - fills the entire screen with ground/sky colours                        */
/*    ======================================================================================= */

// should only be able to see as far as horizon,
// so if track is further away then either :-
//
//        horizon should also be further away
//        OR
//        shouldn't be drawing pieces at this distance
//
// - viewpoint limitations should eventually prevent track exceeding horizon

static void DrawHorizon(long viewpoint_y, long viewpoint_x_angle, long viewpoint_z_angle) {
    long i;
    long upside_down = FALSE;
    short sin_x, sin_z;
    short cos_x, cos_z;
    long x, y, z, y_adjust;
    long trans_x, trans_y, trans_z;
    long screen_width, screen_height;
    long x1, y1, x2, y2;

    // set up two co-ordinates defining horizon line
    COORD_3D plane[2];
    COORD_2D screen_coords[2];

    // left co-ordinate
    plane[0].x = -0x00010000;
    plane[0].y = 0;
    plane[0].z = 0x00010000;

    // right co-ordinate
    plane[1].x = 0x00010000;
    plane[1].y = 0;
    plane[1].z = 0x00010000;

    // start of code
    GetScreenDimensions(&screen_width, &screen_height);

    // calculate y adjustment depending upon viewpoint_x_angle.
    // this is needed because only two horizon points are used rather than four.
    // when the rotated z values are negative (i.e. viewpoint_x_angle in range shown)
    // the resulting y values are negated so the y adjustment must also change sign
    if ((viewpoint_x_angle >= (MAX_ANGLE / 4)) &&
        (viewpoint_x_angle < ((3 * MAX_ANGLE) / 4))) // if >= 90 and less than 270 degrees
        y_adjust = (viewpoint_y >> LOG_PRECISION);
    else
        y_adjust = -(viewpoint_y >> LOG_PRECISION);

    y_adjust /= 2; // 24/04/1998 - reduce using PC_FACTOR

    // rotate two points about x/z axis and perform perspective projection
    GetSinCos(viewpoint_x_angle, &sin_x, &cos_x);
    GetSinCos(viewpoint_z_angle, &sin_z, &cos_z);
    for (i = 0; i < 2; i++) {
        x = plane[i].x;
        y = plane[i].y;
        z = plane[i].z;

        y += y_adjust;

        // rotate about x axis
        trans_y = (y * cos_x) - (z * sin_x);
        trans_z = (y * sin_x) + (z * cos_x);

        // rotate about z axis
        y = trans_y >> LOG_PRECISION;
        trans_x = (x * cos_z) - (y * sin_z);
        trans_y = (x * sin_z) + (y * cos_z);

        // perspective projection
        z = trans_z >> LOG_FOCUS;

        // debug stuff
        if (z == 0) {
#if defined(DEBUG) || defined(_DEBUG)
            fprintf(out, "7.  Preventing division by zero\n");
            //Sleep(10);
#endif

            z = 1;
        }

        x = (trans_x / z) + screen_width / 2;
        y = (trans_y / z) + screen_height / 2;

        // store screen x and screen y
        screen_coords[i].x = x;
        screen_coords[i].y = y;
    }

    x1 = screen_coords[0].x;
    y1 = screen_coords[0].y;
    x2 = screen_coords[1].x;
    y2 = screen_coords[1].y;

    //fprintf(out, "(x1,y1) = (%d,%d), (x2,y2) = (%d,%d)\n", x1, y1, x2, y2);

    // draw required rectangular sections
    long min_x = 0;
    long min_y = 0;
    long max_x = screen_width - 1;
    long max_y = screen_height - 1;
    long on_screen, colour_index = SKY_COLOUR;

    if ((x1 > x2) || ((x1 == x2) && (y1 > y2)))
        upside_down = (!upside_down);

    on_screen = ClipLine(&x1, &y1, &x2, &y2, screen_width, screen_height);

    //fprintf(out, "upside_down = %d, on_screen = %d\n", upside_down, on_screen);
    //fprintf(out, "clipped (x1,y1) = (%d,%d), (x2,y2) = (%d,%d)\n\n", x1, y1, x2, y2);

    // special check for when line has been clipped to a single
    // pixel (i.e. one of the four corners of the screen)
    if ((x1 == x2) && (y1 == y2))
        on_screen = FALSE;

    if (on_screen) {
        /* Draw sky and ground as polygons clipped by the horizon line so they meet exactly when view rolls */
        POINT points[MAX_POLY_SIDES];
        long sides;

        /* Sky: the half of the screen on the "sky" side of the horizon line */
        sides = BuildGroundPlanePolygon(x1, y1, x2, y2, screen_width, screen_height, !upside_down, -1, points);
        if (sides >= 3) {
            SetTextureColour(SKY_COLOUR);
            DrawPolygon(points, sides);
        }
        /* Ground: the half on the "ground" side; limit how far below the horizon it extends when camera is angled down */
        {
            long horizon_bottom = (y1 > y2) ? y1 : y2;
            long ground_extent = (long)(screen_height * 0.35f);
            long clip_bottom = horizon_bottom + ground_extent;
            if (clip_bottom > screen_height - 1)
                clip_bottom = screen_height - 1;
            sides = BuildGroundPlanePolygon(x1, y1, x2, y2, screen_width, screen_height, upside_down, clip_bottom, points);
        }
        if (sides >= 3) {
            SetTextureColour(GROUND_COLOUR);
            DrawPolygon(points, sides);
        }
    } else // horizon line is off screen
    {
        long off_top = FALSE, off_bottom = FALSE;
        long off_left = FALSE, off_right = FALSE;

        if ((y1 <= min_y) && (y2 <= min_y))
            off_top = TRUE;

        if ((y1 >= max_y) && (y2 >= max_y))
            off_bottom = TRUE;

        if ((x1 <= min_x) && (x2 <= min_x))
            off_left = TRUE;

        if ((x1 >= max_x) && (x2 >= max_x))
            off_right = TRUE;

        if (off_top) {
            if (!upside_down)
                colour_index = GROUND_COLOUR;
            else
                colour_index = SKY_COLOUR;
        } else if (off_bottom) {
            if (!upside_down)
                colour_index = SKY_COLOUR;
            else
                colour_index = GROUND_COLOUR;
        } else if (off_left) {
            if (y1 > y2)
                colour_index = GROUND_COLOUR;
            else
                colour_index = SKY_COLOUR;
        } else if (off_right) {
            if (y1 > y2)
                colour_index = SKY_COLOUR;
            else
                colour_index = GROUND_COLOUR;
        }

        DrawFilledRectangle(min_x, min_y, max_x, max_y, SCRGB(colour_index));
    }
}

/*    ======================================================================================= */
/*    Function:        DrawScenery3D                                                            */
/*                                                                                            */
/*    Description:    Draw horizon scenery as world-space 3D geometry                          */
/*    ======================================================================================= */

#define NUM_SCENERY_OBJECTS 32

#define MAX_SCENERY_COORDS 7

#define SCENERY_X_Y_SCALE_FACTOR 64 // (z of 0x00010000, divided by (4 * FOCUS))
#define SCENERY_WORLD_RING_OFFSET 45000
#define SCENERY_WORLD_BASE_Y (TRACK_BOTTOM_Y - 8)
#define SCENERY_BASE_VERTEX_DROP_PERCENT 0.15f
#define MAX_SCENERY_3D_VERTICES 4096

typedef struct {
    COORD_3D* coords;
    long coordsSize; // memory size for coords
    long numPolygons;
    long* polygons;
} SCENERY;

static void DrawScenery3D(RenderDevice* pDevice) {
    if (pDevice == NULL)
        return;

    if ((pScenery3DVB == NULL) || (scenery3DBuiltType != current_scenery_type)) {
        if (FAILED(RebuildScenery3DVertexBuffer(pDevice)))
            return;
    }

    if ((pScenery3DVB == NULL) || (scenery3DVertexCount <= 0))
        return;

    pDevice->SetRenderState(RS_ZENABLE, FALSE);
    pDevice->SetRenderState(RS_CULLMODE, CULL_NONE);
    pDevice->SetTextureStageState(0, TSS_COLOROP, TOP_DISABLE);
    pDevice->SetStreamSource(0, pScenery3DVB, 0, sizeof(UTVERTEX));
    pDevice->SetFVF(FVF_UTVERTEX);
    pDevice->DrawPrimitive(PT_TRIANGLELIST, 0, scenery3DVertexCount / 3);
}

static HRESULT RebuildScenery3DVertexBuffer(RenderDevice* pDevice) {
    if (pScenery3DVB) {
        pScenery3DVB->Release();
        pScenery3DVB = NULL;
    }
    scenery3DVertexCount = 0;
    scenery3DBuiltType = -1;

    if (FAILED(pDevice->CreateVertexBuffer(MAX_SCENERY_3D_VERTICES * sizeof(UTVERTEX), VB_USAGE_WRITEONLY,
                                           FVF_UTVERTEX, POOL_DEFAULT, &pScenery3DVB, NULL))) {
        OutputDebugStringW(L"ERROR: Failed to create 3D scenery vertex buffer\n");
        return E_FAIL;
    }

    UTVERTEX* pVertices = NULL;
    if (FAILED(pScenery3DVB->Lock(0, 0, (void**)&pVertices, 0))) {
        OutputDebugStringW(L"ERROR: Failed to lock 3D scenery vertex buffer\n");
        pScenery3DVB->Release();
        pScenery3DVB = NULL;
        return E_FAIL;
    }

    // scenery y positions (range 0-255)
    static long scenery_positions[NUM_SCENERY_OBJECTS] = {
        0x05, 0x0f, 0x15, 0x1f, 0x25, 0x2f, 0x35, 0x3f, 0x45, 0x4f, 0x55, 0x5f, 0x65, 0x6f, 0x75, 0x7f,
        0x85, 0x8f, 0x95, 0x9f, 0xa5, 0xaf, 0xb5, 0xbf, 0xc5, 0xcf, 0xd5, 0xdf, 0xe5, 0xef, 0xf5, 0xff};

    // scenery numbers (i.e. IDs, range 0-24)
    static long standard_numbers[NUM_SCENERY_OBJECTS] = {0, 13, 10, 11, 12, 5, 2, 3, 0, 1, 4, 5, 2, 1, 0, 5,
                                                         2, 3,  4,  5,  0,  9, 6, 7, 8, 5, 0, 3, 4, 1, 2, 5};

    static long taller_numbers[NUM_SCENERY_OBJECTS] = {14, 15, 15, 14, 15, 14, 14, 15, 14, 15, 14, 15, 15, 14, 15, 14,
                                                       14, 15, 15, 14, 15, 15, 14, 14, 15, 14, 15, 15, 14, 14, 14, 15};

    static long snowcapped_numbers[NUM_SCENERY_OBJECTS] = {16, 17, 18, 19, 17, 16, 17, 18, 18, 16, 19,
                                                           17, 19, 18, 18, 16, 19, 17, 17, 18, 16, 16,
                                                           19, 18, 17, 16, 19, 19, 17, 18, 16, 19};

    static long building_numbers[NUM_SCENERY_OBJECTS] = {20, 21, 22, 23, 23, 21, 20, 20, 21, 22, 22,
                                                         20, 20, 21, 23, 22, 21, 20, 21, 21, 23, 22,
                                                         20, 21, 23, 23, 22, 20, 21, 20, 21, 23};

    static long mixed_numbers[NUM_SCENERY_OBJECTS] = {16, 0,  18, 1,  17, 2,  17, 3,  18, 4,  19, 5,  20, 24, 21, 24,
                                                      23, 22, 12, 19, 6,  16, 7,  18, 8,  16, 9,  19, 10, 18, 11, 19};

    static long* scenery_types[] = {standard_numbers, taller_numbers, snowcapped_numbers, building_numbers,
                                    mixed_numbers};

    long* scenery_numbers = scenery_types[current_scenery_type];

    // scenery co-ordinates
    static COORD_3D standard1_c[] = {{0, 0, 0x00010000}, // x, y, z
                                     {0x180, 0, 0x00010000},
                                     {0x4b, 0x1c, 0x00010000},
                                     {0x104, 0x10, 0x00010000}};

    static COORD_3D standard2_c[] = {
        {0, 0, 0x00010000}, {0x100, 0, 0x00010000}, {0x7d, 0x12, 0x00010000}, {0xc0, 0x1e, 0x00010000}};

    static COORD_3D standard3_c[] = {
        {0, 0, 0x00010000}, {0x180, 0, 0x00010000}, {0x64, 0x14, 0x00010000}, {0x136, 0x25, 0x00010000}};

    static COORD_3D standard4_c[] = {
        {0, 0, 0x00010000}, {0x100, 0, 0x00010000}, {0x46, 0x18, 0x00010000}, {0xd8, 0x24, 0x00010000}};

    static COORD_3D standard5_c[] = {
        {0, 0, 0x00010000}, {0x180, 0, 0x00010000}, {0xc8, 0x27, 0x00010000}, {0xf0, 0x1f, 0x00010000}};

    static COORD_3D standard6_c[] = {
        {0, 0, 0x00010000}, {0x100, 0, 0x00010000}, {0x32, 0xc, 0x00010000}, {0xa8, 0x1a, 0x00010000}};

    static COORD_3D standard7_c[] = {
        {0, 0, 0x00010000}, {0x172, 0, 0x00010000}, {0x70, 0x19, 0x00010000}, {0xe6, 0x14, 0x00010000}};

    static COORD_3D standard8_c[] = {
        {0, 0, 0x00010000}, {0xfa, 0, 0x00010000}, {0x64, 0xc, 0x00010000}, {0xbb, 0x12, 0x00010000}};

    static COORD_3D standard9_c[] = {
        {0, 0, 0x00010000}, {0x180, 0, 0x00010000}, {0xc6, 0x1c, 0x00010000}, {0x13b, 0x18, 0x00010000}};

    static COORD_3D standard10_c[] = {
        {0, 0, 0x00010000}, {0x100, 0, 0x00010000}, {0x23, 0x28, 0x00010000}, {0x6e, 0x37, 0x00010000}};

    static COORD_3D standard11_c[] = {
        {0, 0, 0x00010000}, {0x159, 0, 0x00010000}, {0x5c, 0x2a, 0x00010000}, {0xf0, 0x1e, 0x00010000}};

    static COORD_3D standard12_c[] = {
        {0, 0, 0x00010000}, {0xfa, 0, 0x00010000}, {0x2d, 0xf, 0x00010000}, {0x80, 0xb, 0x00010000}};

    static COORD_3D standard13_c[] = {
        {0, 0, 0x00010000}, {0x17c, 0, 0x00010000}, {0x88, 0x2b, 0x00010000}, {0xd2, 0x23, 0x00010000}};

    static COORD_3D standard14_c[] = {
        {0, 0, 0x00010000}, {0x100, 0, 0x00010000}, {0x4b, 0x29, 0x00010000}, {0x9b, 0x37, 0x00010000}};

    static COORD_3D taller1_c[] = {
        {0, 0, 0x00010000}, {0x17c, 0, 0x00010000}, {0x88, 0, 0x00010000}, {0x2b, 0xd2, 0x00010000}};

    static COORD_3D taller2_c[] = {
        {0, 0, 0x00010000}, {0x100, 0, 0x00010000}, {0x4b, 0, 0x00010000}, {0x29, 0x9b, 0x00010000}};

    static COORD_3D snowcapped1_c[] = {{0, 0, 0x00010000},       {0xfa, 0, 0x00010000},     {0x1a4, 0, 0x00010000},
                                       {0x253, 0, 0x00010000},   {0x181, 0x2e, 0x00010000}, {0x118, 0x34, 0x00010000},
                                       {0x19f, 0x73, 0x00010000}};

    static COORD_3D snowcapped2_c[] = {{0, 0, 0x00010000},      {0x4b, 0, 0x00010000},    {0x127, 0, 0x00010000},
                                       {0x1f4, 0, 0x00010000},  {0xaf, 0x32, 0x00010000}, {0x87, 0x3c, 0x00010000},
                                       {0xff, 0x48, 0x00010000}};

    static COORD_3D snowcapped3_c[] = {{0, 0, 0x00010000},      {0x87, 0, 0x00010000},    {0xc5, 0, 0x00010000},
                                       {0xfa, 0, 0x00010000},   {0x96, 0x46, 0x00010000}, {0x69, 0x50, 0x00010000},
                                       {0xaa, 0x5f, 0x00010000}};

    static COORD_3D snowcapped4_c[] = {{0, 0, 0x00010000},      {0x87, 0, 0x00010000},    {0x113, 0, 0x00010000},
                                       {0x1a9, 0, 0x00010000},  {0x91, 0x2a, 0x00010000}, {0x3c, 0x32, 0x00010000},
                                       {0x8c, 0x4d, 0x00010000}};

    static COORD_3D building1_c[] = {{0, 0, 0x00010000},    {0x10, 0, 0x00010000},    {0x18, 0, 0x00010000},
                                     {0, 0x50, 0x00010000}, {0x10, 0x50, 0x00010000}, {0x18, 0x50, 0x00010000}};

    static COORD_3D building2_c[] = {{0, 0, 0x00010000},    {0x10, 0, 0x00010000},    {0x18, 0, 0x00010000},
                                     {0, 0x3c, 0x00010000}, {0x10, 0x3c, 0x00010000}, {0x18, 0x3c, 0x00010000}};

    static COORD_3D building3_c[] = {{0, 0, 0x00010000},    {0x28, 0, 0x00010000},    {0x3c, 0, 0x00010000},
                                     {0, 0x39, 0x00010000}, {0x28, 0x39, 0x00010000}, {0x3c, 0x39, 0x00010000}};

    static COORD_3D building4_c[] = {{0, 0, 0x00010000},    {0x69, 0, 0x00010000},    {0x7d, 0, 0x00010000},
                                     {0, 0x2a, 0x00010000}, {0x69, 0x2a, 0x00010000}, {0x7d, 0x2a, 0x00010000}};

    static COORD_3D lake_c[] = {
        {0, 8, 0x00010000}, {0x32, 0, 0x00010000}, {0x28a, 0x0, 0x00010000}, {0x2bc, 8, 0x00010000}};

    // scenery polygons
    static long standard_p[] = {5,           // colour
                                4,           // number of sides
                                1, 0, 2, 3}; // co-ordinate offsets

    static long taller_p[] = {4, 3, 2, 0, 3, 5, 3, 1, 2, 3};

    static long snowcapped_p[] = {4, 4, 1, 0, 5, 4, 5, 3, 2, 1, 4, 5, 4, 3, 2, 4, 6, 15, 3, 4, 5, 6};

    static long building_p[] = {15, 4, 1, 0, 3, 4, 14, 4, 2, 1, 4, 5};

    static long lake_p[] = {6, 4, 2, 1, 0, 3};

    // scenery definitions
    static SCENERY standard1 = {standard1_c, sizeof(standard1_c), 1, standard_p};
    static SCENERY standard2 = {standard2_c, sizeof(standard2_c), 1, standard_p};
    static SCENERY standard3 = {standard3_c, sizeof(standard3_c), 1, standard_p};
    static SCENERY standard4 = {standard4_c, sizeof(standard4_c), 1, standard_p};
    static SCENERY standard5 = {standard5_c, sizeof(standard5_c), 1, standard_p};
    static SCENERY standard6 = {standard6_c, sizeof(standard6_c), 1, standard_p};
    static SCENERY standard7 = {standard7_c, sizeof(standard7_c), 1, standard_p};
    static SCENERY standard8 = {standard8_c, sizeof(standard8_c), 1, standard_p};
    static SCENERY standard9 = {standard9_c, sizeof(standard9_c), 1, standard_p};
    static SCENERY standard10 = {standard10_c, sizeof(standard10_c), 1, standard_p};
    static SCENERY standard11 = {standard11_c, sizeof(standard11_c), 1, standard_p};
    static SCENERY standard12 = {standard12_c, sizeof(standard12_c), 1, standard_p};
    static SCENERY standard13 = {standard13_c, sizeof(standard13_c), 1, standard_p};
    static SCENERY standard14 = {standard14_c, sizeof(standard14_c), 1, standard_p};

    static SCENERY taller1 = {taller1_c, sizeof(taller1_c), 2, taller_p};
    static SCENERY taller2 = {taller2_c, sizeof(taller2_c), 2, taller_p};

    static SCENERY snowcapped1 = {snowcapped1_c, sizeof(snowcapped1_c), 4, snowcapped_p};
    static SCENERY snowcapped2 = {snowcapped2_c, sizeof(snowcapped2_c), 4, snowcapped_p};
    static SCENERY snowcapped3 = {snowcapped3_c, sizeof(snowcapped3_c), 4, snowcapped_p};
    static SCENERY snowcapped4 = {snowcapped4_c, sizeof(snowcapped4_c), 4, snowcapped_p};

    static SCENERY building1 = {building1_c, sizeof(building1_c), 2, building_p};
    static SCENERY building2 = {building2_c, sizeof(building2_c), 2, building_p};
    static SCENERY building3 = {building3_c, sizeof(building3_c), 2, building_p};
    static SCENERY building4 = {building4_c, sizeof(building4_c), 2, building_p};

    static SCENERY lake = {lake_c, sizeof(lake_c), 1, lake_p};

    static SCENERY* scenery_objects[NUM_SCENERY_OBJECTS] = {
        &standard1, &standard2, &standard3,   &standard4,   &standard5,   &standard6,   &standard7,
        &standard8, &standard9, &standard10,  &standard11,  &standard12,  &standard13,  &standard14,
        &taller1,   &taller2,   &snowcapped1, &snowcapped2, &snowcapped3, &snowcapped4, &building1,
        &building2, &building3, &building4,   &lake};

    SCENERY* scenery;

    COORD_3D* scenery_coords;
    glm::vec3 world_coords[MAX_SCENERY_COORDS];

    long m, i, j, number, sides, offset;
    long position, y_angle;
    short sin_y, cos_y;
    long x, y, z;
    long trans_x, trans_z;
    long* polygons;
    long polygon_offsets[MAX_POLY_SIDES];

    BYTE colour;
    const long scale = (1L << (LOG_CUBE_SIZE - LOG_PRECISION));
    const float world_center = static_cast<float>((NUM_TRACK_CUBES * scale) / 2);
    const float base_y = static_cast<float>(SCENERY_WORLD_BASE_Y);

    for (m = 0; m < NUM_SCENERY_OBJECTS; m++) {
        // get pointer to scenery definition
        number = scenery_numbers[m];
        scenery = scenery_objects[number];

        // get pointer to scenery co-ordinates
        scenery_coords = scenery->coords;

        // calculate number of co-ordinates
        number = scenery->coordsSize / sizeof(COORD_3D);

        // Extend base vertices down by 5% of object height so feet meet the ground.
        long max_source_y = 0;
        for (i = 0; i < number; ++i) {
            if (scenery_coords[i].y > max_source_y)
                max_source_y = scenery_coords[i].y;
        }
        float base_vertex_drop = static_cast<float>(max_source_y * SCENERY_X_Y_SCALE_FACTOR) *
                                 SCENERY_BASE_VERTEX_DROP_PERCENT;
        if (base_vertex_drop < 1.0f)
            base_vertex_drop = 1.0f;

        // calculate y angle for this scenery object around the world centre
        position = scenery_positions[m] * 256;
        y_angle = (-position & (MAX_ANGLE - 1));

        GetSinCos(y_angle, &sin_y, &cos_y);

        // rotate scenery around world centre and store world-space coords
        for (i = 0; i < number; i++) {
            const bool is_base_vertex = (scenery_coords[i].y == 0);
            x = scenery_coords[i].x * SCENERY_X_Y_SCALE_FACTOR;
            y = scenery_coords[i].y * SCENERY_X_Y_SCALE_FACTOR;
            z = scenery_coords[i].z + SCENERY_WORLD_RING_OFFSET;

            // rotate about y axis
            trans_x = (x * cos_y) + (z * sin_y);
            trans_z = (z * cos_y) - (x * sin_y);

            world_coords[i].x = world_center + static_cast<float>(trans_x >> LOG_PRECISION);
            world_coords[i].y = base_y + static_cast<float>(y) + (is_base_vertex ? base_vertex_drop : 0.0f);
            world_coords[i].z = world_center + static_cast<float>(trans_z >> LOG_PRECISION);
        }

        // draw scenery object
        polygons = scenery->polygons;
        for (i = 0; i < scenery->numPolygons; i++) {
            colour = (BYTE)*polygons++;

            sides = *polygons++;
            if ((sides < 3) || (sides > MAX_POLY_SIDES)) {
                polygons += sides;
                continue;
            }

            for (j = 0; j < sides; j++) {
                polygon_offsets[j] = *polygons++;
            }

            const DWORD poly_colour = SCRGB(SCR_BASE_COLOUR + colour);
            for (j = 1; j < (sides - 1); ++j) {
                if ((scenery3DVertexCount + 3) > MAX_SCENERY_3D_VERTICES)
                    goto done;

                offset = polygon_offsets[0];
                pVertices[scenery3DVertexCount].pos = world_coords[offset];
                pVertices[scenery3DVertexCount].color = poly_colour;
                pVertices[scenery3DVertexCount].tu = 0.0f;
                pVertices[scenery3DVertexCount].tv = 0.0f;
                ++scenery3DVertexCount;

                offset = polygon_offsets[j];
                pVertices[scenery3DVertexCount].pos = world_coords[offset];
                pVertices[scenery3DVertexCount].color = poly_colour;
                pVertices[scenery3DVertexCount].tu = 0.0f;
                pVertices[scenery3DVertexCount].tv = 0.0f;
                ++scenery3DVertexCount;

                offset = polygon_offsets[j + 1];
                pVertices[scenery3DVertexCount].pos = world_coords[offset];
                pVertices[scenery3DVertexCount].color = poly_colour;
                pVertices[scenery3DVertexCount].tu = 0.0f;
                pVertices[scenery3DVertexCount].tv = 0.0f;
                ++scenery3DVertexCount;
            }
        }
    }

done:
    pScenery3DVB->Unlock();

    if (scenery3DVertexCount <= 0) {
        pScenery3DVB->Release();
        pScenery3DVB = NULL;
        return E_FAIL;
    }

    scenery3DBuiltType = current_scenery_type;
    return S_OK;
}

static long BuildGroundPlanePolygon(long x1, long y1, long x2, long y2, long screen_width, long screen_height,
                                    long upside_down, long clip_bottom_y, POINT* out_points) {
    typedef struct {
        float x;
        float y;
    } FPOINT;

    FPOINT input[MAX_POLY_SIDES];
    FPOINT output[MAX_POLY_SIDES];
    long input_count = 4;
    long output_count = 0;
    long i;

    const float a = static_cast<float>(y1 - y2);
    const float b = static_cast<float>(x2 - x1);
    const float c = static_cast<float>(x1 * y2 - x2 * y1);

    const float ref_x = static_cast<float>(screen_width / 2);
    const float ref_y = upside_down ? 0.0f : static_cast<float>(screen_height - 1);
    const bool keep_positive = ((a * ref_x) + (b * ref_y) + c) >= 0.0f;

    /* When clip_bottom_y >= 0, limit the bottom of the clip rect so ground doesn't extend too far below the horizon */
    const float bottom_y = (clip_bottom_y >= 0)
        ? (float)(clip_bottom_y < screen_height - 1 ? clip_bottom_y : screen_height - 1)
        : static_cast<float>(screen_height - 1);

    input[0].x = 0.0f;
    input[0].y = 0.0f;
    input[1].x = static_cast<float>(screen_width - 1);
    input[1].y = 0.0f;
    input[2].x = static_cast<float>(screen_width - 1);
    input[2].y = bottom_y;
    input[3].x = 0.0f;
    input[3].y = bottom_y;

    for (i = 0; i < input_count; ++i) {
        const FPOINT s = input[i];
        const FPOINT e = input[(i + 1) % input_count];

        const float ds = (a * s.x) + (b * s.y) + c;
        const float de = (a * e.x) + (b * e.y) + c;
        const bool s_inside = keep_positive ? (ds >= 0.0f) : (ds <= 0.0f);
        const bool e_inside = keep_positive ? (de >= 0.0f) : (de <= 0.0f);

        if (s_inside && e_inside) {
            if (output_count < MAX_POLY_SIDES)
                output[output_count++] = e;
            continue;
        }

        if (s_inside != e_inside) {
            float t = 0.0f;
            const float denom = ds - de;
            if (fabsf(denom) > 0.00001f)
                t = ds / denom;
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;

            FPOINT cross;
            cross.x = s.x + ((e.x - s.x) * t);
            cross.y = s.y + ((e.y - s.y) * t);
            if (output_count < MAX_POLY_SIDES)
                output[output_count++] = cross;
        }

        if (!s_inside && e_inside) {
            if (output_count < MAX_POLY_SIDES)
                output[output_count++] = e;
        }
    }

    long sides = 0;
    for (i = 0; i < output_count; ++i) {
        const long px = static_cast<long>(output[i].x + (output[i].x >= 0.0f ? 0.5f : -0.5f));
        const long py = static_cast<long>(output[i].y + (output[i].y >= 0.0f ? 0.5f : -0.5f));
        if ((sides > 0) && (out_points[sides - 1].x == px) && (out_points[sides - 1].y == py))
            continue;
        out_points[sides].x = px;
        out_points[sides].y = py;
        ++sides;
    }

    if ((sides >= 2) && (out_points[0].x == out_points[sides - 1].x) && (out_points[0].y == out_points[sides - 1].y))
        --sides;

    return sides;
}

/*    ======================================================================================= */
/*    Function:        ClipLine                                                                */
/*                                                                                            */
/*    Description:    Clip line co-ordinates to screen boundaries                                */
/*                    Return FALSE if line is entirely off screen                                */
/*    ======================================================================================= */

static long ClipLine(long* x1ptr, long* y1ptr, long* x2ptr, long* y2ptr, long screen_width, long screen_height) {
    long on_screen = FALSE;
    long x1 = *x1ptr, y1 = *y1ptr, x2 = *x2ptr, y2 = *y2ptr;
    long max_x = screen_width - 1;
    long max_y = screen_height - 1;

    // clip x1
    if (x1 < 0) {
        // x1 is off left of screen
        if (x2 < 0)
            goto store_line; // entire line is off left of screen
        else {
            // clip line to left edge, giving a new value for y1
            y1 -= ((x1 * (y2 - y1)) / (x2 - x1));
            x1 = 0;
        }
    } else if (x1 > max_x) {
        // x1 is off right of screen
        if (x2 > max_x)
            goto store_line; // entire line is off right of screen
        else {
            // clip line to right edge, giving a new value for y1
            y1 -= (((x1 - max_x) * (y2 - y1)) / (x2 - x1));
            x1 = max_x;
        }
    }

    // clip y1
    if (y1 < 0) {
        // y1 is off top of screen
        if (y2 < 0)
            goto store_line; // entire line is off top of screen
        else {
            // clip line to top edge, giving a new value for x1
            x1 -= ((y1 * (x2 - x1)) / (y2 - y1));
            y1 = 0;

            // check new x1 is on screen
            if ((x1 < 0) || (x1 > max_x))
                goto store_line;
        }
    } else if (y1 > max_y) {
        // y1 is off bottom of screen
        if (y2 > max_y)
            goto store_line; // entire line is off bottom of screen
        else {
            // clip line to bottom edge, giving a new value for x1
            x1 -= (((y1 - max_y) * (x2 - x1)) / (y2 - y1));
            y1 = max_y;

            // check new x1 is on screen
            if ((x1 < 0) || (x1 > max_x))
                goto store_line;
        }
    }

    // clip x2
    if (x2 < 0) {
        // x2 is off left of screen
        // clip line to left edge, giving a new value for y2
        y2 -= ((x2 * (y1 - y2)) / (x1 - x2));
        x2 = 0;
    } else if (x2 > max_x) {
        // x2 is off right of screen
        // clip line to right edge, giving a new value for y2
        y2 -= (((x2 - max_x) * (y1 - y2)) / (x1 - x2));
        x2 = max_x;
    }

    // clip y2
    if (y2 < 0) {
        // y2 is off top of screen
        // clip line to top edge, giving a new value for x2
        x2 -= ((y2 * (x1 - x2)) / (y1 - y2));
        y2 = 0;

        // check new x2 is on screen
        if ((x2 < 0) || (x2 > max_x))
            goto store_line;
    } else if (y2 > max_y) {
        // y2 is off bottom of screen
        // clip line to bottom edge, giving a new value for x2
        x2 -= (((y2 - max_y) * (x1 - x2)) / (y1 - y2));
        y2 = max_y;

        // check new x2 is on screen
        if ((x2 < 0) || (x2 > max_x))
            goto store_line;
    }

    on_screen = TRUE;

store_line:
    // store clipped points (possibly only partially clipped if off screen)
    *x1ptr = x1;
    *y1ptr = y1;
    *x2ptr = x2;
    *y2ptr = y2;
    return (on_screen);
}
