#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <malloc.h>

extern "C" {
    #include <gccore.h>
    #include <wiiuse/wpad.h>
    #include <ogc/tpl.h>
    #include <network.h>
    #include <ogc/isfs.h>
    #include "textures_tpl.h"
    #include "textures.h"
}

#define DEFAULT_FIFO_SIZE (256 * 1024)
#define LISTEN_PORT       8080
#define RFL_DB_SIZE       0x1F1E0

//---------------------------------------------------------------------------------
// 74-Byte Big-Endian Mii Structure
//---------------------------------------------------------------------------------
#pragma pack(push, 1)
struct RawMiiData {
    u16 invalid : 1, isGirl : 1, month : 4, day : 5, favColor : 4, isFavorite : 1;
    u16 name[10];
    u8  height, weight;
    u8  miiID[4], systemID[4];
    u16 faceShape : 3, skinColor : 3, facialFeature : 4, unk1 : 3, mingleOff : 1, unk2 : 1, downloaded : 1;
    u16 hairType : 7, hairColor : 3, hairPart : 1, unk3 : 5;
    u32 eyebrowType : 5, unk4 : 1, eyebrowRot : 4, unk5 : 6, eyebrowColor : 3, eyebrowSize : 4, eyebrowVert : 5, eyebrowHoriz : 4;
    u32 eyeType : 6, unk6 : 2, eyeRot : 3, eyeVert : 5, eyeColor : 3, unk7 : 1, eyeSize : 3, eyeHoriz : 4, unk8 : 5;
    u16 noseType : 4, noseSize : 4, noseVert : 5, unk9 : 3;
    u16 lipType : 5, lipColor : 2, lipSize : 4, lipVert : 5;
    u16 glassesType : 4, glassesColor : 3, unk10 : 1, glassesSize : 3, glassesVert : 5;
    u16 mustacheType : 2, beardType : 2, facialHairColor : 3, mustacheSize : 4, mustacheVert : 5;
    u16 moleOn : 1, moleSize : 4, moleVert : 5, moleHoriz : 5, unk11 : 1;
    u16 creatorName[10];
};
#pragma pack(pop)

//---------------------------------------------------------------------------------
// Global State & UI Colors
//---------------------------------------------------------------------------------
static void *frameBuffer[2] = { nullptr, nullptr };
static GXRModeObj *rmode = nullptr;

GXTexObj g_fontTex;
GXTexObj g_logoTex;
bool g_texturesLoaded = false;

RawMiiData g_receivedMii = {};
bool g_hasMii = false;
char g_statusMsg[64] = "Waiting for .mii packet...";
alignas(32) static u8 g_db[RFL_DB_SIZE];

const GXColor COL_BG_DARK = { 235, 238, 242, 255 };
const GXColor COL_PANEL   = { 255, 255, 255, 255 };
const GXColor COL_BORDER  = { 75,  90,  125, 255 };
const GXColor COL_WHITE   = { 255, 255, 255, 255 };
const GXColor COL_CYAN    = { 110, 210, 255, 255 };
const GXColor COL_YELLOW  = { 255, 220, 90,  255 };
const GXColor COL_GREEN   = { 60,  220, 100, 255 };
const GXColor COL_RED     = { 230, 65,  65,  255 };
const GXColor COL_MUTED   = { 150, 160, 185, 255 };

//---------------------------------------------------------------------------------
// GX Drawing Helpers
//---------------------------------------------------------------------------------

void drawSolidRect(float x, float y, float w, float h, GXColor c) {
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(x, y);         GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(x + w, y);     GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(x + w, y + h); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(x, y + h);     GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(0.0f, 0.0f);
    GX_End();
}

void drawPanel(float x, float y, float w, float h, GXColor fill, GXColor border) {
    drawSolidRect(x - 2, y - 2, w + 4, h + 4, border);
    drawSolidRect(x, y, w, h, fill);
}

void drawTextureRect(GXTexObj *tex, float x, float y, float w, float h, GXColor tint = COL_WHITE) {
    if (!g_texturesLoaded) return;
    GX_LoadTexObj(tex, GX_TEXMAP0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(x, y);         GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(x + w, y);     GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position2f32(x + w, y + h); GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(x, y + h);     GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(0.0f, 1.0f);
    GX_End();
}

void drawText(float x, float y, float charSize, GXColor color, const char *fmt, ...) {

    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    GX_LoadTexObj(&g_fontTex, GX_TEXMAP0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);

    float cursorX = x;
    float advance = charSize * 0.62f;

    for (int i = 0; buf[i] != '\0'; i++) {
        unsigned char c = static_cast<unsigned char>(buf[i]);
        if (c < 32 || c >= 128) continue;

        int col = c % 16;
        int row = c / 16;

        float u0 = col * (1.0f / 16.0f);
        float v0 = row * (1.0f / 8.0f);
        float u1 = u0 + (1.0f / 16.0f);
        float v1 = v0 + (1.0f / 8.0f);

        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
            GX_Position2f32(cursorX, y);
            GX_Color4u8(color.r, color.g, color.b, color.a);
            GX_TexCoord2f32(u0, v0);

            GX_Position2f32(cursorX + charSize, y);
            GX_Color4u8(color.r, color.g, color.b, color.a);
            GX_TexCoord2f32(u1, v0);

            GX_Position2f32(cursorX + charSize, y + charSize);
            GX_Color4u8(color.r, color.g, color.b, color.a);
            GX_TexCoord2f32(u1, v1);

            GX_Position2f32(cursorX, y + charSize);
            GX_Color4u8(color.r, color.g, color.b, color.a);
            GX_TexCoord2f32(u0, v1);
        GX_End();

        cursorX += advance;
    }
}

//---------------------------------------------------------------------------------
// NAND Mii Plaza Save Logic
//---------------------------------------------------------------------------------
bool saveToMiiPlaza() {
    const char *newName = "NewMii";
    memset(g_receivedMii.name, 0, sizeof(g_receivedMii.name));
    for (int i = 0; newName[i] != '\0'; i++) g_receivedMii.name[i] = newName[i];

    g_receivedMii.invalid = 0;
    g_receivedMii.miiID[0] = (g_receivedMii.miiID[0] & 0x1F) | 0x80;
    if (g_receivedMii.miiID[1] == 0 && g_receivedMii.miiID[2] == 0 && g_receivedMii.miiID[3] == 0) {
        g_receivedMii.miiID[3] = 1;
    }

    if (ISFS_Initialize() != ISFS_OK) return false;
    s32 fd = ISFS_Open("/shared2/menu/FaceLib/RFL_DB.dat", ISFS_OPEN_RW);
    if (fd < 0) {
        ISFS_Deinitialize();
        return false;
    }

    bool ok = false;
    if (ISFS_Read(fd, g_db, RFL_DB_SIZE) == RFL_DB_SIZE) {
        RawMiiData *slots = reinterpret_cast<RawMiiData*>(g_db + 4);
        if (slots[0].systemID[0] != 0 || slots[0].systemID[1] != 0) {
            memcpy(g_receivedMii.systemID, slots[0].systemID, 4);
        }

        int target = -1;
        for (int i = 0; i < 100; i++) {
            if (memcmp(slots[i].miiID, g_receivedMii.miiID, 4) == 0) { target = i; break; }
            if (target == -1 && slots[i].miiID[0] == 0 && slots[i].miiID[1] == 0) target = i;
        }
        if (target == -1) target = 0;
        slots[target] = g_receivedMii;

        u32 crc = 0;
        for (u32 i = 0; i < RFL_DB_SIZE - 2; i++) {
            for (int b = 7; b >= 0; b--)
                crc = ((crc << 1) | ((g_db[i] >> b) & 1)) ^ ((crc & 0x8000) ? 0x1021 : 0);
        }
        for (int b = 0; b < 16; b++) crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
        *reinterpret_cast<u16*>(g_db + RFL_DB_SIZE - 2) = static_cast<u16>(crc);

        ISFS_Seek(fd, 0, SEEK_SET);
        ok = (ISFS_Write(fd, g_db, RFL_DB_SIZE) == RFL_DB_SIZE);
    }
    ISFS_Close(fd);
    ISFS_Deinitialize();
    return ok;
}

//---------------------------------------------------------------------------------
// Main Layout Renderer
//---------------------------------------------------------------------------------
void renderAppUI(bool netActive, const char *localIP) {
    // 1. TOP STATUS BAR
    drawPanel(20, 16, 600, 36, COL_PANEL, COL_BORDER);
    GXColor ledColor = netActive ? COL_GREEN : COL_RED;
    drawSolidRect(32, 24, 20, 20, ledColor);
    drawText(62, 24, 20.0f, COL_WHITE, "NET: %s | WII IP: %s:%d",
             netActive ? "ONLINE" : "OFFLINE", localIP, LISTEN_PORT);

    // 2. FACE PREVIEW AREA
    drawPanel(20, 66, 330, 310, COL_PANEL, COL_BORDER);
    drawText(32, 74, 18.0f, COL_MUTED, "MII PREVIEW");

    // 3. RIGHT-HAND ATTRIBUTE LIST PANEL
    drawPanel(366, 66, 254, 310, COL_PANEL, COL_BORDER);
    drawText(378, 74, 18.0f, COL_CYAN, "MII ATTRIBUTES");
    drawSolidRect(376, 96, 234, 2, COL_BORDER);

    float attrY = 104.0f;
    float lineStep = 20.0f;
    const RawMiiData &m = g_receivedMii;

    drawText(378, attrY, 16.0f, COL_WHITE,  "Gender   : %u", m.isGirl);               attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_WHITE,  "Fav Color: %u", m.favColor);             attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_WHITE,  "Height/Wt: %u/%u", m.height, m.weight);  attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_YELLOW, "Face/Skin: %u / %u", m.faceShape, m.skinColor); attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_YELLOW, "Feature  : %u", m.facialFeature);        attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_YELLOW, "Hair/Col : %u / %u", m.hairType, m.hairColor);  attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_YELLOW, "Brows/Col: %u / %u", m.eyebrowType, m.eyebrowColor); attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_YELLOW, "Eyes/Col : %u / %u", m.eyeType, m.eyeColor);    attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_YELLOW, "Nose/Size: %u / %u", m.noseType, m.noseSize);   attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_YELLOW, "Mouth/Col: %u / %u", m.lipType, m.lipColor);    attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_WHITE,  "Glasses  : %u", m.glassesType);          attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_WHITE,  "Beard/Mst: %u / %u", m.beardType, m.mustacheType); attrY += lineStep;
    drawText(378, attrY, 16.0f, COL_WHITE,  "Mole     : %u", m.moleOn);

    // 4. BOTTOM-LEFT LOGO SPACE
    drawPanel(20, 392, 160, 64, COL_PANEL, COL_BORDER);
    drawTextureRect(&g_logoTex, 20, 392, 160, 64);

    // 5. BOTTOM INSTRUCTION BAR
    drawPanel(196, 392, 424, 64, COL_PANEL, COL_BORDER);
    drawText(212, 402, 20.0f, g_hasMii ? COL_GREEN : COL_MUTED,
             "Press [A] to save Mii to Plaza");
    drawText(212, 428, 16.0f, COL_CYAN, "%s", g_statusMsg);
}

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------
    VIDEO_Init();
    WPAD_Init();

    rmode = VIDEO_GetPreferredMode(nullptr);
    u32 fb = 0;
    u32 first_frame = 1;

    frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

    VIDEO_ClearFrameBuffer(rmode, frameBuffer[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, frameBuffer[1], COLOR_BLACK);

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(frameBuffer[fb]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    // 1. Initialize Network Subsystem (Crucial for real Wii green light!)
    net_init();

    char localIP[16] = "0.0.0.0";
    bool netActive = (if_config(localIP, nullptr, nullptr, true, 20) >= 0);
    s32 sock = -1;
    if (netActive) {
        sock = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock >= 0) {
            u32 nonBlocking = 1;
            net_ioctl(sock, FIONBIO, &nonBlocking);

            struct sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(LISTEN_PORT);
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            if (net_bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
                netActive = false;
            }
        } else {
            netActive = false;
        }
    }

    // 2. Initialize GX Graphics Pipeline
    fb ^= 1;
    void *gp_fifo = memalign(32, DEFAULT_FIFO_SIZE);
    memset(gp_fifo, 0, DEFAULT_FIFO_SIZE);
    GX_Init(gp_fifo, DEFAULT_FIFO_SIZE);

    GX_SetCopyClear(COL_BG_DARK, 0x00FFFFFF);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfbHeight = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfbHeight);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
    GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_CopyDisp(frameBuffer[fb], GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS,  GX_POS_XY,   GX_F32,   0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,   GX_F32,   0);

    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    // 3. Load Textures Safely with TPL
    GX_InvalidateTexAll();
    TPLFile spriteTPL;
    TPL_OpenTPLFromMemory(&spriteTPL, (void *)textures_tpl, textures_tpl_size);
    TPL_GetTexture(&spriteTPL, tex_font, &g_fontTex);
    TPL_GetTexture(&spriteTPL, tex_logo, &g_logoTex);
    GX_LoadTexObj(&g_fontTex, GX_TEXMAP0);

    Mtx44 perspective;
    Mtx GXmodelView2D;
    guOrtho(perspective, 0, 479, 0, 639, 0, 300);
    GX_LoadProjectionMtx(perspective, GX_ORTHOGRAPHIC);

    // 4. Main Loop
    while (SYS_MainLoop()) {
        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);
        if (pressed & WPAD_BUTTON_HOME) break;

        if (netActive && sock >= 0) {
            RawMiiData incoming;
            struct sockaddr_in client;
            u32 clientLen = sizeof(client);
            if (net_recvfrom(sock, &incoming, sizeof(RawMiiData), 0,
                             reinterpret_cast<struct sockaddr*>(&client), &clientLen) == sizeof(RawMiiData)) {
                g_receivedMii = incoming;
                g_hasMii = true;
                snprintf(g_statusMsg, sizeof(g_statusMsg), "Received Mii! Ready to save.");
            }
        }

        if ((pressed & WPAD_BUTTON_A) && g_hasMii) {
            if (saveToMiiPlaza()) {
                g_hasMii = false;
                snprintf(g_statusMsg, sizeof(g_statusMsg), "Saved 'NewMii' to Mii Plaza!");
            } else {
                snprintf(g_statusMsg, sizeof(g_statusMsg), "Error: Could not write RFL_DB.dat");
            }
        }

        GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
        GX_InvVtxCache();
        GX_InvalidateTexAll();

        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

        guMtxIdentity(GXmodelView2D);
        guMtxTransApply(GXmodelView2D, GXmodelView2D, 0.0F, 0.0F, -5.0F);
        GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);

        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        GX_SetZMode(GX_FALSE, GX_LEQUAL, GX_TRUE);

        renderAppUI(netActive, localIP);

        GX_DrawDone();
        GX_SetAlphaUpdate(GX_TRUE);
        GX_SetColorUpdate(GX_TRUE);
        GX_CopyDisp(frameBuffer[fb], GX_TRUE);

        VIDEO_SetNextFramebuffer(frameBuffer[fb]);
        if (first_frame) {
            VIDEO_SetBlack(false);
            first_frame = 0;
        }
        VIDEO_Flush();
        VIDEO_WaitVSync();
        fb ^= 1;
    }

    return 0;
}