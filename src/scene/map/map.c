#include "map.h"
#include "dbglog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP_HEADER_SIZE 96u
#define MAP_VERTEX_STRIDE 32u
#define MAP_PART_STRIDE 16u
#define MAP_MATERIAL_STRIDE 112u
#define MAP_COLLIDER_STRIDE 48u
#define MAP_START_RESOURCE "3d/maps/Scene/Scene_92.map"
#define PLAYER_DEFAULT_EYE 1.65f
#define PLAYER_DEFAULT_RADIUS 0.28f

static char error_text[256];

static void fail(const char *message)
{
    snprintf(error_text, sizeof(error_text), "%s", message ? message : "map error");
}

static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float read_float(const unsigned char *p)
{
    float value;
    uint32_t bits = read32(p);
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int range_ok(uint32_t offset, uint32_t bytes, uint32_t size)
{
    return offset <= size && bytes <= size - offset;
}

static int finite3(const float value[3])
{
    return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static void free_map_arrays(GrMap *map)
{
    free(map->vertices);
    free(map->indices);
    free(map->parts);
    free(map->materials);
    free(map->texture_names);
    free(map->colliders);
    map->vertices = NULL;
    map->indices = NULL;
    map->parts = NULL;
    map->materials = NULL;
    map->texture_names = NULL;
    map->colliders = NULL;
    map->vertex_count = map->index_count = map->part_count = 0;
    map->material_count = map->collider_count = 0;
}

static int read_collision(GrMap *map, const unsigned char *data, uint32_t size,
                          uint32_t offset, uint32_t expected_count)
{
    uint32_t count, i;
    if (!expected_count)
        return 1;
    if (!range_ok(offset, 32, size) || memcmp(data + offset, "GCOL", 4) != 0) {
        fail("GMAP collision block is missing or invalid");
        return 0;
    }
    count = read32(data + offset + 12);
    if (count != expected_count || count > 4096u ||
        !range_ok(offset + 32u, count * MAP_COLLIDER_STRIDE, size)) {
        fail("GMAP collision count or range is invalid");
        return 0;
    }
    map->colliders = calloc(count, sizeof(*map->colliders));
    if (!map->colliders) {
        fail("GMAP collision allocation failed");
        return 0;
    }
    map->collider_count = count;
    for (i = 0; i < count; ++i) {
        const unsigned char *src = data + offset + 32u + i * MAP_COLLIDER_STRIDE;
        GrMapCollider *dst = &map->colliders[i];
        int j;
        dst->type = read32(src);
        dst->flags = read32(src + 4);
        for (j = 0; j < 3; ++j) {
            dst->center[j] = read_float(src + 8 + j * 4);
            dst->extent[j] = read_float(src + 20 + j * 4);
        }
        for (j = 0; j < 4; ++j)
            dst->rotation[j] = read_float(src + 32 + j * 4);
        if (!finite3(dst->center) || !finite3(dst->extent)) {
            fail("GMAP collision contains a non-finite value");
            return 0;
        }
    }
    return 1;
}

int map_load(GrMap *map, const GrAsset *asset, const char *resource_name)
{
    const unsigned char *data;
    uint32_t size, vertex_count, index_count, part_count, material_count;
    uint32_t vertex_offset, index_offset, part_offset, material_offset;
    uint32_t index_size, collision_offset, collision_count;
    uint32_t i;
    if (!map || !asset || !asset->data || !resource_name) {
        fail("GMAP received invalid arguments");
        return 0;
    }
    memset(map, 0, sizeof(*map));
    error_text[0] = '\0';
    data = asset->data;
    size = asset->size;
    if (size < MAP_HEADER_SIZE || memcmp(data, "GMAP", 4) != 0 || read32(data + 4) != 2u) {
        fail("unsupported GMAP header or version");
        return 0;
    }
    if (read32(data + 8) > size) {
        fail("GMAP declared size exceeds DATA.PAK resource");
        return 0;
    }
    vertex_count = read32(data + 12);
    index_count = read32(data + 16);
    part_count = read32(data + 20);
    material_count = read32(data + 24);
    vertex_offset = read32(data + 28);
    index_offset = read32(data + 32);
    part_offset = read32(data + 36);
    material_offset = read32(data + 40);
    index_size = read32(data + 68);
    collision_offset = read32(data + 72);
    collision_count = read32(data + 76);
    if (vertex_count > 65536u || index_count > 393216u || part_count > 512u ||
        material_count > 64u || index_size != 2u ||
        !range_ok(vertex_offset, vertex_count * MAP_VERTEX_STRIDE, size) ||
        !range_ok(index_offset, index_count * index_size, size) ||
        !range_ok(part_offset, part_count * MAP_PART_STRIDE, size) ||
        !range_ok(material_offset, material_count * MAP_MATERIAL_STRIDE, size)) {
        fail("GMAP counts or section offsets exceed runtime limits");
        return 0;
    }
    for (i = 0; i < 6; ++i)
        map->bounds[i] = read_float(data + 44 + i * 4);
    if (!finite3(map->bounds) || !finite3(map->bounds + 3)) {
        fail("GMAP bounds are not finite");
        return 0;
    }
    map->vertices = calloc(vertex_count, sizeof(*map->vertices));
    map->indices = calloc(index_count, sizeof(*map->indices));
    map->parts = calloc(part_count, sizeof(*map->parts));
    map->materials = calloc(material_count, sizeof(*map->materials));
    map->texture_names = calloc(material_count, sizeof(*map->texture_names));
    if ((vertex_count && !map->vertices) || (index_count && !map->indices) ||
        (part_count && !map->parts) || (material_count &&
         (!map->materials || !map->texture_names))) {
        fail("GMAP geometry allocation failed");
        free_map_arrays(map);
        return 0;
    }
    map->vertex_count = vertex_count;
    map->index_count = index_count;
    map->part_count = part_count;
    map->material_count = material_count;
    for (i = 0; i < vertex_count; ++i) {
        const unsigned char *src = data + vertex_offset + i * MAP_VERTEX_STRIDE;
        GrRenderVertex *dst = &map->vertices[i];
        dst->x = read_float(src);
        dst->y = read_float(src + 4);
        dst->z = read_float(src + 8);
        dst->u = read_float(src + 12);
        dst->v = read_float(src + 16);
        dst->nx = (float)(int8_t)src[20] / 127.0f;
        dst->ny = (float)(int8_t)src[21] / 127.0f;
        dst->nz = (float)(int8_t)src[22] / 127.0f;
        if (!isfinite(dst->x) || !isfinite(dst->y) || !isfinite(dst->z) ||
            !isfinite(dst->u) || !isfinite(dst->v)) {
            fail("GMAP vertex contains a non-finite value");
            free_map_arrays(map);
            return 0;
        }
    }
    for (i = 0; i < index_count; ++i) {
        uint32_t index = (uint32_t)data[index_offset + i * 2u] |
                         ((uint32_t)data[index_offset + i * 2u + 1u] << 8);
        if (index >= vertex_count) {
            fail("GMAP index points outside vertex buffer");
            free_map_arrays(map);
            return 0;
        }
        map->indices[i] = index;
    }
    for (i = 0; i < part_count; ++i) {
        const unsigned char *src = data + part_offset + i * MAP_PART_STRIDE;
        GrRenderPart *dst = &map->parts[i];
        dst->first_index = read32(src);
        dst->index_count = read32(src + 4);
        dst->material_index = read32(src + 8);
        dst->flags = read32(src + 12);
        if (dst->first_index > index_count || dst->index_count > index_count - dst->first_index ||
            dst->material_index >= material_count) {
            fail("GMAP part points outside a section");
            free_map_arrays(map);
            return 0;
        }
    }
    for (i = 0; i < material_count; ++i) {
        const unsigned char *src = data + material_offset + i * MAP_MATERIAL_STRIDE;
        GrRenderMaterial *dst = &map->materials[i];
        size_t length = 0;
        while (length < 47u && src[48 + length]) ++length;
        if (length >= sizeof(map->texture_names[i])) length = sizeof(map->texture_names[i]) - 1u;
        memcpy(map->texture_names[i], src + 48, length);
        map->texture_names[i][length] = '\0';
        dst->rgba[0] = dst->rgba[1] = dst->rgba[2] = dst->rgba[3] = 1.0f;
        dst->flags = read32(src + 96);
        dst->texture_name = map->texture_names[i];
        if (map->texture_names[i][0]) {
            const GrAsset *texture;
            texture = assets_find_texture(map->texture_names[i]);
            if (!texture) {
                fail("GMAP material texture is missing from DATA.PAK");
                free_map_arrays(map);
                return 0;
            }
            dst->texture_data = texture->data;
            dst->texture_size = texture->size;
        }
    }
    if (collision_count && !read_collision(map, data, size, collision_offset, collision_count)) {
        free_map_arrays(map);
        return 0;
    }
    snprintf(map->resource_name, sizeof(map->resource_name), "%s", resource_name);
    debug_log("MAP loaded name=%s vertices=%u indices=%u parts=%u materials=%u colliders=%u bounds=%f,%f,%f..%f,%f,%f\n",
              map->resource_name, (unsigned)vertex_count, (unsigned)index_count,
              (unsigned)part_count, (unsigned)material_count, (unsigned)collision_count,
              map->bounds[0], map->bounds[1], map->bounds[2],
              map->bounds[3], map->bounds[4], map->bounds[5]);
    return 1;
}

int map_load_start(GrMap *map)
{
    const GrAsset *asset = assets_find(MAP_START_RESOURCE);
    if (!asset) {
        fail("start GMAP resource is missing from DATA.PAK");
        return 0;
    }
    return map_load(map, asset, MAP_START_RESOURCE);
}

int map_reload_near_player(GrMap *map, const float position[3])
{
    (void)position;
    if (!map || !map->resource_name[0]) {
        fail("cannot reload map without an active map");
        return 0;
    }
    /* The first milestone deliberately keeps one bounded chunk resident. */
    return 1;
}

void map_draw(const GrMap *map)
{
    if (!map || !map->vertices || !map->indices || !map->parts)
        return;
    renderer_draw(map->vertices, map->vertex_count, map->indices,
                  map->index_count, map->parts, map->part_count);
}

static int inside_horizontal(const GrMapCollider *c, float x, float z, float radius)
{
    float hx = fabsf(c->extent[0]) + radius;
    float hz = fabsf(c->extent[2]) + radius;
    return x >= c->center[0] - hx && x <= c->center[0] + hx &&
           z >= c->center[2] - hz && z <= c->center[2] + hz;
}

void map_resolve_player(const GrMap *map, float position[3], float eye_height, float radius)
{
    uint32_t i;
    float best_floor = -100000.0f;
    if (!map || !position)
        return;
    for (i = 0; i < map->collider_count; ++i) {
        const GrMapCollider *c = &map->colliders[i];
        float top;
        if (c->flags & 1u || !inside_horizontal(c, position[0], position[2], radius))
            continue;
        top = c->center[1] + fabsf(c->extent[1]);
        if (top > best_floor && position[1] >= c->center[1] - fabsf(c->extent[1]) - 0.5f &&
            position[1] <= top + eye_height + 0.75f)
            best_floor = top;
    }
    if (best_floor > -99999.0f && position[1] < best_floor + eye_height)
        position[1] = best_floor + eye_height;
}

const GrRenderMaterial *map_materials(const GrMap *map)
{
    return map ? map->materials : NULL;
}

uint32_t map_material_count(const GrMap *map)
{
    return map ? map->material_count : 0;
}

void map_destroy(GrMap *map)
{
    if (!map) return;
    free_map_arrays(map);
    memset(map, 0, sizeof(*map));
}

const char *map_error(void)
{
    return error_text[0] ? error_text : "unknown map error";
}
