/*
 * MII HORROR - prototipo homebrew Wii (libogc + GRRLIB)
 * Esplora le "mappe" stile Wii Sports, nasconditi da Mario.
 * Piu' avanzi, piu' anomalie. Le mappe completate non sono piu' giocabili.
 *
 * Controlli: Nunchuk stick (o croce direzionale) = muovi
 *            B (tenuto, dentro un nascondiglio) = nasconditi
 *            A = conferma / puntatore IR nel menu
 *            HOME = esci
 */
#include <grrlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <fat.h>
#include <wiiuse/wpad.h>
#include "models_data.h"

#define SW 640
#define SH 480
#define NMAPS 5
#define NSPOTS 4

typedef struct { float x, y; } V2;
typedef struct { int x, y, w, h; } Rc;

enum { SC_GAME, SC_MENU, SC_OVER, SC_END };

/* colori RGBA */
#define COL_SKIN  0xF2C9A0FF
#define COL_SPOT  0x2A3A2AFF
#define COL_EXIT  0xDDDD55FF
#define COL_RED   0xCC1111FF
#define COL_BLUE  0x2244CCFF

static const u32 mapColors[NMAPS] = {
    0x2E5E34FF, /* tennis  */
    0x3A2F2AFF, /* bowling */
    0x2F6A3FFF, /* golf    */
    0x3B2A4AFF, /* boxe    */
    0x5A4A2AFF  /* baseball*/
};

static u8 dead[NMAPS];          /* mappe "morte" (gia' completate) */
static int scene = SC_GAME;
static int curMap = 0;
static V2 player, mario, patrolTarget;
static Rc spots[NSPOTS];
static Rc exitR = { 580, 200, 40, 80 };
static int chase = 0, hidden = 0, giveUpTimer = 0, frame = 0;
static V2 lastSeen;
static int anomalyChannels[12];

/* ---------- salvataggio su SD ---------- */
static void saveGame(void) {
    FILE *f = fopen("sd:/miihorror.sav", "wb");
    if (f) { fwrite(dead, 1, NMAPS, f); fclose(f); }
}
static void loadGame(void) {
    FILE *f = fopen("sd:/miihorror.sav", "rb");
    if (f) { fread(dead, 1, NMAPS, f); fclose(f); }
}
static int anomalyLevel(void) {
    int n = 0;
    for (int i = 0; i < NMAPS; i++) n += dead[i];
    return n;
}
static int nextMap(void) {
    for (int i = 0; i < NMAPS; i++) if (!dead[i]) return i;
    return -1;
}

/* ---------- utilita' ---------- */
static float dist(V2 a, V2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}
static int inRect(V2 p, Rc r) {
    return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void pickPatrol(void) {
    patrolTarget.x = 60 + rand() % 500;
    patrolTarget.y = 40 + rand() % 400;
}

static void resetMap(int m) {
    curMap = m;
    srand(m * 7919 + 13);
    for (int i = 0; i < NSPOTS; i++) {
        spots[i].x = 120 + rand() % 380;
        spots[i].y = 60 + rand() % 330;
        spots[i].w = 70; spots[i].h = 50;
    }
    srand(frame + 1);
    player.x = 40; player.y = 240;
    mario.x = 320; mario.y = 240;
    chase = 0; hidden = 0; giveUpTimer = 0;
    pickPatrol();
}

/* ---------- disegno ---------- */
/* ---------- modelli 3D ---------- */
static GRRLIB_texImg *marioTex[MARIO_NTEX];
static float playerFace = 0, marioFace = 0;   /* gradi: +dx = guarda a destra */

/* Se un modello appare girato di lato o di schiena, cambia questi offset (gradi) */
#define MII_YAW_OFFSET   0.0f
#define MARIO_YAW_OFFSET 0.0f

/* Disegna un modello 3D in coordinate schermo (modalita' 2D di GRRLIB).
   px,py = punto dei piedi, h = altezza in pixel, yaw = rotazione, sx/sy = stiramenti (glitch) */
static void drawModel(const Model *m, GRRLIB_texImg **tex, float px, float py,
                      float h, float yaw, float sx, float sy, u8 alpha)
{
    Mtx r, s, t, tmp, mv;
    guMtxScale(s, h * sx, -h * sy, h * sx);   /* y negativo: lo schermo ha y verso il basso */
    guMtxRotDeg(r, 'y', yaw);
    guMtxTrans(t, px, py, -(700.0f - py));    /* piu' in basso = piu' vicino */
    guMtxConcat(r, s, tmp);
    guMtxConcat(t, tmp, mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS,  GX_POS_XYZ, GX_F32,   0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,  GX_F32,   0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

    for (int p = 0; p < m->nparts; p++) {
        const MPart *pt = &m->parts[p];
        if (pt->tex >= 0 && tex && tex[pt->tex]) {
            GRRLIB_SetTexture(tex[pt->tex], 1);
            GX_SetNumTexGens(1);
            GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
            GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        } else {
            GX_SetNumTexGens(1);
            GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
            GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        }
        GX_Begin(GX_TRIANGLES, GX_VTXFMT0, pt->count);
        for (int i = 0; i < pt->count; i++) {
            const MVert *v = &m->v[m->idx[pt->start + i]];
            GX_Position3f32(v->x, v->y, v->z);
            GX_Color1u32((v->col & 0xFFFFFF00u) | alpha);
            GX_TexCoord2f32(v->u, v->v);
        }
        GX_End();
    }

    /* torna alla modalita' 2D normale */
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GRRLIB_2dMode();
}

static void drawMii(V2 p, int isHidden) {
    float sway = sinf(frame * 0.05f) * 6.0f;   /* leggero dondolio */
    drawModel(&mii_model, NULL, p.x, p.y + 24, 52.0f,
              playerFace + sway + MII_YAW_OFFSET, 1, 1, isHidden ? 0x70 : 0xFF);
}
static void drawMario(V2 p, int glitch) {
    float sx = 1, sy = 1, ox = 0, oy = 0;
    if (glitch) {                               /* Mario "buggato" */
        ox = (rand() % 13) - 6; oy = (rand() % 9) - 4;
        if (rand() % 5 == 0) { sx = 0.6f + (rand() % 100) / 100.0f; sy = 0.7f + (rand() % 80) / 100.0f; }
    }
    drawModel(&mario_model, marioTex, p.x + ox, p.y + 28 + oy, 62.0f,
              marioFace + (glitch ? (rand() % 40) - 20 : 0) + MARIO_YAW_OFFSET, sx, sy, 0xFF);
    if (glitch && rand() % 4 == 0)              /* artefatto grafico */
        GRRLIB_Rectangle(p.x - 40, p.y + (rand() % 40) - 20, 80, 2, 0xFFFFFFFF, 1);
}

static void drawGame(int level) {
    u32 bg = mapColors[curMap];
    if (level >= 3 && (frame % 90) < 3) bg = 0x330000FF; /* sfarfallio */
    GRRLIB_FillScreen(bg);

    /* indicatore mappa: pallini */
    for (int i = 0; i < NMAPS; i++)
        GRRLIB_Circle(20 + i * 18, 16, 5, dead[i] ? 0x550000FF : (i == curMap ? 0xFFFFFFFF : 0x888888FF), 1);

    for (int i = 0; i < NSPOTS; i++)
        GRRLIB_Rectangle(spots[i].x, spots[i].y, spots[i].w, spots[i].h, COL_SPOT, 1);
    GRRLIB_Rectangle(exitR.x, exitR.y, exitR.w, exitR.h, COL_EXIT, 1);

    drawMario(mario, chase || level >= 2);
    drawMii(player, hidden);

    /* vignettatura: piu' buio con piu' anomalie */
    if (level > 0)
        GRRLIB_Rectangle(0, 0, SW, SH, 0x00000000 | (level * 18), 1);
}

/* ---------- logica gioco ---------- */
static void updateGame(u32 held, int level) {
    WPADData *d = WPAD_Data(0);
    float dx = 0, dy = 0;

    /* hiding */
    hidden = 0;
    if (held & WPAD_BUTTON_B)
        for (int i = 0; i < NSPOTS; i++)
            if (inRect(player, spots[i])) hidden = 1;

    if (!hidden) {
        if (d->exp.type == WPAD_EXP_NUNCHUK) {
            float mag = d->exp.nunchuk.js.mag;
            float ang = d->exp.nunchuk.js.ang * M_PI / 180.0f;
            dx = sinf(ang) * mag; dy = -cosf(ang) * mag;
        }
        if (held & WPAD_BUTTON_LEFT)  dx = -1;
        if (held & WPAD_BUTTON_RIGHT) dx = 1;
        if (held & WPAD_BUTTON_UP)    dy = -1;
        if (held & WPAD_BUTTON_DOWN)  dy = 1;
        if (dx > 0.1f) playerFace = 55; else if (dx < -0.1f) playerFace = -55;
        player.x = clampf(player.x + dx * 2.2f, 10, SW - 10);
        player.y = clampf(player.y + dy * 2.2f, 30, SH - 10);
    }

    float dMP = dist(mario, player);

    /* Mario ti vede? */
    if (!chase && !hidden && dMP < 170) { chase = 1; lastSeen = player; }

    if (chase) {
        if (!hidden) { lastSeen = player; giveUpTimer = 0; }
        else if (++giveUpTimer > 180) { chase = 0; pickPatrol(); }

        /* corre verso l'ultima posizione vista, "buggato" */
        float ang = atan2f(lastSeen.y - mario.y, lastSeen.x - mario.x);
        marioFace = (cosf(ang) >= 0) ? 55 : -55;
        float sp = 2.6f + level * 0.25f;
        mario.x += cosf(ang) * sp + ((rand() % 5) - 2);
        mario.y += sinf(ang) * sp + ((rand() % 5) - 2);
        if (rand() % 120 == 0) { /* glitch: micro-teletrasporto */
            mario.x += cosf(ang) * 60; mario.y += sinf(ang) * 60;
        }
        WPAD_Rumble(0, (frame % 8) < 4);

        float thresh = hidden ? 22 : 18;
        if (dMP < thresh) { scene = SC_OVER; WPAD_Rumble(0, 0); }
    } else {
        WPAD_Rumble(0, 0);
        float ang = atan2f(patrolTarget.y - mario.y, patrolTarget.x - mario.x);
        marioFace = (cosf(ang) >= 0) ? 55 : -55;
        mario.x += cosf(ang) * 1.1f;
        mario.y += sinf(ang) * 1.1f;
        if (dist(mario, patrolTarget) < 8) pickPatrol();
        /* con tante anomalie Mario ti "sente" anche vicino ai nascondigli */
        if (level >= 3 && dMP < 60) chase = 1, lastSeen = player;
    }

    /* uscita raggiunta -> mappa morta -> finto menu Wii */
    if (inRect(player, exitR)) {
        dead[curMap] = 1;
        saveGame();
        WPAD_Rumble(0, 0);
        int lv = anomalyLevel();
        for (int i = 0; i < 12; i++) anomalyChannels[i] = 0;
        srand(lv * 31 + 5);
        for (int i = 0; i < lv + 1 && i < 12; i++)
            anomalyChannels[rand() % 12] = 1;
        scene = (nextMap() < 0) ? SC_END : SC_MENU;
    }
}

/* ---------- finto menu Wii ---------- */
static void menuLoop(u32 down) {
    GRRLIB_FillScreen(0xE8E8EEFF);
    ir_t ir; WPAD_IR(0, &ir);
    int hover = -1;

    for (int i = 0; i < 12; i++) {
        int cx = 40 + (i % 4) * 145, cy = 60 + (i / 4) * 120;
        u32 col = anomalyChannels[i] ? 0x3A0A0AFF : 0xFFFFFFFF;
        GRRLIB_Rectangle(cx, cy, 130, 100, col, 1);
        GRRLIB_Rectangle(cx, cy, 130, 100, 0xAAAAAAFF, 0);
        if (anomalyChannels[i]) /* "occhio" di Mario nel canale */
            GRRLIB_Circle(cx + 65, cy + 50, 10 + (frame % 20), COL_RED, 1);
        if (ir.valid && ir.x > cx && ir.x < cx + 130 && ir.y > cy && ir.y < cy + 100)
            hover = i;
    }

    if (ir.valid) GRRLIB_Circle(ir.x, ir.y, 6, 0x333333FF, 1);

    /* vibrazione extra sui canali anomali */
    if (hover >= 0 && anomalyChannels[hover]) WPAD_Rumble(0, (frame % 3) != 0);
    else WPAD_Rumble(0, 0);

    if (down & WPAD_BUTTON_A) {
        WPAD_Rumble(0, 0);
        int m = nextMap();
        if (m >= 0) { resetMap(m); scene = SC_GAME; }
    }
}

int main(void) {
    VIDEO_Init();
    WPAD_Init();
    WPAD_SetDataFormat(0, WPAD_FMT_BTNS_ACC_IR);
    GRRLIB_Init();
    marioTex[0] = GRRLIB_LoadTexture(mario_tex0);
    marioTex[1] = GRRLIB_LoadTexture(mario_tex1);
    marioTex[2] = GRRLIB_LoadTexture(mario_tex2);
    fatInitDefault();
    loadGame();

    int m = nextMap();
    if (m < 0) { scene = SC_END; } else resetMap(m);

    while (1) {
        WPAD_ScanPads();
        u32 down = WPAD_ButtonsDown(0), held = WPAD_ButtonsHeld(0);
        if (down & WPAD_BUTTON_HOME) break;
        frame++;
        int level = anomalyLevel();

        switch (scene) {
        case SC_GAME:
            updateGame(held, level);
            if (scene == SC_GAME) drawGame(level);
            break;
        case SC_MENU:
            menuLoop(down);
            break;
        case SC_OVER:
            GRRLIB_FillScreen(0x000000FF);
            GRRLIB_Rectangle(0, 200 + (rand() % 8), SW, 6, COL_RED, 1);
            if (down & WPAD_BUTTON_A) resetMap(curMap), scene = SC_GAME;
            break;
        case SC_END:
            GRRLIB_FillScreen((frame % 20) < 10 ? 0x000000FF : 0x330000FF);
            drawMario((V2){ SW / 2, SH / 2 }, 1);
            break;
        }
        GRRLIB_Render();
    }
    WPAD_Rumble(0, 0);
    GRRLIB_Exit();
    return 0;
}
