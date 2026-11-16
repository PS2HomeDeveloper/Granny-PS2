#ifndef GRANNY_MAP_H
#define GRANNY_MAP_H

#include "renderer.h"

#include <stdint.h>

#define GR_WORLD_RESIDENT_MAPS 2u

typedef struct GrWorld {
    void *state;
} GrWorld;

int world_init(GrWorld *world);
void world_shutdown(GrWorld *world);
/* Returns 1 when the resident maps changed, 0 when unchanged, and -1 on error. */
int world_update(GrWorld *world, const float position[3]);
unsigned world_material_count(const GrWorld *world);
int world_fill_materials(GrWorld *world, GrRenderMaterial *out,
                         unsigned capacity, unsigned base_material);
void world_draw(GrWorld *world);
int world_capsule_blocked(const GrWorld *world, float x, float feet_y, float z,
                          float radius, float height);
const char *world_error(void);

#endif
