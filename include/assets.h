#ifndef GRANNY_ASSETS_H
#define GRANNY_ASSETS_H

#include <stdint.h>

typedef struct {
    const char *name;
    const unsigned char *data;
    uint32_t size;
} GrAsset;

int assets_init_external(const char *package_path);
const GrAsset *assets_find(const char *path_or_unique_basename);
const GrAsset *assets_find_texture(const char *source_texture_name);
const char *assets_error(void);
void assets_shutdown(void);

#endif
