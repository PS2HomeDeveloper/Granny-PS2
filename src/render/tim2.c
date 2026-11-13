#include "render/tim2.h"
#include "core/gr_limits.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PSM_CT32 0u
#define PSM_CT24 1u
#define PSM_CT16 2u
#define PSM_CT16S 10u
#define PSM_T8 19u
#define PSM_T4 20u
#define PSM_T8H 27u
#define PSM_T4HL 26u
#define PSM_T4HH 44u

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t read_u64(const unsigned char *p)
{
    return (uint64_t)read_u32(p) | (uint64_t)read_u32(p + 4) << 32;
}

static int failure(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
    return -1;
}

static int is_power_of_two(uint32_t n)
{
    return n && !(n & (n - 1u));
}

static uint32_t log2_power_of_two(uint32_t n)
{
    uint32_t value = 0;
    while (n > 1u) { n >>= 1; ++value; }
    return value;
}

static uint32_t pixel_bytes(uint32_t psm, uint32_t width, uint32_t height)
{
    uint64_t pixels = (uint64_t)width * height;
    uint64_t bytes;
    switch (psm) {
    case PSM_CT32: bytes = pixels * 4u; break;
    case PSM_CT24: bytes = pixels * 3u; break;
    case PSM_CT16:
    case PSM_CT16S: bytes = pixels * 2u; break;
    case PSM_T8: bytes = pixels; break;
    case PSM_T4: bytes = (pixels + 1u) / 2u; break;
    case PSM_T8H:
    case PSM_T4HL:
    case PSM_T4HH: bytes = pixels * 4u; break;
    default: return 0;
    }
    return bytes <= 0xffffffffu ? (uint32_t)bytes : 0;
}

/* Mirror gsKit_texture_size() so its cache/DMA spans are backed by memory. */
static uint32_t gs_texture_size(uint32_t width, uint32_t height, uint32_t psm)
{
    uint32_t wb, hb, wa, ha;
    switch (psm) {
    case PSM_CT32:
    case PSM_CT24:
        wb = (width + 7u) / 8u; hb = (height + 7u) / 8u;
        if (wb <= 2 && hb <= 1) { wa = 1; ha = 1; }
        else if (wb <= 4 && hb <= 2) { wa = 2; ha = 2; }
        else if (wb <= 8 && hb <= 4) { wa = 4; ha = 4; }
        else { wa = 8; ha = 4; }
        break;
    case PSM_CT16:
    case PSM_T4:
        wb = (width + 15u) / 16u; hb = (height + 7u) / 8u;
        if (wb <= 1 && hb <= 2) { wa = 1; ha = 1; }
        else if (wb <= 2 && hb <= 4) { wa = 2; ha = 2; }
        else if (wb <= 4 && hb <= 8) { wa = 4; ha = 4; }
        else { wa = 4; ha = 8; }
        break;
    case PSM_T8:
        wb = (width + 15u) / 16u; hb = (height + 15u) / 16u;
        if (wb <= 2 && hb <= 1) { wa = 1; ha = 1; }
        else if (wb <= 4 && hb <= 2) { wa = 2; ha = 2; }
        else if (wb <= 8 && hb <= 4) { wa = 4; ha = 4; }
        else { wa = 8; ha = 4; }
        break;
    case PSM_CT16S:
        wb = (width + 15u) / 16u; hb = (height + 7u) / 8u;
        if (wb <= 1 && hb <= 2) { wa = 1; ha = 1; }
        else if (wb <= 2 && hb <= 2) { wa = 2; ha = 2; }
        else if (wb <= 2 && hb <= 8) { wa = 2; ha = 8; }
        else { wa = 4; ha = 8; }
        break;
    default: return 0;
    }
    wb = (wb + wa - 1u) & ~(wa - 1u);
    hb = (hb + ha - 1u) & ~(ha - 1u);
    return wb * hb * 256u;
}

static void pack_t4(unsigned char *out, const unsigned char *indices, uint32_t count)
{
    uint32_t i;
    memset(out, 0, (count + 1u) / 2u);
    for (i = 0; i < count; ++i) {
        unsigned char v = (unsigned char)(indices[i] & 0x0fu);
        if (i & 1u) out[i >> 1] |= (unsigned char)(v << 4);
        else out[i >> 1] = v;
    }
}

int gr_tim2_load(const unsigned char *data, size_t size, GrTim2Image *image,
                 char *error, size_t error_size)
{
    const unsigned char *picture, *src_pixels, *src_clut;
    uint32_t total, clut_size, image_size, header_size, clut_colors;
    uint32_t width, height, source_psm, psm, cpsm, csm, mip_count;
    uint32_t source_bytes, needed_image_bytes, base_bytes, allocation;
    uint32_t clut_bytes = 0, clut_allocation = 0, clut_width = 0, clut_height = 0;
    uint32_t pixel_count, x, mip_w, mip_h, level;
    uint64_t tex0;
    size_t clut_offset;
    unsigned char *pixels = NULL, *clut = NULL;
    int indexed;

    if (!image)
        return failure(error, error_size, "TIM2: null output image");
    memset(image, 0, sizeof(*image));
    if (!data || size < 64 || memcmp(data, "TIM2", 4))
        return failure(error, error_size, "TIM2: missing or truncated header");
    if (data[4] != 4 || data[5] != 0 || read_u16(data + 6) != 1)
        return failure(error, error_size, "TIM2: require version 4, linear file header, one picture");
    picture = data + 16;
    total = read_u32(picture);
    clut_size = read_u32(picture + 4);
    image_size = read_u32(picture + 8);
    header_size = read_u16(picture + 12);
    clut_colors = read_u16(picture + 14);
    mip_count = picture[17];
    if (header_size != 48 || picture[16] != 0 || !mip_count || mip_count > 16)
        return failure(error, error_size, "TIM2: unsupported picture header, compression, or mip count");
    if (total > size - 16u || total < header_size || image_size > total - header_size ||
        clut_size > total - header_size - image_size)
        return failure(error, error_size, "TIM2: picture block exceeds input bounds");

    width = read_u16(picture + 20);
    height = read_u16(picture + 22);
    tex0 = read_u64(picture + 24);
    source_psm = (uint32_t)((tex0 >> 20) & 63u);
    cpsm = (uint32_t)((tex0 >> 51) & 15u);
    csm = (uint32_t)((tex0 >> 55) & 1u);
    if (!width || !height || width > GR_MAX_TEXTURE_SIZE || height > GR_MAX_TEXTURE_SIZE ||
        !is_power_of_two(width) || !is_power_of_two(height))
        return failure(error, error_size, "TIM2: dimensions must be powers of two, 1..512");
    if (((source_psm == PSM_CT32) && picture[19] != 0) ||
        ((source_psm == PSM_CT24) && picture[19] != 2) ||
        ((source_psm == PSM_CT16 || source_psm == PSM_CT16S) && picture[19] != 1) ||
        ((source_psm == PSM_T8 || source_psm == PSM_T8H) && picture[19] != 5) ||
        ((source_psm == PSM_T4 || source_psm == PSM_T4HL || source_psm == PSM_T4HH) &&
         picture[19] != 3 && picture[19] != 4))
        return failure(error, error_size, "TIM2: image type does not match GsTex0 PSM");
    if (((tex0 >> 26) & 15u) != log2_power_of_two(width))
        return failure(error, error_size, "TIM2: GsTex0 texture width does not match dimensions");
    if (((tex0 >> 30) & 15u) != log2_power_of_two(height))
        return failure(error, error_size, "TIM2: GsTex0 texture height does not match dimensions");

    source_bytes = pixel_bytes(source_psm, width, height);
    if (!source_bytes)
        return failure(error, error_size, "TIM2: unsupported GS PSM");
    needed_image_bytes = 0;
    mip_w = width;
    mip_h = height;
    for (level = 0; level < mip_count; ++level) {
        uint32_t level_bytes = pixel_bytes(source_psm, mip_w, mip_h);
        if (!level_bytes || needed_image_bytes > 0xffffffffu - level_bytes)
            return failure(error, error_size, "TIM2: invalid or oversized mip chain");
        needed_image_bytes += level_bytes;
        mip_w = mip_w > 1 ? mip_w / 2u : 1u;
        mip_h = mip_h > 1 ? mip_h / 2u : 1u;
    }
    if (image_size < needed_image_bytes || image_size < source_bytes)
        return failure(error, error_size, "TIM2: image data is truncated for the declared PSM/mip chain");

    indexed = source_psm == PSM_T8 || source_psm == PSM_T4 ||
              source_psm == PSM_T8H || source_psm == PSM_T4HL || source_psm == PSM_T4HH;
    psm = source_psm;
    if (source_psm == PSM_CT24) psm = PSM_CT32;
    if (source_psm == PSM_T8H) psm = PSM_T8;
    if (source_psm == PSM_T4HL || source_psm == PSM_T4HH) psm = PSM_T4;
    base_bytes = pixel_bytes(psm, width, height);
    if (!base_bytes || !(allocation = gs_texture_size(width, height, psm)))
        return failure(error, error_size, "TIM2: this pixel format cannot be uploaded by gsKit");

    if (indexed) {
        uint32_t minimum_colors = (psm == PSM_T8) ? 256u : 16u;
        uint32_t clut_entry_bytes;
        if (clut_colors < minimum_colors || !clut_size)
            return failure(error, error_size, "TIM2: indexed texture has no complete CLUT");
        if (picture[18] == 0x01) clut_entry_bytes = 2;
        else if (picture[18] == 0x02 || picture[18] == 0x03) clut_entry_bytes = 4;
        else return failure(error, error_size, "TIM2: unsupported CLUT entry format");
        clut_bytes = minimum_colors * clut_entry_bytes;
        if (clut_size < clut_bytes)
            return failure(error, error_size, "TIM2: CLUT payload is truncated");
        if (!(cpsm == PSM_CT32 || cpsm == PSM_CT24 || cpsm == PSM_CT16 || cpsm == PSM_CT16S))
            return failure(error, error_size, "TIM2: unsupported GS CLUT PSM");
        clut_width = psm == PSM_T8 ? 16u : 8u;
        clut_height = psm == PSM_T8 ? 16u : 2u;
        clut_allocation = gs_texture_size(clut_width, clut_height, cpsm);
        if (!clut_allocation)
            return failure(error, error_size, "TIM2: gsKit does not support the declared CLUT PSM");
    } else if (clut_size || clut_colors || picture[18] != 0) {
        return failure(error, error_size, "TIM2: direct-color texture unexpectedly contains a CLUT");
    }

    src_pixels = data + 16u + header_size;
    clut_offset = 16u + header_size + image_size;
    src_clut = data + clut_offset;
    pixels = (unsigned char *)memalign(128, allocation);
    if (!pixels)
        return failure(error, error_size, "TIM2: aligned pixel allocation failed");
    memset(pixels, 0, allocation);
    pixel_count = width * height;

    if (source_psm == PSM_CT24) {
        for (x = 0; x < pixel_count; ++x) {
            pixels[x * 4u + 0u] = src_pixels[x * 3u + 0u];
            pixels[x * 4u + 1u] = src_pixels[x * 3u + 1u];
            pixels[x * 4u + 2u] = src_pixels[x * 3u + 2u];
            pixels[x * 4u + 3u] = 0x80u;
        }
    } else if (source_psm == PSM_T8H) {
        for (x = 0; x < pixel_count; ++x) pixels[x] = src_pixels[x * 4u + 3u];
    } else if (source_psm == PSM_T4HL || source_psm == PSM_T4HH) {
        unsigned char *indices = (unsigned char *)malloc(pixel_count);
        if (!indices) { free(pixels); return failure(error, error_size, "TIM2: high-lane index allocation failed"); }
        for (x = 0; x < pixel_count; ++x) {
            unsigned char lane = src_pixels[x * 4u + 3u];
            indices[x] = source_psm == PSM_T4HL ? (unsigned char)(lane & 0x0fu) : (unsigned char)(lane >> 4);
        }
        pack_t4(pixels, indices, pixel_count);
        free(indices);
    } else {
        memcpy(pixels, src_pixels, base_bytes);
    }

    if (indexed) {
        clut = (unsigned char *)memalign(128, clut_allocation);
        if (!clut) {
            free(pixels);
            return failure(error, error_size, "TIM2: aligned CLUT allocation failed");
        }
        memset(clut, 0, clut_allocation);
        memcpy(clut, src_clut, clut_bytes);
    }
    image->pixels = pixels;
    image->clut = clut;
    image->width = width;
    image->height = height;
    image->psm = psm;
    image->clut_psm = indexed ? cpsm : 0;
    image->clut_storage_mode = indexed ? csm : 0;
    image->byte_size = base_bytes;
    image->clut_size = clut_bytes;
    image->allocation_size = allocation;
    image->clut_allocation_size = clut_allocation;
    if (error && error_size) error[0] = '\0';
    return 0;
}

void gr_tim2_free(GrTim2Image *image)
{
    if (image) {
        free(image->pixels);
        free(image->clut);
        memset(image, 0, sizeof(*image));
    }
}
