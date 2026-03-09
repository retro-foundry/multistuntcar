#ifdef linux
#include "platform_sdl_gl.h"
// use a light version of stb_image
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

extern int wideScreen;

const char* BitMapRessourceName(const char* name) {
    static const char* resname[] = {
        "RoadYellowDark", "RoadYellowLight", "RoadRedDark", "RoadRedLight", "RoadBlack", "RoadWhite", 0};
    static const char* filename[] = {"data/Bitmap/RoadYellowDark.bmp",
                                     "data/Bitmap/RoadYellowLight.bmp",
                                     "data/Bitmap/RoadRedDark.bmp",
                                     "data/Bitmap/RoadRedLight.bmp",
                                     "data/Bitmap/RoadBlack.bmp",
                                     "data/Bitmap/RoadWhite.bmp",
                                     0};

    int i = 0;
    while (resname[i] && strcmp(resname[i], name))
        i++;
    if (filename[i] == 0)
        return name;
    return filename[i];
}

void GpuTexture::LoadTexture(const char* name) {
    if (texID)
        glDeleteTextures(1, &texID);
    glGenTextures(1, &texID);
    int x, y, n;
    unsigned char* img = stbi_load(BitMapRessourceName(name), &x, &y, &n, 0);
    if (!img) {
        printf("Warning, image \"%s\" => \"%s\" not loaded\n", name, BitMapRessourceName(name));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        return;
    }
    GLint intfmt = n;
    GLenum fmt = GL_RGBA;
    switch (intfmt) {
    case 1:
        fmt = GL_ALPHA;
        break;
    case 3: // no alpha channel
        fmt = GL_RGB;
        break;
    case 4: // contains an alpha channel
        fmt = GL_RGBA;
        break;
    }
    w2 = w = x;
    h2 = h = y;
    // will handle non-pot2 texture later? or resize the texture to POT?
    /*w2 = NP2(w);
    h2 = NP2(h);
    wf = (float)w2 / (float)w;
    hf = (float)h2 / (float)h;*/
    Bind();
    // ugly... Just blindly load the texture without much check!
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, intfmt, w2, h2, 0, fmt, GL_UNSIGNED_BYTE, NULL);
    // simple and hugly way to make the texture upside down...
    int pitch = y * n;
    for (int i = 0; i < h; i++) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (h - 1) - i, w, 1, fmt, GL_UNSIGNED_BYTE, img + (pitch * i));
    }
    UnBind();
    if (img)
        free(img);
}

sound_buffer_t* sound_load(void* data, int size, int bits, int sign, int channels, int freq);
sound_source_t* sound_source(sound_buffer_t* buffer);
void sound_play(sound_source_t* s);
void sound_play_looping(sound_source_t* s);
bool sound_is_playing(sound_source_t* s);
void sound_stop(sound_source_t* s);
void sound_release_source(sound_source_t* s);
void sound_release_buffer(sound_buffer_t* s);
void sound_set_frequency(sound_source_t* source, long frequency);
void sound_set_pitch(sound_source_t* s, float pitch);
void sound_volume(sound_source_t* s, long decibels);
void sound_pan(sound_source_t* s, long pan);
void sound_position(sound_source_t* s, float x, float y, float z, float min_distance, float max_distance);

void sound_set_position(sound_source_t* s, long newpos);
long sound_get_position(sound_source_t* s);

int npot(int n) {
    int i = 1;
    while (i < n)
        i <<= 1;
    return i;
}

IDirectSoundBuffer8::IDirectSoundBuffer8() {
    source = NULL;
    buffer = NULL;
    wav_bits = 8;
    wav_sign = 0;
    wav_channels = 1;
    wav_freq = 11025;
}

HRESULT IDirectSoundBuffer8::SetVolume(LONG lVolume) {
    if (!source)
        return DSERR_GENERIC;
    sound_volume(source, lVolume);
    return DS_OK;
}

HRESULT IDirectSoundBuffer8::Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags) {
    if (!source)
        return DSERR_GENERIC;
    if (dwFlags & DSBPLAY_LOOPING)
        sound_play_looping(source);
    else
        sound_play(source);
    return DS_OK;
}

HRESULT IDirectSoundBuffer8::SetFrequency(DWORD dwFrequency) {
    if (!source)
        return DSERR_GENERIC;
    sound_set_frequency(source, dwFrequency);
    return DS_OK;
}

HRESULT IDirectSoundBuffer8::SetCurrentPosition(DWORD dwNewPosition) {
    if (!source)
        return DSERR_GENERIC;
    sound_set_position(source, dwNewPosition);
    return DS_OK;
}

HRESULT IDirectSoundBuffer8::GetCurrentPosition(LPDWORD pdwCurrentPlayCursor, LPDWORD pdwCurrentWriteCursor) {
    if (!source)
        return DSERR_GENERIC;
    if (pdwCurrentPlayCursor)
        *pdwCurrentPlayCursor = sound_get_position(source);
    return DS_OK;
}

bool IDirectSoundBuffer8::IsPlaying() const {
    return source ? sound_is_playing(source) : false;
}

HRESULT IDirectSoundBuffer8::Stop() {
    if (!source)
        return DSERR_GENERIC;
    sound_stop(source);
    return DS_OK;
}

HRESULT IDirectSoundBuffer8::SetPan(LONG lPan) {
    if (!source)
        return DSERR_GENERIC;
    sound_pan(source, lPan);
    return DS_OK;
}

IDirectSoundBuffer8::~IDirectSoundBuffer8() {
    if (buffer)
        Release();
}

HRESULT IDirectSoundBuffer8::Release() {
    if (source) {
        sound_release_source(source);
        source = NULL;
    }
    if (buffer) {
        sound_release_buffer(buffer);
        buffer = NULL;
    }
    return S_OK;
}

HRESULT IDirectSoundBuffer8::Lock(DWORD dwOffset, DWORD dwBytes, LPVOID* ppvAudioPtr1, LPDWORD pdwAudioBytes1,
                                  LPVOID* ppvAudioPtr2, LPDWORD pdwAudioBytes2, DWORD dwFlags) {
    if (dwOffset != 0)
        return E_FAIL;
    *ppvAudioPtr2 = NULL;
    *pdwAudioBytes2 = 0;
    *ppvAudioPtr1 = malloc(dwBytes);
    *pdwAudioBytes1 = dwBytes;
    return S_OK;
}
HRESULT IDirectSoundBuffer8::Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1, LPVOID pvAudioPtr2, DWORD dwAudioBytes2) {
    if (dwAudioBytes2 != 0)
        return E_FAIL;
    if (source || buffer)
        Release();
    buffer = sound_load(pvAudioPtr1, dwAudioBytes1, wav_bits, wav_sign, wav_channels, wav_freq);
    source = sound_source(buffer);
    free(pvAudioPtr1);
    return S_OK;
}

HRESULT IDirectSound8::CreateSoundBuffer(LPCDSBUFFERDESC pcDSBufferDesc, LPDIRECTSOUNDBUFFER* ppDSBuffer,
                                         LPUNKNOWN pUnkOuter) {
    IDirectSoundBuffer8* tmp = new IDirectSoundBuffer8();
    if (pcDSBufferDesc && pcDSBufferDesc->lpwfxFormat) {
        LPWAVEFORMATEX wfx = pcDSBufferDesc->lpwfxFormat;
        tmp->wav_bits = (int)wfx->wBitsPerSample;
        tmp->wav_sign = (wfx->wBitsPerSample == 16) ? 1 : 0;
        tmp->wav_channels = (int)wfx->nChannels;
        tmp->wav_freq = (int)wfx->nSamplesPerSec;
    }
    *ppDSBuffer = tmp;
    return S_OK;
}

HRESULT DirectSoundCreate8(LPCGUID lpcGuidDevice, LPDIRECTSOUND8* ppDS8, LPUNKNOWN pUnkOuter) {
    *ppDS8 = new IDirectSound8();
    return DS_OK;
}

/*
 * Matrix
*/
// Try to keep everything column-major to make OpenGL happy...

glm::mat4* mat4Identity(glm::mat4* pOut) {
#ifdef USEGLM
    *pOut = glm::mat4(1.0f);
#else
    set_identity(pOut->m);
#endif
    return pOut;
}

glm::mat4* mat4RotationX(glm::mat4* pOut, float angle) {
#ifdef USEGLM
    *pOut = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(1.0f, 0.0f, 0.0f));
#else
    matrix_rot(angle, 1.0f, 0.0f, 0.0f, pOut->m);
#endif
    return pOut;
}
glm::mat4* mat4RotationY(glm::mat4* pOut, float angle) {
#ifdef USEGLM
    *pOut = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
#else
    matrix_rot(angle, 0.0f, 1.0f, 0.0f, pOut->m);
#endif
    return pOut;
}

glm::mat4* mat4RotationZ(glm::mat4* pOut, float angle) {
#ifdef USEGLM
    *pOut = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));
#else
    matrix_rot(angle, 0.0f, 0.0f, 1.0f, pOut->m);
#endif
    return pOut;
}

glm::mat4* mat4Translation(glm::mat4* pOut, float x, float y, float z) {
#ifdef USEGLM
    *pOut = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
#else
    matrix_trans(x, y, z, pOut->m);
#endif
    return pOut;
}

glm::mat4* mat4Scaling(glm::mat4* pOut, float sx, float sy, float sz) {
#ifdef USEGLM
    *pOut = glm::translate(glm::mat4(1.0f), glm::vec3(sx, sy, sz));
#else
    matrix_scale(sx, sy, sz, pOut->m);
#endif
    return pOut;
}

glm::mat4* mat4Multiply(glm::mat4* pOut, const glm::mat4* pM1, const glm::mat4* pM2) {
#ifdef USEGLM
    *pOut = (*pM2) * (*pM1); // reverse order for column-major OpenGL
#else
    matrix_mul(pM1->m, pM2->m, pOut->m);
#endif
    return pOut;
}

glm::mat4* mat4LookAt(glm::mat4* pOut, const glm::vec3* pEye, const glm::vec3* pAt, const glm::vec3* pUp) {
#ifdef USEGLM
    glm::vec3 eye = *pEye;
    glm::vec3 at = *pAt;
    glm::vec3 up = *pUp;
    *pOut = glm::lookAt(eye, at, up);
#else
    matrix_lookat((float*)pEye, (float*)pAt, (float*)pUp, pOut->m);
#endif
    return pOut;
}

glm::mat4* mat4PerspectiveFov(glm::mat4* pOut, float fovy, float Aspect, float zn, float zf) {
#ifdef USEGLM
    float fw, fh;
    fh = tanf(fovy / 2.0f) * zn;
    fw = fh * Aspect;
    *pOut = glm::frustum(-fw, +fw, +fh, -fh, zn, zf);
#else
    const float ymax = zn * tanf(fovy * 0.5f);
    const float xmax = ymax * Aspect;
    const float temp = 2.0f * zn;
    const float temp2 = 2.0f * xmax;
    const float temp3 = 2.0f * ymax;
    const float temp4 = zf - zn;
    pOut->m[0] = temp / temp2;
    pOut->m[1] = 0.0f;
    pOut->m[2] = 0.0f;
    pOut->m[3] = 0.0f;
    pOut->m[4] = 0.0f;
    pOut->m[5] = temp / temp3;
    pOut->m[6] = 0.0f;
    pOut->m[7] = 0.0f;
    pOut->m[8] = 0.0f;
    pOut->m[9] = 0.0f;
    pOut->m[10] = zf / temp4;
    pOut->m[11] = 1.0f;
    pOut->m[12] = 0.0f;
    pOut->m[13] = 0.0f;
    pOut->m[14] = (zn * zf) / (zn - zf);
    pOut->m[15] = 0.0f;
#endif
#endif
    return pOut;
}

void RenderDevice::ActivateWorldMatrix() {
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
#ifdef USEGLM
    glLoadMatrixf(glm::value_ptr(mInv * mProj * mView * mWorld));
#else
    float m[16];
    matrix_mul(mProj.m, mView.m, m);
    matrix_mul(m, mWorld.m, m);
    glLoadMatrixf(m);
#endif
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
}
void RenderDevice::DeactivateWorldMatrix() {
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

uint32_t GetStrideFromFVF(DWORD fvf) {
    uint32_t stride = 0;
    if (fvf & FVF_DIFFUSE)
        stride += sizeof(DWORD);
    if (fvf & FVF_NORMAL)
        stride += 3 * sizeof(float);
    if (fvf & FVF_XYZ)
        stride += 3 * sizeof(float);
    if (fvf & FVF_XYZRHW)
        stride += 4 * sizeof(float);
    if (fvf & FVF_XYZW)
        stride += 4 * sizeof(float);
    if (fvf & FVF_TEX0)
        stride += 2 * sizeof(float);

    return stride;
}

// RenderDevice
RenderDevice::RenderDevice() {
    for (int i = 0; i < 8; i++) {
        colorop[i] = 0;
        colorarg1[i] = 0;
        colorarg2[i] = 0;
        alphaop[i] = 0;
    }
#ifdef USEGLM
    mView = glm::mat4(1.0f);
    mWorld = glm::mat4(1.0f);
    mProj = glm::mat4(1.0f);
    mText = glm::mat4(1.0f);
    mInv = glm::mat4(-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, +1, 0, 0, 0, 0, 1);
#else
    set_identity(mView.m);
    set_identity(mWorld.m);
    set_identity(mProj.m);
    set_identity(mText.m);
#endif
}

RenderDevice::~RenderDevice() {}

HRESULT RenderDevice::SetTransform(TransformState State, glm::mat4* pMatrix) {
    switch (State) {
    case TS_VIEW:
        mView = *pMatrix;
        break;
    case TS_WORLD:
        mWorld = *pMatrix;
        break;
    case TS_PROJECTION:
        mProj = *pMatrix;
        break;
    case TS_TEXTURE0:
    case TS_TEXTURE1:
    case TS_TEXTURE2:
    case TS_TEXTURE3:
    case TS_TEXTURE4:
        mText = *pMatrix;
        glMatrixMode(GL_TEXTURE);
#ifdef USEGLM
        glLoadMatrixf(glm::value_ptr(mText));
#else
        glLoadMatrixf(mText.m);
#endif
        break;
    default:
        printf("Unhandled Matrix SetTransform(%X, %p)\n", State, pMatrix);
    }
    return S_OK;
}

HRESULT RenderDevice::GetTransform(TransformState State, glm::mat4* pMatrix) {
    switch (State) {
    case TS_VIEW:
        *pMatrix = mView;
        break;
    case TS_PROJECTION:
        *pMatrix = mProj;
        break;
    case TS_WORLD:
        *pMatrix = mWorld;
        break;
    case TS_TEXTURE0:
    case TS_TEXTURE1:
    case TS_TEXTURE2:
    case TS_TEXTURE3:
    case TS_TEXTURE4:
        *pMatrix = mText;
        break;
    default:
        printf("Unhandled GetTransform(%X, %p)\n", State, pMatrix);
    }
    return S_OK;
}

HRESULT RenderDevice::SetRenderState(RenderStateType State, int Value) {
    switch (State) {
    case RS_ZENABLE:
        if (Value) {
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
        } else {
            glDepthMask(GL_FALSE);
            glDisable(GL_DEPTH_TEST);
        }
        break;
    case RS_CULLMODE:
        switch (Value) {
        case CULL_NONE:
            glDisable(GL_CULL_FACE);
            break;
        case CULL_CW:
            glFrontFace(GL_CW);
            glCullFace(GL_FRONT);
            glEnable(GL_CULL_FACE);
            break;
        case CULL_CCW:
            glFrontFace(GL_CCW);
            glCullFace(GL_FRONT);
            glEnable(GL_CULL_FACE);
            break;
        }
        break;
    case RS_SRCBLENDALPHA:
        break;
    case RS_DESTBLENDALPHA:
        break;
    case RS_ALPHABLENDENABLE:
        if (Value) {
            glEnable(GL_ALPHA_TEST);
        } else {
            glDisable(GL_ALPHA_TEST);
        }
        break;
    case RS_SRCBLEND:
        break;
    case RS_DESTBLEND:
        break;
    default:
        printf("Unhandled Render State %X=%d\n", State, Value);
    }
    return S_OK;
}

HRESULT RenderDevice::DrawPrimitive(PrimitiveType PrimitiveType, UINT StartVertex, UINT PrimitiveCount) {
    const GLenum primgl[] = {GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};
    const GLenum prim1[] = {1, 2, 1, 3, 1, 1};
    const GLenum prim2[] = {0, 0, 1, 0, 2, 2};
    if (PrimitiveType < PT_POINTLIST || PrimitiveType > PT_TRIANGLEFAN) {
        printf("Unsupported Primitive %d\n", PrimitiveType);
        return E_FAIL;
    }
    if (PrimitiveCount == 0)
        return S_OK;

    GLenum mode = primgl[PrimitiveType - 1];
    bool transf = ((fvf & FVF_XYZRHW) == 0);
    char* ptr = (char*)buffer[0]->buffer.buffer;
    bool vtx = false, col = false, tex0 = false, tex1 = false;
    if (fvf & FVF_XYZ) {
        glVertexPointer(3, GL_FLOAT, stride[0], ptr);
        ptr += 3 * sizeof(float);
        vtx = true;
    };
    if (fvf & FVF_XYZW) {
        glVertexPointer(4, GL_FLOAT, stride[0], ptr);
        ptr += 4 * sizeof(float);
        vtx = true;
    };
    if (fvf & FVF_XYZRHW) {
        glVertexPointer(2, GL_FLOAT, stride[0], ptr);
        ptr += 4 * sizeof(float);
        vtx = true;
    };
    if (fvf & FVF_DIFFUSE) {
        glColorPointer(4, GL_UNSIGNED_BYTE, stride[0], ptr);
        ptr += sizeof(DWORD);
        col = true;
    }
    if (fvf & FVF_TEX0) {
        glTexCoordPointer(2, GL_FLOAT, stride[0], ptr);
        ptr += 2 * sizeof(float);
        tex0 = true;
    }
    if (fvf & FVF_TEX1) {
        glTexCoordPointer(2, GL_FLOAT, stride[0], ptr);
        ptr += 2 * sizeof(float);
        tex1 = true;
    }

    if (vtx)
        glEnableClientState(GL_VERTEX_ARRAY);
    else
        glDisableClientState(GL_VERTEX_ARRAY);

    if ((colorop[0] == TOP_SELECTARG1) && (colorarg1[0] != TA_DIFFUSE))
        col = false;
    if ((colorop[0] == TOP_SELECTARG2) && (colorarg2[0] != TA_DIFFUSE))
        col = false;
    /*    if((colorop[0]==D3DTOP_SELECTARG1) && (colorarg1[0]==D3DTA_TEXTURE)) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else*/
    {
        glDisable(GL_BLEND);
    }

    if (col)
        glEnableClientState(GL_COLOR_ARRAY);
    else {
        glDisableClientState(GL_COLOR_ARRAY);
        glColor3f(1.0f, 1.0f, 1.0f);
    }

    if (tex0 || tex1) {
        if (colorop[0] <= TOP_DISABLE) {
            glDisable(GL_TEXTURE_2D);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisable(GL_BLEND);
        } else {
            glEnable(GL_TEXTURE_2D);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnable(GL_BLEND);
        }
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    if (transf)
        ActivateWorldMatrix();

    glDrawArrays(mode, StartVertex, prim1[PrimitiveType - 1] * PrimitiveCount + prim2[PrimitiveType - 1]);

    if (transf)
        DeactivateWorldMatrix();
    return S_OK;
}

HRESULT RenderDevice::SetTextureStageState(DWORD Stage, TextureStageStateType Type, DWORD Value) {
    if (Stage > 7) {
        printf("Unhandled SetTextureStageState(%d, 0x%X, 0x%X)\n", Stage, Type, Value);
        return S_OK;
    }

    switch (Type) {
    case TSS_COLOROP:
        colorop[Stage] = Value;
        break;
    case TSS_COLORARG1:
        colorarg1[Stage] = Value;
        break;
    case TSS_COLORARG2:
        colorarg2[Stage] = Value;
        break;
    case TSS_ALPHAOP:
        break;
    case TSS_ALPHAARG1:
        break;
    case TSS_ALPHAARG2:
        break;
    default:
        printf("Unhandled SetTextureStageState(%d, 0x%X, 0x%X)\n", Stage, Type, Value);
    }

    /*    glActiveTexture(GL_TEXTURE0+0);
    glClientActiveTexture(GL_TEXTURE0+0);*/

    return S_OK;
}

HRESULT RenderDevice::SetSamplerState(DWORD Sampler, SamplerStateType Type, DWORD Value) {
    GLint wrap = GL_REPEAT;
    switch (Value) {
    case TADDRESS_CLAMP:
        wrap = GL_CLAMP_TO_EDGE;
        break;
#ifdef GL_MIRRORED_REPEAT
    case TADDRESS_MIRROR:
        wrap = GL_MIRRORED_REPEAT;
        break;
#endif
    case TADDRESS_WRAP:
    default:
        wrap = GL_REPEAT;
        break;
    }

    switch (Type) {
    case SAMP_ADDRESSU:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        break;
    case SAMP_ADDRESSV:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        break;
    default:
        break;
    }

    return S_OK;
}

HRESULT RenderDevice::SetTexture(DWORD Sampler, GpuTexture* pTexture) {
    (void)Sampler;
    pTexture->Bind();
    return S_OK;
}

HRESULT RenderDevice::Clear(DWORD Count, const ClearRect* pRects, DWORD Flags, COLOR Color, float Z,
                                DWORD Stencil) {
    GLbitfield clearval = 0;
    if (Flags & CLEAR_STENCIL) {
        glClearStencil(Stencil);
        clearval |= GL_STENCIL_BUFFER_BIT;
    }
    if (Flags & CLEAR_ZBUFFER) {
        glClearDepth(Z);
        clearval |= GL_DEPTH_BUFFER_BIT;
    }
    if (Flags & CLEAR_TARGET) {
        float r, g, b, a;
        r = ((Color >> 0) & 0xff) / 255.0f;
        g = ((Color >> 8) & 0xff) / 255.0f;
        b = ((Color >> 16) & 0xff) / 255.0f;
        a = ((Color >> 24) & 0xff) / 255.0f;
        glClearColor(r, g, b, a);
        clearval |= GL_COLOR_BUFFER_BIT;
    }
    if (clearval) {
        GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        if (Count > 0 && pRects) {
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            /* pRects use top-left origin (y down); glScissor uses bottom-left (y up) */
            glEnable(GL_SCISSOR_TEST);
            glScissor((GLint)pRects[0].x1, (GLint)(vp[3] - pRects[0].y2),
                      (GLsizei)(pRects[0].x2 - pRects[0].x1), (GLsizei)(pRects[0].y2 - pRects[0].y1));
        }
        glClear(clearval);
        if (Count > 0 && pRects) {
            if (!scissorEnabled)
                glDisable(GL_SCISSOR_TEST);
        }
    }
    return S_OK;
}

HRESULT RenderDevice::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, PoolType Pool,
                                             VertexBuffer** ppVertexBuffer, HANDLE* pSharedHandle) {
    *ppVertexBuffer = new VertexBuffer(Length, FVF);

    return S_OK;
}

HRESULT RenderDevice::SetStreamSource(UINT StreamNumber, VertexBuffer* pStreamData, UINT OffsetInBytes,
                                          UINT Stride) {
    buffer[StreamNumber] = pStreamData;
    offset[StreamNumber] = OffsetInBytes;
    stride[StreamNumber] = Stride;
    return S_OK;
}

HRESULT RenderDevice::SetFVF(DWORD FVF) {
    fvf = FVF;
    return S_OK;
}

VertexBuffer::VertexBuffer(uint32_t size, uint32_t fvf) {
    buffer.fvf = fvf;
    buffer.buffer = malloc(size);
}

VertexBuffer::~VertexBuffer() { Release(); }

HRESULT VertexBuffer::Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) {
    *ppbData = (void*)((char*)buffer.buffer + OffsetToLock);
    return S_OK;
}

HRESULT VertexBuffer::Unlock() {
    return S_OK;
}

HRESULT VertexBuffer::Release() {
    free(buffer.buffer);
    return S_OK;
}

TextHelper::TextHelper(TTF_Font* font, GLuint sprite, int size) : m_sprite(sprite), m_size(size), m_posx(0), m_posy(0) {
    // set colors
    m_forecol[0] = m_forecol[1] = m_forecol[2] = m_forecol[3] = 1.0f;
    // setup texture
    m_fontsize = TTF_FontHeight(font);
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int w = npot(16 * m_fontsize);
    void* tmp = malloc(w * w * 4);
    memset(tmp, 0, w * w * 4);
    m_sizew = w;
    m_sizeh = w;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_sizew, m_sizeh, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    free(tmp);
    SDL_Color forecol = {255, 255, 255, 255};
    m_inv = 1.0 / (float)m_sizew;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            char text[2] = {(char)(i * 16 + j), 0};
            SDL_Surface* surf = TTF_RenderText_Blended(font, text, forecol);
            if (surf) {
                m_as[i * 16 + j] = surf->w;
                glPixelStorei(GL_UNPACK_ROW_LENGTH, surf->pitch / surf->format->BytesPerPixel);
                glTexSubImage2D(GL_TEXTURE_2D, 0, j * m_fontsize, i * m_fontsize, surf->w,
                                (surf->h >= m_fontsize) ? m_fontsize - 1 : surf->h, GL_RGBA, GL_UNSIGNED_BYTE,
                                surf->pixels);
                SDL_FreeSurface(surf);
            } else {
                m_as[i * 16 + j] = m_fontsize / 2;
            }
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

TextHelper::~TextHelper() { glDeleteTextures(1, &m_texture); }

void TextHelper::SetInsertionPos(int x, int y) {
    m_posx = x;
    m_posy = y;
}

void TextHelper::DrawTextLine(const wchar_t* line) {
    // Draw it
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glColor4fv(m_forecol);
    glBegin(GL_QUADS);
    char ch;
    int i = 0;
    float posx = m_posx;
    while ((ch = line[i])) {
        float col = ch % 16, lin = ch / 16;
        glTexCoord2f((col * m_fontsize + 0) * m_inv, (lin * m_fontsize + 0) * m_inv);
        glVertex2f(posx, m_posy);
        glTexCoord2f((col * m_fontsize + m_as[ch]) * m_inv, (lin * m_fontsize + 0) * m_inv);
        glVertex2f(posx + m_as[ch], m_posy);
        glTexCoord2f((col * m_fontsize + m_as[ch]) * m_inv, (lin * m_fontsize + m_fontsize - 1) * m_inv);
        glVertex2f(posx + m_as[ch], m_posy + m_fontsize);
        glTexCoord2f((col * m_fontsize + 0) * m_inv, (lin * m_fontsize + m_fontsize - 1) * m_inv);
        glVertex2f(posx, m_posy + m_fontsize);
        posx += m_as[ch];
        i++;
    }
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    m_posy += m_size;

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
}

void TextHelper::DrawFormattedTextLine(const wchar_t* line, ...) {
    wchar_t buff[1000];
    va_list args;
    va_start(args, line);
    vswprintf(buff, 1000, line, args);
    DrawTextLine(buff);
    va_end(args);
}

void TextHelper::SetForegroundColor(glm::vec4 clr) {
    m_forecol[0] = clr.r;
    m_forecol[1] = clr.g;
    m_forecol[2] = clr.b;
    m_forecol[3] = clr.a;
}

static RenderDevice* device = NULL;
RenderDevice* GetRenderDevice() {
    if (!device)
        device = new RenderDevice();
    return device;
}

static SurfaceDesc surface_desc = {0};
const SurfaceDesc* GetBackBufferSurfaceDesc() {
    surface_desc.Width = wideScreen ? 800 : 640;
    surface_desc.Height = 480;
    return &surface_desc;
}

DOUBLE GetTimeSeconds() { return ((DOUBLE)SDL_GetTicks()) / 1000.0; }

void ResetRenderEnvironment() {
    // NOTHING?
}
