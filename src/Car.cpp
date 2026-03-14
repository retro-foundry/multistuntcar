/**************************************************************************

    Car.cpp - Functions for manipulating car (excluding player's car behaviour)

 **************************************************************************/

/*    ============= */
/*    Include files */
/*    ============= */
#include "platform_sdl_gl.h"

#include "Car.h"
#include "StuntCarRacer.h"
#include "3D_Engine.h"
#include "Atlas.h"
/*    ===== */
/*    Debug */
/*    ===== */
extern FILE* out;

/*    ========= */
/*    Constants */
/*    ========= */
#define SCR_BASE_COLOUR 26

#define MAX_VERTICES_PER_CAR (142 * 3)

extern bool bSuperLeague;
extern int wideScreen;
extern bool bPaused;

/*    =========== */
/*    Static data */
/*    =========== */

/*    ===================== */
/*    Function declarations */
/*    ===================== */
/*
static void DrawHorizon( long viewpoint_y,
                         long viewpoint_x_angle,
                         long viewpoint_z_angle );
*/


/*    ======================================================================================= */
/*    Function:        DrawCar                                                                    */
/*                                                                                            */
/*    Description:    Draw the car using the supplied viewpoint                                */
/*    ======================================================================================= */
static VertexBuffer* pCarVB = NULL;
static long numCarVertices = 0;

static void StoreCarTriangle(COORD_3D* c1, COORD_3D* c2, COORD_3D* c3, UTVERTEX* pVertices, DWORD colour) {
    glm::vec3 v1(static_cast<float>(c1->x), static_cast<float>(c1->y), static_cast<float>(c1->z));
    glm::vec3 v2(static_cast<float>(c2->x), static_cast<float>(c2->y), static_cast<float>(c2->z));
    glm::vec3 v3(static_cast<float>(c3->x), static_cast<float>(c3->y), static_cast<float>(c3->z));

    if ((numCarVertices + 3) > MAX_VERTICES_PER_CAR) {
        MessageBox(NULL, L"Exceeded numCarVertices", L"StoreCarTriangle", MB_OK);
        return;
    }

    /*
    // Calculate surface normal: surface_normal = glm::normalize(glm::cross(v2 - v1, v3 - v2));
    */

    pVertices[numCarVertices].pos = v1;
    //    pVertices[numCarVertices].normal = surface_normal;
    pVertices[numCarVertices].color = colour;
    ++numCarVertices;

    pVertices[numCarVertices].pos = v2;
    //    pVertices[numCarVertices].normal = surface_normal;
    pVertices[numCarVertices].color = colour;
    ++numCarVertices;

    pVertices[numCarVertices].pos = v3;
    //    pVertices[numCarVertices].normal = surface_normal;
    pVertices[numCarVertices].color = colour;
    ++numCarVertices;
}

static void CreateCarInVB(UTVERTEX* pVertices) {
    static long first_time = TRUE;
    // car co-ordinates
    static COORD_3D car[16 + 8] = {//x,                y,                    z
                                   {-VCAR_WIDTH / 2, -VCAR_HEIGHT / 4, -VCAR_LENGTH / 2}, // rear left wheel
                                   {-VCAR_WIDTH / 2, 0, -VCAR_LENGTH / 2},
                                   {-VCAR_WIDTH / 4, 0, -VCAR_LENGTH / 2},
                                   {-VCAR_WIDTH / 4, -VCAR_HEIGHT / 4, -VCAR_LENGTH / 2},

                                   {VCAR_WIDTH / 4, -VCAR_HEIGHT / 4, -VCAR_LENGTH / 2}, // rear right wheel
                                   {VCAR_WIDTH / 4, 0, -VCAR_LENGTH / 2},
                                   {VCAR_WIDTH / 2, 0, -VCAR_LENGTH / 2},
                                   {VCAR_WIDTH / 2, -VCAR_HEIGHT / 4, -VCAR_LENGTH / 2},

                                   {-VCAR_WIDTH / 2, -VCAR_HEIGHT / 4, VCAR_LENGTH / 2}, // front left wheel
                                   {-VCAR_WIDTH / 2, 0, VCAR_LENGTH / 2},
                                   {-VCAR_WIDTH / 4, 0, VCAR_LENGTH / 2},
                                   {-VCAR_WIDTH / 4, -VCAR_HEIGHT / 4, VCAR_LENGTH / 2},

                                   {VCAR_WIDTH / 4, -VCAR_HEIGHT / 4, VCAR_LENGTH / 2}, // front right wheel
                                   {VCAR_WIDTH / 4, 0, VCAR_LENGTH / 2},
                                   {VCAR_WIDTH / 2, 0, VCAR_LENGTH / 2},
                                   {VCAR_WIDTH / 2, -VCAR_HEIGHT / 4, VCAR_LENGTH / 2},

                                   {-VCAR_WIDTH / 4, -VCAR_HEIGHT / 8, -VCAR_LENGTH / 2}, // car rear points
                                   {-(3 * VCAR_WIDTH) / 16, VCAR_HEIGHT / 4, -VCAR_LENGTH / 2},
                                   {(3 * VCAR_WIDTH) / 16, VCAR_HEIGHT / 4, -VCAR_LENGTH / 2},
                                   {VCAR_WIDTH / 4, -VCAR_HEIGHT / 8, -VCAR_LENGTH / 2},

                                   {-VCAR_WIDTH / 4, -VCAR_HEIGHT / 8, VCAR_LENGTH / 2}, // car front points
                                   {-VCAR_WIDTH / 4, 0, VCAR_LENGTH / 2},
                                   {VCAR_WIDTH / 4, 0, VCAR_LENGTH / 2},
                                   {VCAR_WIDTH / 4, -VCAR_HEIGHT / 8, VCAR_LENGTH / 2}};

    /*
    if (first_time)
        {
        first_time = FALSE;
        // temporarily reduce car size at runtime
        // eventually car size will be decided and this code can be removed
        long i, reduce = 2;
        for (i = 0; i < (sizeof(car) / sizeof(COORD_3D)); i++)
            {
            car[i].x /= reduce;
            car[i].y /= reduce;
            car[i].z /= reduce;
            }
        }
    */

    // rear left wheel
    DWORD colour = SCRGB(SCR_BASE_COLOUR + 0);
    /**/
#define vertices pVertices
    // viewing from back
    StoreCarTriangle(&car[0], &car[1], &car[2], vertices, colour);
    StoreCarTriangle(&car[0], &car[2], &car[3], vertices, colour);
    // viewing from front
    StoreCarTriangle(&car[3], &car[2], &car[1], vertices, colour);
    StoreCarTriangle(&car[3], &car[1], &car[0], vertices, colour);

    // rear right wheel
    // viewing from back
    StoreCarTriangle(&car[0 + 4], &car[1 + 4], &car[2 + 4], vertices, colour);
    StoreCarTriangle(&car[0 + 4], &car[2 + 4], &car[3 + 4], vertices, colour);
    // viewing from front
    StoreCarTriangle(&car[3 + 4], &car[2 + 4], &car[1 + 4], vertices, colour);
    StoreCarTriangle(&car[3 + 4], &car[1 + 4], &car[0 + 4], vertices, colour);
    /**/
    /**/
    // front left wheel
    // viewing from back
    StoreCarTriangle(&car[0 + 8], &car[1 + 8], &car[2 + 8], vertices, colour);
    StoreCarTriangle(&car[0 + 8], &car[2 + 8], &car[3 + 8], vertices, colour);
    // viewing from front
    StoreCarTriangle(&car[3 + 8], &car[2 + 8], &car[1 + 8], vertices, colour);
    StoreCarTriangle(&car[3 + 8], &car[1 + 8], &car[0 + 8], vertices, colour);

    // front right wheel
    // viewing from back
    StoreCarTriangle(&car[0 + 12], &car[1 + 12], &car[2 + 12], vertices, colour);
    StoreCarTriangle(&car[0 + 12], &car[2 + 12], &car[3 + 12], vertices, colour);
    // viewing from front
    StoreCarTriangle(&car[3 + 12], &car[2 + 12], &car[1 + 12], vertices, colour);
    StoreCarTriangle(&car[3 + 12], &car[1 + 12], &car[0 + 12], vertices, colour);
    /**/

    // car left side
    if (bSuperLeague)
        colour = SCRGB(SCR_BASE_COLOUR + 21);
    else
        colour = SCRGB(SCR_BASE_COLOUR + 12);
    StoreCarTriangle(&car[4 + 16], &car[5 + 16], &car[1 + 16], vertices, colour);
    StoreCarTriangle(&car[4 + 16], &car[1 + 16], &car[0 + 16], vertices, colour);
    // car right side
    StoreCarTriangle(&car[3 + 16], &car[2 + 16], &car[6 + 16], vertices, colour);
    StoreCarTriangle(&car[3 + 16], &car[6 + 16], &car[7 + 16], vertices, colour);

    // car back
    if (bSuperLeague)
        colour = SCRGB(SCR_BASE_COLOUR + 20);
    else
        colour = SCRGB(SCR_BASE_COLOUR + 10);
    StoreCarTriangle(&car[0 + 16], &car[1 + 16], &car[2 + 16], vertices, colour);
    StoreCarTriangle(&car[0 + 16], &car[2 + 16], &car[3 + 16], vertices, colour);
    // car front
    StoreCarTriangle(&car[7 + 16], &car[6 + 16], &car[5 + 16], vertices, colour);
    StoreCarTriangle(&car[7 + 16], &car[5 + 16], &car[4 + 16], vertices, colour);

    // car top
    colour = SCRGB(SCR_BASE_COLOUR + 15);
    StoreCarTriangle(&car[1 + 16], &car[5 + 16], &car[6 + 16], vertices, colour);
    StoreCarTriangle(&car[1 + 16], &car[6 + 16], &car[2 + 16], vertices, colour);
    // car bottom
    if (bSuperLeague)
        colour = SCRGB(SCR_BASE_COLOUR + 19);
    else
        colour = SCRGB(SCR_BASE_COLOUR + 9);
    StoreCarTriangle(&car[3 + 16], &car[7 + 16], &car[4 + 16], vertices, colour);
    StoreCarTriangle(&car[3 + 16], &car[4 + 16], &car[0 + 16], vertices, colour);
#undef vertices
}

HRESULT CreateCarVertexBuffer(RenderDevice* pDevice) {
    if (pCarVB == NULL) {
        if (FAILED(pDevice->CreateVertexBuffer(MAX_VERTICES_PER_CAR * sizeof(UTVERTEX), VB_USAGE_WRITEONLY,
                                                  FVF_UTVERTEX, POOL_DEFAULT, &pCarVB, NULL))) {
            OutputDebugStringW(L"ERROR: Failed to create car vertex buffer\n");
            return E_FAIL;
        }
    }

    UTVERTEX* pVertices;
    if (FAILED(pCarVB->Lock(0, 0, (void**)&pVertices, 0))) {
        OutputDebugStringW(L"ERROR: Failed to lock car vertex buffer\n");
        return E_FAIL;
    }
    numCarVertices = 0;
    CreateCarInVB(pVertices);
    pCarVB->Unlock();
    return S_OK;
}

void FreeCarVertexBuffer(void) {
    if (pCarVB)
        pCarVB->Release(), pCarVB = NULL;
}

void DrawCar(RenderDevice* pDevice) {
    pDevice->SetRenderState(RS_ZENABLE, TRUE);
    pDevice->SetRenderState(RS_CULLMODE, CULL_CCW);

    pDevice->SetStreamSource(0, pCarVB, 0, sizeof(UTVERTEX));
    pDevice->SetFVF(FVF_UTVERTEX);
    pDevice->DrawPrimitive(PT_TRIANGLELIST, 0, numCarVertices / 3); // 3 points per triangle
}

struct TRANSFORMEDTEXVERTEX {
    FLOAT x, y, z, rhw; // The transformed position for the vertex.
    FLOAT u, v;         // Texture
};
#define FVF_TRANSFORMEDTEXVERTEX (FVF_XYZRHW | FVF_TEX1)

struct TRANSFORMEDCOLVERTEX {
    FLOAT x, y, z, rhw; // The transformed position for the vertex.
    DWORD color;        // Color
};
#define FVF_TRANSFORMEDCOLVERTEX (FVF_XYZRHW | FVF_DIFFUSE)

static VertexBuffer *pCockpitVB = NULL, *pSpeedBarCB = NULL;
#define MAX_COCKIPTVB 512
static int old_speedbar = -1;
static int old_leftwheel = -1, old_rightwheel = -1;

extern GpuTexture* g_pAtlas;
extern GpuTexture* g_pCockpitAtlas;
extern long front_left_amount_below_road, front_right_amount_below_road;
extern long leftwheel_angle, rightwheel_angle;
extern long boost_activated;
extern long new_damage;
extern long nholes;

HRESULT CreateCockpitVertexBuffer(RenderDevice* pDevice) {
    if (pCockpitVB == NULL) {
        if (FAILED(pDevice->CreateVertexBuffer(MAX_COCKIPTVB * sizeof(TRANSFORMEDTEXVERTEX), VB_USAGE_WRITEONLY,
                                                  FVF_TRANSFORMEDTEXVERTEX, POOL_DEFAULT, &pCockpitVB, NULL))) {
            OutputDebugStringW(L"ERROR: Failed to create cockpit vertex buffer\n");
            return E_FAIL;
        }
    }
    if (pSpeedBarCB == NULL) {
        if (FAILED(pDevice->CreateVertexBuffer(4 * sizeof(TRANSFORMEDCOLVERTEX), VB_USAGE_WRITEONLY,
                                                  FVF_TRANSFORMEDCOLVERTEX, POOL_DEFAULT, &pSpeedBarCB, NULL))) {
            OutputDebugStringW(L"ERROR: Failed to create speed bar vertex buffer\n");
            return E_FAIL;
        }
    }
    return S_OK;
}

void FreeCockpitVertexBuffer(void) {
    if (pCockpitVB)
        pCockpitVB->Release(), pCockpitVB = NULL;
    if (pSpeedBarCB)
        pSpeedBarCB->Release(), pSpeedBarCB = NULL;
    /*if (pLeftwheelVB) pLeftwheelVB->Release(), pLeftwheelVB = NULL;
    if (pRightwheelVB) pRightwheelVB->Release(), pRightwheelVB = NULL;*/
}

extern long CalculateDisplaySpeed(void);

static int cockpit_vtx = 0;
static float g_cockpitAtlasUOffset = 0.0f;
static const float kCockpitAtlasUShift = 100.0f / 1024.0f;
static const float kCockpitAtlasSideUShift = 100.0f / 1024.0f;
static const float kCockpitAtlasSideLeftU1 = 0.0f / 1024.0f;
static const float kCockpitAtlasSideLeftU2 = 141.0f / 1024.0f;
static const float kCockpitAtlasSideRightU1 = 379.0f / 1024.0f;
static const float kCockpitAtlasSideRightU2 = 520.0f / 1024.0f;
static const float kCockpitAtlasSideVExtend = 47.0f / 1024.0f;
static bool g_cockpitAtlasUseExtendedSideUV = false;
static bool g_cockpitAtlasUseSingleSideQuad = false;
static void AddQuad(TRANSFORMEDTEXVERTEX* pVertices, float x1, float y1, float x2, float y2, float z, int idx, int revX,
                    float w) {
    float u1 = (revX) ? atlas_tx2[idx] : atlas_tx1[idx], v1 = atlas_ty1[idx];
    float u2 = (revX) ? atlas_tx1[idx] : atlas_tx2[idx], v2 = atlas_ty2[idx];
    bool useCustomSideU = false;
    if (g_cockpitAtlasUseExtendedSideUV) {
        if (idx == eCockpitLeft || idx == eCockpitLeft2) {
            u1 = kCockpitAtlasSideLeftU1 + kCockpitAtlasSideUShift;
            u2 = (g_cockpitAtlasUseSingleSideQuad ? kCockpitAtlasSideRightU2 : kCockpitAtlasSideLeftU2) +
                 kCockpitAtlasSideUShift;
            v2 += (v2 >= v1) ? kCockpitAtlasSideVExtend : -kCockpitAtlasSideVExtend;
            useCustomSideU = true;
        } else if (idx == eCockpitRight || idx == eCockpitRight2) {
            u1 = kCockpitAtlasSideRightU1 + kCockpitAtlasSideUShift;
            u2 = kCockpitAtlasSideRightU2 + kCockpitAtlasSideUShift;
            v2 += (v2 >= v1) ? kCockpitAtlasSideVExtend : -kCockpitAtlasSideVExtend;
            useCustomSideU = true;
        }
    }
    if (!useCustomSideU) {
        u1 += g_cockpitAtlasUOffset;
        u2 += g_cockpitAtlasUOffset;
    }
    if (w != 1.0f) {
        u2 = u1 + (u2 - u1) * w;
    }
    pVertices += cockpit_vtx;
    pVertices[0].x = x1;
    pVertices[0].y = y1;
    pVertices[0].z = z;
    pVertices[0].rhw = 1.0f;
    pVertices[1].x = x2;
    pVertices[1].y = y1;
    pVertices[1].z = z;
    pVertices[1].rhw = 1.0f;
    pVertices[2].x = x2;
    pVertices[2].y = y2;
    pVertices[2].z = z;
    pVertices[2].rhw = 1.0f;
    pVertices[0].u = u1;
    pVertices[0].v = v1;
    pVertices[1].u = u2;
    pVertices[1].v = v1;
    pVertices[2].u = u2;
    pVertices[2].v = v2;
    cockpit_vtx += 3;
    pVertices += 3;
    pVertices[0].x = x1;
    pVertices[0].y = y1;
    pVertices[0].z = z;
    pVertices[0].rhw = 1.0f;
    pVertices[1].x = x2;
    pVertices[1].y = y2;
    pVertices[1].z = z;
    pVertices[1].rhw = 1.0f;
    pVertices[2].x = x1;
    pVertices[2].y = y2;
    pVertices[2].z = z;
    pVertices[2].rhw = 1.0f;
    pVertices[0].u = u1;
    pVertices[0].v = v1;
    pVertices[1].u = u2;
    pVertices[1].v = v2;
    pVertices[2].u = u1;
    pVertices[2].v = v2;
    cockpit_vtx += 3;
}

#ifdef linux
extern int GL_MSAA;
#endif

void DrawCockpit(RenderDevice* pDevice) {
#ifdef linux
#ifdef GL_MULTISAMPLE
    if (GL_MSAA)
        glDisable(GL_MULTISAMPLE);
#endif
#endif
    /* Cockpit is drawn in ortho space: X in [0, projWidth], Y in [0, 480]. Use base coords + offset, no pixel scaling. */
    float base_width = wideScreen ? static_cast<float>(BASE_WIDTH_WIDESCREEN) : static_cast<float>(BASE_WIDTH_STANDARD);
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    float projWidth = (vp[3] > 0) ? (480.0f * static_cast<float>(vp[2]) / static_cast<float>(vp[3])) : base_width;
    float offsetX = (projWidth - base_width) * 0.5f;

    const bool useCockpitAtlas = (g_pCockpitAtlas != NULL);
    const float cockpitBodyUOffset = kCockpitAtlasUShift;
    g_cockpitAtlasUseExtendedSideUV = useCockpitAtlas;
    g_cockpitAtlasUseSingleSideQuad = useCockpitAtlas;
    g_cockpitAtlasUOffset = 0.0f;

    // Prepare Cockpit drawing
    TRANSFORMEDTEXVERTEX* pVertices;
    cockpit_vtx = 0;
    if (FAILED(pCockpitVB->Lock(0, 0, (void**)&pVertices, 0))) {
        OutputDebugStringW(L"ERROR: Failed to lock cockpit vertex buffer\n");
        return;
    }
    int cockpitBodyVertexStart = 0;
    int cockpitBodyVertexCount = 0;
    int postCockpitBodyVertexStart = 0;
    int cockpitSideVertexStart = 0;
    int cockpitSideVertexCount = 0;
    int cockpitBottomVertexStart = 0;
    int cockpitBottomVertexCount = 0;

    old_leftwheel = (front_left_amount_below_road >> 6);
    float Wide = wideScreen ? COCKPIT_WIDESCREEN_OFFSET : 0.0f;
    const float cockpitSideScreenExtend = useCockpitAtlas ? 2.0f * 100.0f : 0.0f;
    const float cockpitSideY2 = useCockpitAtlas ? 480.0f : (COCKPIT_SIDE_HEIGHT * 2.4f);
    float X1 = (Wide + COCKPIT_WHEEL_LEFT_OFFSET) * 2 + offsetX,
          X2 = ((Wide + COCKPIT_WHEEL_LEFT_OFFSET) * 2 + 2 * COCKPIT_WHEEL_WIDTH) + offsetX;
    float Y1 = (480.0f - COCKPIT_WHEEL_HEIGHT * 2.4f - COCKPIT_WHEEL_BOTTOM_GAP * 2.4f),
          Y2 = (480.0f - COCKPIT_WHEEL_BOTTOM_GAP * 2.4f);
    Y1 -= old_leftwheel;
    Y2 -= old_leftwheel;
    const int leftWheelPhase = static_cast<int>((static_cast<unsigned int>(leftwheel_angle) >> 16) % 6u);
    const int leftWheelFrame = 5 - leftWheelPhase;
    AddQuad(pVertices, X1, Y1, X2, Y2, 0.8f, eWheel0 + leftWheelFrame, 0, 1);
    old_rightwheel = (front_right_amount_below_road >> 6);
    X1 = (Wide * 2.f + 640.f - COCKPIT_WHEEL_LEFT_OFFSET * 2.f - COCKPIT_WHEEL_WIDTH * 2) + offsetX,
    X2 = (Wide * 2.f + 640.f - COCKPIT_WHEEL_LEFT_OFFSET * 2.f) + offsetX;
    Y1 = (480.0f - COCKPIT_WHEEL_HEIGHT * 2.4f - COCKPIT_WHEEL_BOTTOM_GAP * 2.4f),
    Y2 = (480.0f - COCKPIT_WHEEL_BOTTOM_GAP * 2.4f);
    Y1 -= old_rightwheel;
    Y2 -= old_rightwheel;
    const int rightWheelPhase = static_cast<int>((static_cast<unsigned int>(rightwheel_angle) >> 16) % 6u);
    const int rightWheelFrame = 5 - rightWheelPhase;
    AddQuad(pVertices, X1, Y1, X2, Y2, 0.8f, eWheel0 + rightWheelFrame, 1, 1);

    int engineFrame = eEngine;
    if (boost_activated) {
        static int frame = 0;
        if (!bPaused)
            frame = (frame + 1) % 16;
        const int engineframes[8] = {0, 0, 0, 1, 2, 2, 2, 1};
        engineFrame = eEngineFlames0 + engineframes[frame >> 1];
    }
    AddQuad(pVertices, (Wide + COCKPIT_ENGINE_X_OFFSET) * 2.0f + offsetX, COCKPIT_ENGINE_Y_OFFSET * 2.4f,
            (Wide + COCKPIT_ENGINE_X_OFFSET + COCKPIT_ENGINE_WIDTH) * 2.0f + offsetX,
            (COCKPIT_ENGINE_Y_OFFSET + COCKPIT_ENGINE_HEIGHT) * 2.4f, 0.89f, engineFrame, 0, 1);
    cockpitBodyVertexStart = cockpit_vtx;
    g_cockpitAtlasUOffset = cockpitBodyUOffset;
    //AddQuad(pVertices, (Wide + COCKPIT_TOP_X_OFFSET) * 2.f + offsetX, 0.0f,
    //        (Wide + COCKPIT_TOP_X_OFFSET + COCKPIT_TOP_WIDTH) * 2.f + offsetX, COCKPIT_TOP_HEIGHT * 2.4f, 0.9f,
    //        (bSuperLeague) ? eCockpitTop2 : eCockpitTop, 0, 1);
    cockpitSideVertexStart = cockpit_vtx;
    if (useCockpitAtlas) {
        AddQuad(pVertices, Wide * 2.f + 0.0f + offsetX - cockpitSideScreenExtend, 0.0f,
                (640.0f + Wide * 2.f) + offsetX + cockpitSideScreenExtend, cockpitSideY2, 0.9f,
                (bSuperLeague) ? eCockpitLeft2 : eCockpitLeft, 0, 1);
    } else {
        AddQuad(pVertices, Wide * 2.f + 0.0f + offsetX - cockpitSideScreenExtend, 0.0f,
                (Wide + COCKPIT_TOP_X_OFFSET) * 2.f + offsetX,
                cockpitSideY2, 0.9f, (bSuperLeague) ? eCockpitLeft2 : eCockpitLeft, 0, 1);
        AddQuad(pVertices, (Wide + COCKPIT_RIGHT_X_OFFSET) * 2.f + offsetX, 0.0f,
                (640.0f + Wide * 2.f) + offsetX + cockpitSideScreenExtend,
                cockpitSideY2, 0.9f, (bSuperLeague) ? eCockpitRight2 : eCockpitRight, 0, 1);
    }
    cockpitSideVertexCount = cockpit_vtx - cockpitSideVertexStart;
    cockpitBottomVertexStart = cockpit_vtx;
    //AddQuad(pVertices, Wide * 2 + 0.0f + offsetX, COCKPIT_SIDE_HEIGHT * 2.4f, (640.0f + Wide * 2.f) + offsetX,
    //        480.0f, 0.9f, (bSuperLeague) ? eCockpitBottom2 : eCockpitBottom, 0, 1);
    cockpitBottomVertexCount = cockpit_vtx - cockpitBottomVertexStart;
    cockpitBodyVertexCount = cockpit_vtx - cockpitBodyVertexStart;
    g_cockpitAtlasUOffset = 0.0f;
    postCockpitBodyVertexStart = cockpit_vtx;
    if (new_damage) {
        // cracking... width is 238, offset is 41 (in 320x200 screen space)
        float dam = static_cast<float>(new_damage);
        if (dam > COCKPIT_TOP_WIDTH)
            dam = COCKPIT_TOP_WIDTH;
        float damX1 = (Wide + COCKPIT_TOP_X_OFFSET) * 2.0f + offsetX,
              damX2 = (Wide + COCKPIT_TOP_X_OFFSET + dam) * 2.0f + offsetX;
        float damY1 = 0.0f, damY2 = 0.0f + COCKPIT_DAMAGE_HEIGHT * 2.4f;
        AddQuad(pVertices, damX1, damY1, damX2, damY2, 0.91f, (bSuperLeague) ? eCracking2 : eCracking, 0,
                dam / COCKPIT_TOP_WIDTH);
    }
    for (int i = 0; i < nholes; i++) {
        float holeX1 = (Wide + COCKPIT_HOLE_X_OFFSET + COCKPIT_HOLE_SPACING * i) * 2 + offsetX,
              holeX2 = holeX1 + COCKPIT_HOLE_WIDTH * 2.0f;
        float holeY1 = 0.0f, holeY2 = 0.0f + COCKPIT_DAMAGE_HEIGHT * 2.4f;
        AddQuad(pVertices, holeX1, holeY1, holeX2, holeY2, 0.95f, (bSuperLeague) ? eHole2 : eHole, 0, 1);
    }

    pCockpitVB->Unlock();

    // Prepare speedbar
    if (old_speedbar != CalculateDisplaySpeed()) {
        old_speedbar = CalculateDisplaySpeed();
        TRANSFORMEDCOLVERTEX* pSpeedVertices;
        if (FAILED(pSpeedBarCB->Lock(0, 0, (void**)&pSpeedVertices, 0))) {
            OutputDebugStringW(L"ERROR: Failed to lock speed bar vertex buffer\n");
            return;
        }
        float speedX1 = (Wide * 2.f + COCKPIT_SPEEDBAR_X_OFFSET) + offsetX,
              speedX2 =
                  (Wide * 2.f + COCKPIT_SPEEDBAR_X_OFFSET +
                   ((old_speedbar > COCKPIT_SPEEDBAR_MAX) ? (old_speedbar - COCKPIT_SPEEDBAR_MAX) : old_speedbar) /
                       static_cast<float>(COCKPIT_SPEEDBAR_MAX) * COCKPIT_SPEEDBAR_WIDTH) +
                  offsetX;
        float speedY1 = (480.0f - COCKPIT_SPEEDBAR_Y_OFFSET),
              speedY2 = (480.0f - COCKPIT_SPEEDBAR_Y_OFFSET + COCKPIT_SPEEDBAR_HEIGHT);
#ifdef linux
#define SPEEDCOL1 0xff00ffff // ABGR
#define SPEEDCOL2 0xff00ccff // ABGR
#else
#define SPEEDCOL1 0xffffff00 // ARGB
#define SPEEDCOL2 0xffffcc00 // ARGB
#endif
        pSpeedVertices[0].x = speedX1;
        pSpeedVertices[0].y = speedY1;
        pSpeedVertices[0].z = 1.0f;
        pSpeedVertices[0].rhw = 1.0f;
        pSpeedVertices[0].color = (old_speedbar > COCKPIT_SPEEDBAR_MAX) ? SPEEDCOL2 : SPEEDCOL1;
        pSpeedVertices[1].x = speedX2;
        pSpeedVertices[1].y = speedY1;
        pSpeedVertices[1].z = 1.0f;
        pSpeedVertices[1].rhw = 1.0f;
        pSpeedVertices[1].color = (old_speedbar > COCKPIT_SPEEDBAR_MAX) ? SPEEDCOL2 : SPEEDCOL1;
        pSpeedVertices[2].x = speedX2;
        pSpeedVertices[2].y = speedY2;
        pSpeedVertices[2].z = 1.0f;
        pSpeedVertices[2].rhw = 1.0f;
        pSpeedVertices[2].color = (old_speedbar > COCKPIT_SPEEDBAR_MAX) ? SPEEDCOL2 : SPEEDCOL1;
        pSpeedVertices[3].x = speedX1;
        pSpeedVertices[3].y = speedY2;
        pSpeedVertices[3].z = 1.0f;
        pSpeedVertices[3].rhw = 1.0f;
        pSpeedVertices[3].color = (old_speedbar > COCKPIT_SPEEDBAR_MAX) ? SPEEDCOL2 : SPEEDCOL1;
        pSpeedBarCB->Unlock();
    }

    pDevice->SetRenderState(RS_ZENABLE, FALSE);
    pDevice->SetRenderState(RS_CULLMODE, CULL_NONE);

    pDevice->SetRenderState(RS_ALPHABLENDENABLE, TRUE);
    pDevice->SetRenderState(RS_SRCBLEND, BLEND_SRCALPHA);
    pDevice->SetRenderState(RS_DESTBLEND, BLEND_INVSRCALPHA);

    pDevice->SetTextureStageState(0, TSS_ALPHAOP, TOP_BLENDDIFFUSEALPHA);
    pDevice->SetTextureStageState(0, TSS_ALPHAARG1, TA_TEXTURE);
    pDevice->SetTextureStageState(0, TSS_ALPHAARG2, TA_DIFFUSE);
    pDevice->SetTextureStageState(0, TSS_COLOROP, TOP_SELECTARG1);
    pDevice->SetTextureStageState(0, TSS_COLORARG1, TA_TEXTURE);
    pDevice->SetTextureStageState(1, TSS_COLOROP, TOP_DISABLE);
#ifdef WIN32
    pDevice->SetSamplerState(0, SAMP_ADDRESSU, TADDRESS_CLAMP);
    pDevice->SetSamplerState(0, SAMP_ADDRESSV, TADDRESS_CLAMP);
#endif
    // Draw cockpit in three ranges so only cockpit body uses atlas2.
    // Cockpit should use point filtering (pixel sharp), not bilinear.
    pDevice->SetStreamSource(0, pCockpitVB, 0, sizeof(TRANSFORMEDTEXVERTEX));

    pDevice->SetFVF(FVF_TRANSFORMEDTEXVERTEX);
    if (cockpitBodyVertexStart > 0) {
        pDevice->SetTexture(0, g_pAtlas);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        pDevice->DrawPrimitive(PT_TRIANGLELIST, 0, cockpitBodyVertexStart / 3);
    }

    if (cockpitBodyVertexCount > 0) {
        GpuTexture* cockpitBodyTexture = useCockpitAtlas ? g_pCockpitAtlas : g_pAtlas;
        const int cockpitPostSideVertexStart = cockpitSideVertexStart + cockpitSideVertexCount;
        if (cockpitSideVertexStart > cockpitBodyVertexStart) {
            pDevice->SetTexture(0, cockpitBodyTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            pDevice->DrawPrimitive(PT_TRIANGLELIST, cockpitBodyVertexStart,
                                   (cockpitSideVertexStart - cockpitBodyVertexStart) / 3);
        }
        if (cockpitBottomVertexStart > cockpitPostSideVertexStart) {
            pDevice->SetTexture(0, cockpitBodyTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            pDevice->DrawPrimitive(PT_TRIANGLELIST, cockpitPostSideVertexStart,
                                   (cockpitBottomVertexStart - cockpitPostSideVertexStart) / 3);
        }
        if (cockpitBottomVertexCount > 0) {
            pDevice->SetTexture(0, g_pAtlas);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            pDevice->DrawPrimitive(PT_TRIANGLELIST, cockpitBottomVertexStart, cockpitBottomVertexCount / 3);
        }
        const int cockpitBottomVertexEnd = cockpitBottomVertexStart + cockpitBottomVertexCount;
        if (postCockpitBodyVertexStart > cockpitBottomVertexEnd) {
            pDevice->SetTexture(0, cockpitBodyTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            pDevice->DrawPrimitive(PT_TRIANGLELIST, cockpitBottomVertexEnd,
                                   (postCockpitBodyVertexStart - cockpitBottomVertexEnd) / 3);
        }
        if (cockpitSideVertexCount > 0) {
            // Draw side quads last so they appear above the rest of cockpit body pieces.
            GpuTexture* sideTexture = useCockpitAtlas ? g_pCockpitAtlas : g_pAtlas;
            pDevice->SetTexture(0, sideTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            pDevice->DrawPrimitive(PT_TRIANGLELIST, cockpitSideVertexStart, cockpitSideVertexCount / 3);
        }
    }

    if (cockpit_vtx > postCockpitBodyVertexStart) {
        pDevice->SetTexture(0, g_pAtlas);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        pDevice->DrawPrimitive(PT_TRIANGLELIST, postCockpitBodyVertexStart, (cockpit_vtx - postCockpitBodyVertexStart) / 3);
    }

    // Draw Speed bar
    pDevice->SetTextureStageState(0, TSS_COLOROP, TOP_DISABLE);
    pDevice->SetStreamSource(0, pSpeedBarCB, 0, sizeof(TRANSFORMEDCOLVERTEX));

    pDevice->SetFVF(FVF_TRANSFORMEDCOLVERTEX);
    pDevice->DrawPrimitive(PT_TRIANGLEFAN, 0, 2); // 3 points per triangle

    // Restore default filtering for non-cockpit atlas usage.
    pDevice->SetTexture(0, g_pAtlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    pDevice->SetRenderState(RS_ALPHABLENDENABLE, FALSE);
    pDevice->SetRenderState(RS_ZENABLE, TRUE);
    pDevice->SetRenderState(RS_ALPHABLENDENABLE, FALSE);
    //pDevice->SetTextureStageState(0, TSS_COLOROP, TOP_DISABLE);
#ifdef linux
#ifdef GL_MULTISAMPLE
    if (GL_MSAA)
        glEnable(GL_MULTISAMPLE);
#endif
#endif
}
