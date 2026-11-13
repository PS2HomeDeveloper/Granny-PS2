#ifndef GR_TIM2_H
#define GR_TIM2_H

#include <stddef.h>
#include <stdint.h>

typedef struct GrTim2Image {
    unsigned char *pixels;
    unsigned char *clut;
    uint32_t width, height;
    uint32_t psm, clut_psm, clut_storage_mode;
    uint32_t byte_size, clut_size;
    uint32_t allocation_size, clut_allocation_size;
} GrTim2Image;

int gr_tim2_load(const unsigned char *data, size_t size, GrTim2Image *image,
                 char *error, size_t error_size);
void gr_tim2_free(GrTim2Image *image);

#endif
