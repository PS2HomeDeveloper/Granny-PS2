#include <string.h>
#include <malloc.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>

#include "font_engine.h"

extern int gsKit_texture_finish(GSGLOBAL *gsGlobal, GSTEXTURE *Texture);

static int load_embedded_texture(GSGLOBAL *gsGlobal, GSTEXTURE *Texture,
                                  const unsigned char *data, unsigned int data_size)
{
    unsigned int need;

    need = gsKit_texture_size_ee(Texture->Width, Texture->Height, Texture->PSM);
    Texture->Mem = memalign(128, need);

    if (Texture->Mem == NULL)
        return -1;

    if (Texture->PSM != GS_PSM_T8 && Texture->PSM != GS_PSM_T4)
    {
        Texture->VramClut = 0;
        Texture->Clut = NULL;
    }

    if (need > data_size)
        need = data_size;

    memcpy(Texture->Mem, data, need);

    return gsKit_texture_finish(gsGlobal, Texture);
}

static const FontCharEntry s_font_map[] = {

    { '!', 0, 0, 0 },
    { '"', 0, 1, 0 },
    { '#', 0, 2, 0 },
    { '$', 0, 3, 0 },
    { '%', 0, 4, 0 },
    { '&', 0, 5, 0 },
    { '\'', 0, 6, 0 },
    { '(', 0, 7, 0 },
    { ')', 0, 8, 0 },
    { '*', 0, 9, 0 },
    { '+', 0, 10, 0 },
    { ',', 0, 11, 0 },
    { '-', 0, 12, 0 },
    { '.', 0, 13, 0 },

    { '/', 0, 0, 1 },
    { '0', 0, 1, 1 },
    { '1', 0, 2, 1 },
    { '2', 0, 3, 1 },
    { '3', 0, 4, 1 },
    { '4', 0, 5, 1 },
    { '5', 0, 6, 1 },
    { '6', 0, 7, 1 },
    { '7', 0, 8, 1 },
    { '8', 0, 9, 1 },
    { '9', 0, 10, 1 },
    { ':', 0, 11, 1 },
    { ';', 0, 12, 1 },
    { '<', 0, 13, 1 },

    { '=', 0, 0, 2 },
    { '>', 0, 1, 2 },
    { '?', 0, 2, 2 },
    { '@', 0, 3, 2 },
    { 'A', 0, 4, 2 },
    { 'B', 0, 5, 2 },
    { 'C', 0, 6, 2 },
    { 'D', 0, 7, 2 },
    { 'E', 0, 8, 2 },
    { 'F', 0, 9, 2 },
    { 'G', 0, 10, 2 },
    { 'H', 0, 11, 2 },
    { 'I', 0, 12, 2 },
    { 'J', 0, 13, 2 },

    { 'K', 0, 0, 3 },
    { 'L', 0, 1, 3 },
    { 'M', 0, 2, 3 },
    { 'N', 0, 3, 3 },
    { 'O', 0, 4, 3 },
    { 'P', 0, 5, 3 },
    { 'Q', 0, 6, 3 },
    { 'R', 0, 7, 3 },
    { 'S', 0, 8, 3 },
    { 'T', 0, 9, 3 },
    { 'U', 0, 10, 3 },
    { 'V', 0, 11, 3 },
    { 'W', 0, 12, 3 },
    { 'X', 0, 13, 3 },

    { 'Y', 0, 0, 4 },
    { 'Z', 0, 1, 4 },
    { '[', 0, 2, 4 },
    { '\\', 0, 3, 4 },
    { ']', 0, 4, 4 },
    { '^', 0, 5, 4 },
    { '_', 0, 6, 4 },
    { '`', 0, 7, 4 },
    { 'a', 0, 8, 4 },
    { 'b', 0, 9, 4 },
    { 'c', 0, 10, 4 },
    { 'd', 0, 11, 4 },
    { 'e', 0, 12, 4 },
    { 'f', 0, 13, 4 },

    { 'g', 0, 0, 5 },
    { 'h', 0, 1, 5 },
    { 'i', 0, 2, 5 },
    { 'j', 0, 3, 5 },
    { 'k', 0, 4, 5 },
    { 'l', 0, 5, 5 },
    { 'm', 0, 6, 5 },
    { 'n', 0, 7, 5 },
    { 'o', 0, 8, 5 },
    { 'p', 0, 9, 5 },
    { 'q', 0, 10, 5 },
    { 'r', 0, 11, 5 },
    { 's', 0, 12, 5 },
    { 't', 0, 13, 5 },

    { 'u', 0, 0, 6 },
    { 'v', 0, 1, 6 },
    { 'w', 0, 2, 6 },
    { 'x', 0, 3, 6 },
    { 'y', 0, 4, 6 },
    { 'z', 0, 5, 6 },
    { '{', 0, 6, 6 },
    { '|', 0, 7, 6 },
    { '}', 0, 8, 6 },
    { '~', 0, 9, 6 },
    { 0x00F7, 0, 10, 6 },
    { 0x00D7, 0, 11, 6 },
    { 0x00B1, 0, 12, 6 },
    { 0x00B0, 0, 13, 6 },

    { 0x00A7, 0, 0, 7 },
    { 0x00A3, 0, 1, 7 },
    { 0x00A5, 0, 2, 7 },
    { 0x20AC, 0, 3, 7 },
    { 0x00A2, 0, 4, 7 },
    { 0x00B5, 0, 5, 7 },
    { 0x00B6, 0, 6, 7 },
    { 0x00A9, 0, 7, 7 },
    { 0x00AE, 0, 8, 7 },
    { 0x2190, 0, 9, 7 },
    { 0x2191, 0, 10, 7 },
    { 0x2192, 0, 11, 7 },
    { 0x2193, 0, 12, 7 },
    { 0x221A, 0, 13, 7 },

    { 0x2713, 1, 0, 0 },
    { 0x2715, 1, 1, 0 },
    { 0x2264, 1, 2, 0 },
    { 0x2265, 1, 3, 0 },
    { 0x2260, 1, 4, 0 },
    { 0x00AB, 1, 5, 0 },
    { 0x00BB, 1, 6, 0 },
    { 0x2018, 1, 7, 0 },
    { 0x2019, 1, 8, 0 },
    { 0x2014, 1, 9, 0 },
    { 0x2013, 1, 10, 0 },
    { 0x2605, 1, 11, 0 },
    { 0x2606, 1, 12, 0 },
    { 0x2026, 1, 13, 0 },

    { 0x2022, 1, 0, 1 },
    { 0x2122, 1, 1, 1 },
    { 0x25C6, 1, 2, 1 },
    { 0x25C7, 1, 3, 1 },
    { 0x25A0, 1, 4, 1 },
    { 0x25A1, 1, 5, 1 },
    { 0x03C0, 1, 6, 1 },
    { 0x03A9, 1, 7, 1 },
    { 0x0394, 1, 8, 1 },
    { 0x03A3, 1, 9, 1 },
    { 0x2116, 1, 10, 1 },
    { 0x2139, 1, 11, 1 },
    { 0x00BF, 1, 12, 1 },
    { ' ', 1, 13, 1 },

    { 0xE000, 1, 0, 2 },

    { 0, -1, 0, 0 }
};

#define FONT_MAP_COUNT \
    ((int)(sizeof(s_font_map) / sizeof(s_font_map[0])) - 1)

#define LUT_ASCII_SIZE  128
#define LUT_UNI_SIZE    64
#define LUT_UNI_EMPTY   0xFFFFFFFFu

static const FontCharEntry *s_ascii_lut[LUT_ASCII_SIZE];

typedef struct {
    unsigned int         codepoint;
    const FontCharEntry *entry;
} UniSlot;

static UniSlot s_uni_lut[LUT_UNI_SIZE];

static int s_lut_built = 0;

static void build_lookup_tables(void)
{
    int i;

    for (i = 0; i < LUT_ASCII_SIZE; i++)
        s_ascii_lut[i] = NULL;

    for (i = 0; i < LUT_UNI_SIZE; i++)
    {
        s_uni_lut[i].codepoint = LUT_UNI_EMPTY;
        s_uni_lut[i].entry     = NULL;
    }

    for (i = 0; i < FONT_MAP_COUNT; i++)
    {
        unsigned int cp = s_font_map[i].codepoint;

        if (cp < LUT_ASCII_SIZE)
        {
            s_ascii_lut[cp] = &s_font_map[i];
        }
        else
        {
            unsigned int slot = (cp ^ (cp >> 8)) & (LUT_UNI_SIZE - 1);
            int tries = 0;

            while (s_uni_lut[slot].codepoint != LUT_UNI_EMPTY
                   && tries < LUT_UNI_SIZE)
            {
                slot = (slot + 1) & (LUT_UNI_SIZE - 1);
                tries++;
            }

            if (tries < LUT_UNI_SIZE)
            {
                s_uni_lut[slot].codepoint = cp;
                s_uni_lut[slot].entry     = &s_font_map[i];
            }
        }
    }

    s_lut_built = 1;
}

const FontCharEntry *font_engine_lookup(unsigned int codepoint)
{
    if (codepoint < LUT_ASCII_SIZE)
        return s_ascii_lut[codepoint];

    unsigned int slot  = (codepoint ^ (codepoint >> 8)) & (LUT_UNI_SIZE - 1);
    int          tries = 0;

    while (s_uni_lut[slot].codepoint != LUT_UNI_EMPTY
           && tries < LUT_UNI_SIZE)
    {
        if (s_uni_lut[slot].codepoint == codepoint)
            return s_uni_lut[slot].entry;

        slot = (slot + 1) & (LUT_UNI_SIZE - 1);
        tries++;
    }

    return NULL;
}

int font_engine_init(GSGLOBAL *gsGlobal, FontEngine *fe,
                     const unsigned char *data1, unsigned int size1,
                     const unsigned char *data2, unsigned int size2)
{
    if (!gsGlobal || !fe || !data1 || !data2)
        return -1;

    memset(fe, 0, sizeof(FontEngine));

    if (!s_lut_built)
        build_lookup_tables();

    fe->sheets[0].Width  = FONT_SHEET_W;
    fe->sheets[0].Height = FONT_SHEET_H;
    fe->sheets[0].PSM    = GS_PSM_CT32;
    fe->sheets[0].Filter = GS_FILTER_NEAREST;

    load_embedded_texture(gsGlobal, &fe->sheets[0], data1, size1);

    fe->sheets[1].Width  = FONT_SHEET_W;
    fe->sheets[1].Height = FONT_SHEET_H;
    fe->sheets[1].PSM    = GS_PSM_CT32;
    fe->sheets[1].Filter = GS_FILTER_NEAREST;

    load_embedded_texture(gsGlobal, &fe->sheets[1], data2, size2);

    fe->loaded = 1;

    return 0;
}

void font_engine_begin_draw(GSGLOBAL *gsGlobal)
{
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_set_test(gsGlobal, GS_ATEST_OFF);
}

void font_engine_end_draw(GSGLOBAL *gsGlobal)
{
    gsKit_set_test(gsGlobal, GS_ATEST_ON);
    gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
}

static void font_engine_draw_char_raw_sized(GSGLOBAL *gsGlobal, FontEngine *fe,
                                            unsigned int ch,
                                            float x, float y,
                                            int z, u64 color,
                                            float char_w, float char_h)
{
    const FontCharEntry *entry;
    GSTEXTURE           *tex;
    float                u1, v1, u2, v2;
    float                x2, y2;

    if (!fe || !fe->loaded || char_w <= 0.0f || char_h <= 0.0f)
        return;

    entry = font_engine_lookup(ch);
    if (!entry || entry->sheet < 0)
        return;

    tex = &fe->sheets[entry->sheet];

    u1 = (float)(entry->col * FONT_CELL_W);
    v1 = (float)(entry->row * FONT_CELL_H);
    u2 = u1 + (float)FONT_CELL_W;
    v2 = v1 + (float)FONT_CELL_H;

    x2 = x + char_w;
    y2 = y + char_h;

    gsKit_prim_sprite_texture(
        gsGlobal, tex,
        x,  y,  u1, v1,
        x2, y2, u2, v2,
        z, color
    );
}

void font_engine_draw_char_raw(GSGLOBAL *gsGlobal, FontEngine *fe,
                               unsigned int ch,
                               float x, float y,
                               int z, u64 color)
{
    font_engine_draw_char_raw_sized(gsGlobal, fe, ch, x, y, z, color,
                                    (float)FONT_CHAR_DRAW_W,
                                    (float)FONT_CHAR_DRAW_H);
}

void font_engine_draw_cursor_raw(GSGLOBAL *gsGlobal, FontEngine *fe,
                                 float x, float y,
                                 int z, u64 color,
                                 int cursor_w, int cursor_h)
{
    const FontCharEntry *entry;
    GSTEXTURE           *tex;
    float                u1, v1, u2, v2;
    float                x2, y2;
    int                  u_offset, v_offset;

    if (!fe || !fe->loaded)
        return;

    entry = font_engine_lookup(0xE000u);
    if (!entry || entry->sheet < 0)
        return;

    tex = &fe->sheets[entry->sheet];

    u_offset = (FONT_CELL_W - cursor_w) / 2;
    v_offset = (FONT_CELL_H - cursor_h) / 2;

    u1 = (float)(entry->col * FONT_CELL_W) + (float)u_offset;
    v1 = (float)(entry->row * FONT_CELL_H) + (float)v_offset;
    u2 = u1 + (float)cursor_w;
    v2 = v1 + (float)cursor_h;

    x2 = x + (float)cursor_w;
    y2 = y + (float)cursor_h;

    gsKit_prim_sprite_texture(
        gsGlobal, tex,
        x,  y,  u1, v1,
        x2, y2, u2, v2,
        z, color
    );
}

void font_engine_draw_char_sized(GSGLOBAL *gsGlobal, FontEngine *fe,
                                 unsigned int ch,
                                 float x, float y,
                                 int z, u64 color,
                                 float char_w, float char_h)
{
    font_engine_begin_draw(gsGlobal);
    font_engine_draw_char_raw_sized(gsGlobal, fe, ch, x, y, z, color,
                                    char_w, char_h);
    font_engine_end_draw(gsGlobal);
}

void font_engine_draw_char(GSGLOBAL *gsGlobal, FontEngine *fe,
                           unsigned int ch,
                           float x, float y,
                           int z, u64 color)
{
    font_engine_draw_char_sized(gsGlobal, fe, ch, x, y, z, color,
                                (float)FONT_CHAR_DRAW_W,
                                (float)FONT_CHAR_DRAW_H);
}

void font_engine_draw_string_sized(GSGLOBAL *gsGlobal, FontEngine *fe,
                                   const char *str,
                                   float x, float y,
                                   int z, u64 color,
                                   float char_w, float char_h)
{
    float cursor_x = x;
    float cursor_y = y;
    int i = 0;

    if (!str || char_w <= 0.0f || char_h <= 0.0f)
        return;

    font_engine_begin_draw(gsGlobal);

    while (str[i] != '\0')
    {
        unsigned char ch = (unsigned char)str[i];

        if (ch == '\n')
        {
            cursor_x = x;
            cursor_y += char_h;
            i++;
            continue;
        }

        font_engine_draw_char_raw_sized(gsGlobal, fe, (unsigned int)ch,
                                        cursor_x, cursor_y, z, color,
                                        char_w, char_h);

        cursor_x += char_w;
        i++;
    }

    font_engine_end_draw(gsGlobal);
}

void font_engine_draw_string(GSGLOBAL *gsGlobal, FontEngine *fe,
                             const char *str,
                             float x, float y,
                             int z, u64 color)
{
    font_engine_draw_string_sized(gsGlobal, fe, str, x, y, z, color,
                                  (float)FONT_CHAR_DRAW_W,
                                  (float)FONT_CHAR_DRAW_H);
}

void font_engine_draw_string32_sized(GSGLOBAL *gsGlobal, FontEngine *fe,
                                     const unsigned int *str32,
                                     float x, float y,
                                     int z, u64 color,
                                     float char_w, float char_h)
{
    float cursor_x = x;
    float cursor_y = y;
    int i = 0;

    if (!str32 || char_w <= 0.0f || char_h <= 0.0f)
        return;

    font_engine_begin_draw(gsGlobal);

    while (str32[i] != 0)
    {
        unsigned int ch = str32[i];

        if (ch == (unsigned int)'\n')
        {
            cursor_x = x;
            cursor_y += char_h;
            i++;
            continue;
        }

        font_engine_draw_char_raw_sized(gsGlobal, fe, ch,
                                        cursor_x, cursor_y, z, color,
                                        char_w, char_h);

        cursor_x += char_w;
        i++;
    }

    font_engine_end_draw(gsGlobal);
}

void font_engine_draw_string32(GSGLOBAL *gsGlobal, FontEngine *fe,
                               const unsigned int *str32,
                               float x, float y,
                               int z, u64 color)
{
    font_engine_draw_string32_sized(gsGlobal, fe, str32, x, y, z, color,
                                    (float)FONT_CHAR_DRAW_W,
                                    (float)FONT_CHAR_DRAW_H);
}

float font_engine_string_width_sized(const char *str, float char_w)
{
    int count = 0;
    int i = 0;

    if (!str || char_w <= 0.0f)
        return 0.0f;

    while (str[i] != '\0')
    {
        if (str[i] != '\n')
            count++;
        i++;
    }

    return (float)count * char_w;
}

float font_engine_string_width(const char *str)
{
    return font_engine_string_width_sized(str,
                                          (float)FONT_CHAR_DRAW_W);
}

float font_engine_string32_width_sized(const unsigned int *str32,
                                       float char_w)
{
    int count = 0;
    int i = 0;

    if (!str32 || char_w <= 0.0f)
        return 0.0f;

    while (str32[i] != 0)
    {
        if (str32[i] != (unsigned int)'\n')
            count++;
        i++;
    }

    return (float)count * char_w;
}

float font_engine_string32_width(const unsigned int *str32)
{
    return font_engine_string32_width_sized(str32,
                                            (float)FONT_CHAR_DRAW_W);
}

void font_engine_shutdown(FontEngine *fe)
{
    if (!fe)
        return;

    memset(fe, 0, sizeof(FontEngine));
}
