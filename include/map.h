#ifndef GRANNY_MAP_H
#define GRANNY_MAP_H

#include "assets.h"
#include "renderer.h"
#include <stdint.h>

typedef struct {
    uint32_t type;
    uint32_t flags;
    float center[3];
    float extent[3];
    float rotation[4];
} GrMapCollider;

typedef struct {
    const char *name;
    const unsigned char *data;
    uint32_t size;
} GrMapMaterialSource;

typedef struct {
    GrRenderVertex *vertices;
    uint32_t *indices;
    GrRenderPart *parts;
    GrRenderMaterial *materials;
    char (*texture_names)[48];
    GrMapCollider *colliders;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t part_count;
    uint32_t material_count;
    uint32_t collider_count;
    float bounds[6];
    char resource_name[96];
} GrMap;

int map_load(GrMap *map, const GrAsset *asset, const char *resource_name);
int map_load_start(GrMap *map);
int map_reload_near_player(GrMap *map, const float position[3]);
void map_draw(const GrMap *map);
void map_resolve_player(const GrMap *map, float position[3], float eye_height, float radius);
const GrRenderMaterial *map_materials(const GrMap *map);
uint32_t map_material_count(const GrMap *map);
void map_destroy(GrMap *map);
const char *map_error(void);

#endif
