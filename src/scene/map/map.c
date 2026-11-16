#include "map.h"

#include "assets.h"
#include "dbglog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP_INDEX_PATH "3d/maps/MAPIDX.BIN"
#define MAP_INDEX_HEADER_SIZE 20u
#define MAP_INDEX_SAMPLE_COUNT 16u
#define MAP_INDEX_RECORD_SIZE 360u
#define MAP_INDEX_PATH_SIZE 128u
#define GMAP_HEADER_SIZE 96u
#define GMAP_VERTEX_SIZE 32u
#define GMAP_BATCH_SIZE 16u
#define GMAP_MATERIAL_SIZE 112u
#define GCOL_HEADER_SIZE 32u
#define GCOL_SHAPE_SIZE 48u
#define WORLD_MAX_MAPS 256u
#define WORLD_EPSILON 1.0e-5f
#define MAP_STREAM_HYSTERESIS 2.0f

typedef struct WorldIndexEntry {
    char path[MAP_INDEX_PATH_SIZE];
    float bounds[6];
    float center[3];
    float samples[MAP_INDEX_SAMPLE_COUNT][3];
    float extent2;
} WorldIndexEntry;

typedef struct MapMaterial {
    char name[48];
    char texture[48];
    uint32_t flags;
} MapMaterial;

typedef struct MapCollider {
    uint32_t type;
    uint32_t flags;
    float center[3];
    float dimensions[3];
    float rotation[4];
} MapCollider;

typedef struct ResidentMap {
    int loaded;
    unsigned index;
    unsigned vertex_count;
    unsigned index_count;
    unsigned part_count;
    unsigned material_count;
    unsigned collider_count;
    unsigned material_base;
    GrRenderVertex *vertices;
    uint32_t *indices;
    GrRenderPart *parts;
    GrRenderPart *draw_parts;
    MapMaterial *materials;
    MapCollider *colliders;
} ResidentMap;

typedef struct WorldState {
    WorldIndexEntry *index;
    unsigned index_count;
    ResidentMap maps[GR_WORLD_RESIDENT_MAPS];
} WorldState;

static char map_error_text[256];

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static float read_f32(const unsigned char *p)
{
    uint32_t bits = read_u32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int range_valid(uint32_t offset, uint32_t count, uint32_t stride,
                       uint32_t minimum, uint32_t size)
{
    return offset >= minimum && (offset & 15u) == 0 && offset <= size &&
           (!stride || count <= (size - offset) / stride);
}

static int finite3(const float v[3])
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static int read_map_index(WorldState *state)
{
    const GrAsset *asset = assets_find(MAP_INDEX_PATH);
    const unsigned char *data;
    uint32_t count, record_size, total_size;
    unsigned i;
    if (!asset || !asset->data || asset->size < MAP_INDEX_HEADER_SIZE) {
        snprintf(map_error_text, sizeof(map_error_text), "map index missing: %s",
                 assets_error());
        return 0;
    }
    data = asset->data;
    count = read_u32(data + 8);
    record_size = read_u32(data + 12);
    total_size = read_u32(data + 16);
    if (memcmp(data, "GMIX", 4) || read_u32(data + 4) != 1u ||
        !count || count > WORLD_MAX_MAPS || record_size != MAP_INDEX_RECORD_SIZE ||
        total_size != asset->size || count > (asset->size - MAP_INDEX_HEADER_SIZE) / record_size ||
        MAP_INDEX_HEADER_SIZE + count * record_size != asset->size) {
        snprintf(map_error_text, sizeof(map_error_text), "invalid GMIX map index header");
        assets_release(asset);
        return 0;
    }
    state->index = (WorldIndexEntry *)calloc(count, sizeof(*state->index));
    if (!state->index) {
        snprintf(map_error_text, sizeof(map_error_text), "not enough RAM for map index");
        assets_release(asset);
        return 0;
    }
    state->index_count = count;
    for (i = 0; i < count; ++i) {
        const unsigned char *record = data + MAP_INDEX_HEADER_SIZE + i * record_size;
        const unsigned char *end = (const unsigned char *)memchr(record, 0, MAP_INDEX_PATH_SIZE);
        WorldIndexEntry *entry = &state->index[i];
        unsigned axis;
        if (!end || end == record) goto bad_index;
        memcpy(entry->path, record, (size_t)(end - record));
        entry->path[end - record] = '\0';
        if (entry->path[0] == '/' || strstr(entry->path, "..") ||
            !strstr(entry->path, "3d/maps/")) goto bad_index;
        for (axis = 0; axis < 6; ++axis) {
            entry->bounds[axis] = read_f32(record + MAP_INDEX_PATH_SIZE + axis * 4);
            if (!isfinite(entry->bounds[axis])) goto bad_index;
        }
        for (axis = 0; axis < 3; ++axis) {
            float extent = entry->bounds[axis + 3] - entry->bounds[axis];
            if (extent < 0.0f || extent > 100000.0f) goto bad_index;
            entry->center[axis] = (entry->bounds[axis] + entry->bounds[axis + 3]) * 0.5f;
            entry->extent2 += extent * extent;
        }
        for (axis = 0; axis < MAP_INDEX_SAMPLE_COUNT * 3u; ++axis) {
            float sample = read_f32(record + MAP_INDEX_PATH_SIZE + 6u * 4u + 4u * 4u + axis * 4u);
            if (!isfinite(sample) || fabsf(sample) > 100000.0f) goto bad_index;
            entry->samples[axis / 3u][axis % 3u] = sample;
        }
    }
    assets_release(asset);
    map_error_text[0] = '\0';
    debug_log("MAP index ready entries=%u bytes=%u\n", state->index_count,
              (unsigned)asset->size);
    return 1;

bad_index:
    assets_release(asset);
    free(state->index);
    state->index = NULL;
    state->index_count = 0;
    snprintf(map_error_text, sizeof(map_error_text), "unsafe or invalid map index record %u", i);
    return 0;
}

static void map_release(ResidentMap *map)
{
    if (!map) return;
    free(map->vertices);
    free(map->indices);
    free(map->parts);
    free(map->draw_parts);
    free(map->materials);
    free(map->colliders);
    memset(map, 0, sizeof(*map));
    map->index = (unsigned)-1;
}

static int load_map(WorldState *state, unsigned index, ResidentMap *out)
{
    const GrAsset *asset;
    const unsigned char *data;
    uint32_t size, vertices, indices, batches, materials;
    uint32_t vertex_offset, index_offset, batch_offset, material_offset;
    uint32_t index_size, collision_offset, collision_count, collision_size;
    uint32_t vertex_end, index_end, batch_end, material_end;
    uint32_t i;
    ResidentMap map;
    memset(&map, 0, sizeof(map));
    map.index = (unsigned)-1;
    if (index >= state->index_count) return 0;
    asset = assets_find(state->index[index].path);
    if (!asset || !asset->data || asset->size < GMAP_HEADER_SIZE) {
        snprintf(map_error_text, sizeof(map_error_text), "cannot load %s: %s",
                 state->index[index].path, assets_error());
        return 0;
    }
    data = asset->data;
    size = asset->size;
    if (memcmp(data, "GMAP", 4) || read_u32(data + 4) != 2u ||
        read_u32(data + 8) != size) goto bad_file;
    vertices = read_u32(data + 12);
    indices = read_u32(data + 16);
    batches = read_u32(data + 20);
    materials = read_u32(data + 24);
    vertex_offset = read_u32(data + 28);
    index_offset = read_u32(data + 32);
    batch_offset = read_u32(data + 36);
    material_offset = read_u32(data + 40);
    index_size = read_u32(data + 68);
    collision_offset = read_u32(data + 72);
    collision_count = read_u32(data + 76);
    if (!vertices || !indices || indices % 3u || !batches || !materials ||
        index_size != 2u || vertices > 65536u ||
        !range_valid(vertex_offset, vertices, GMAP_VERTEX_SIZE, GMAP_HEADER_SIZE, size) ||
        !range_valid(index_offset, indices, index_size, GMAP_HEADER_SIZE, size) ||
        !range_valid(batch_offset, batches, GMAP_BATCH_SIZE, GMAP_HEADER_SIZE, size) ||
        !range_valid(material_offset, materials, GMAP_MATERIAL_SIZE, GMAP_HEADER_SIZE, size) ||
        collision_offset < GMAP_HEADER_SIZE || (collision_offset & 15u) ||
        collision_offset > size || size - collision_offset < GCOL_HEADER_SIZE ||
        collision_count > (size - collision_offset - GCOL_HEADER_SIZE) / GCOL_SHAPE_SIZE)
        goto bad_file;
    vertex_end = vertex_offset + vertices * GMAP_VERTEX_SIZE;
    index_end = index_offset + indices * index_size;
    batch_end = batch_offset + batches * GMAP_BATCH_SIZE;
    material_end = material_offset + materials * GMAP_MATERIAL_SIZE;
    if (index_offset < vertex_end || batch_offset < index_end ||
        material_offset < batch_end || collision_offset < material_end)
        goto bad_file;
    collision_size = read_u32(data + collision_offset + 8);
    if (memcmp(data + collision_offset, "GCOL", 4) ||
        read_u32(data + collision_offset + 4) != 1u ||
        collision_size != size - collision_offset ||
        collision_size < GCOL_HEADER_SIZE + collision_count * GCOL_SHAPE_SIZE ||
        read_u32(data + collision_offset + 12) != collision_count)
        goto bad_file;

    map.vertices = (GrRenderVertex *)calloc(vertices, sizeof(*map.vertices));
    map.indices = (uint32_t *)malloc((size_t)indices * sizeof(*map.indices));
    map.parts = (GrRenderPart *)calloc(batches, sizeof(*map.parts));
    map.draw_parts = (GrRenderPart *)calloc(batches, sizeof(*map.draw_parts));
    map.materials = (MapMaterial *)calloc(materials, sizeof(*map.materials));
    map.colliders = collision_count ? (MapCollider *)calloc(collision_count, sizeof(*map.colliders)) : NULL;
    if (!map.vertices || !map.indices || !map.parts || !map.draw_parts ||
        !map.materials || (collision_count && !map.colliders)) goto no_memory;
    map.vertex_count = vertices;
    map.index_count = indices;
    map.part_count = batches;
    map.material_count = materials;
    map.collider_count = collision_count;
    map.index = index;

    for (i = 0; i < vertices; ++i) {
        const unsigned char *v = data + vertex_offset + i * GMAP_VERTEX_SIZE;
        GrRenderVertex *o = &map.vertices[i];
        int nx = (int)(int8_t)v[20], ny = (int)(int8_t)v[21], nz = (int)(int8_t)v[22];
        o->x = read_f32(v);
        o->y = read_f32(v + 4);
        o->z = read_f32(v + 8);
        o->u = read_f32(v + 12);
        o->v = read_f32(v + 16);
        o->nx = (float)nx / 127.0f;
        o->ny = (float)ny / 127.0f;
        o->nz = (float)nz / 127.0f;
        if (o->nx < -1.0f) o->nx = -1.0f;
        if (o->ny < -1.0f) o->ny = -1.0f;
        if (o->nz < -1.0f) o->nz = -1.0f;
        if (!isfinite(o->x) || !isfinite(o->y) || !isfinite(o->z) ||
            !isfinite(o->u) || !isfinite(o->v)) goto bad_map;
    }
    for (i = 0; i < indices; ++i) {
        map.indices[i] = read_u16(data + index_offset + i * 2u);
        if (map.indices[i] >= vertices) goto bad_map;
    }
    for (i = 0; i < batches; ++i) {
        const unsigned char *p = data + batch_offset + i * GMAP_BATCH_SIZE;
        GrRenderPart *part = &map.parts[i];
        part->first_index = read_u32(p);
        part->index_count = read_u32(p + 4);
        part->material_index = read_u32(p + 8);
        part->flags = read_u32(p + 12);
        if (part->first_index > indices || part->index_count > indices - part->first_index ||
            part->index_count % 3u || part->material_index >= materials) goto bad_map;
        map.draw_parts[i] = *part;
    }
    for (i = 0; i < materials; ++i) {
        const unsigned char *m = data + material_offset + i * GMAP_MATERIAL_SIZE;
        MapMaterial *out_mat = &map.materials[i];
        if (!memchr(m, 0, 48) || !memchr(m + 48, 0, 48)) goto bad_map;
        memcpy(out_mat->name, m, sizeof(out_mat->name));
        memcpy(out_mat->texture, m + 48, sizeof(out_mat->texture));
        out_mat->flags = read_u32(m + 96);
    }
    for (i = 0; i < collision_count; ++i) {
        const unsigned char *c = data + collision_offset + GCOL_HEADER_SIZE + i * GCOL_SHAPE_SIZE;
        MapCollider *shape = &map.colliders[i];
        unsigned k;
        shape->type = read_u32(c);
        shape->flags = read_u32(c + 4);
        for (k = 0; k < 3; ++k) {
            shape->center[k] = read_f32(c + 8 + k * 4);
            shape->dimensions[k] = read_f32(c + 20 + k * 4);
            if (!isfinite(shape->center[k]) || !isfinite(shape->dimensions[k])) goto bad_map;
        }
        for (k = 0; k < 4; ++k) {
            shape->rotation[k] = read_f32(c + 32 + k * 4);
            if (!isfinite(shape->rotation[k])) goto bad_map;
        }
        if (shape->type > 2u) goto bad_map;
        if (shape->type == 0u && (shape->dimensions[0] < 0.0f ||
                                  shape->dimensions[1] < 0.0f ||
                                  shape->dimensions[2] < 0.0f)) goto bad_map;
        if (shape->type == 1u && shape->dimensions[0] < 0.0f) goto bad_map;
        if (shape->type == 2u &&
            (shape->dimensions[0] < 0.0f || shape->dimensions[1] < 0.0f ||
             shape->dimensions[2] < 0.0f || shape->dimensions[2] > 2.0f ||
             fabsf(shape->dimensions[2] - floorf(shape->dimensions[2] + 0.5f)) > 0.001f))
            goto bad_map;
        if (shape->type != 1u) {
            float q2 = shape->rotation[0] * shape->rotation[0] +
                       shape->rotation[1] * shape->rotation[1] +
                       shape->rotation[2] * shape->rotation[2] +
                       shape->rotation[3] * shape->rotation[3];
            if (q2 < 0.25f || q2 > 4.0f) goto bad_map;
        }
    }
    map.loaded = 1;
    assets_release(asset);
    *out = map;
    debug_log("MAP loaded name=%s vertices=%u triangles=%u materials=%u colliders=%u bytes=%u\n",
              state->index[index].path, vertices, indices / 3u, materials,
              collision_count, size);
    return 1;

no_memory:
    snprintf(map_error_text, sizeof(map_error_text), "not enough RAM for map %s",
             state->index[index].path);
    map_release(&map);
    assets_release(asset);
    return 0;
bad_map:
    snprintf(map_error_text, sizeof(map_error_text), "invalid GMAP section in %s",
             state->index[index].path);
    map_release(&map);
    assets_release(asset);
    return 0;
bad_file:
    snprintf(map_error_text, sizeof(map_error_text), "invalid GMAP 2 header in %s",
             state->index[index].path);
    assets_release(asset);
    return 0;
}

static float point_aabb_distance2(const float p[3], const float b[6])
{
    float d2 = 0.0f;
    unsigned i;
    for (i = 0; i < 3; ++i) {
        float d = p[i] < b[i] ? b[i] - p[i] : p[i] > b[i + 3] ? p[i] - b[i + 3] : 0.0f;
        d2 += d * d;
    }
    return d2;
}

static float map_score(const WorldIndexEntry *entry, const float p[3])
{
    float nearest2 = 1.0e30f;
    float bounds_d = sqrtf(point_aabb_distance2(p, entry->bounds));
    unsigned i;
    for (i = 0; i < MAP_INDEX_SAMPLE_COUNT; ++i) {
        float dx = p[0] - entry->samples[i][0];
        float dy = p[1] - entry->samples[i][1];
        float dz = p[2] - entry->samples[i][2];
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < nearest2) nearest2 = d2;
    }
    /* GMIX samples represent actual geometry; AABB is only a small tie-breaker. */
    return sqrtf(nearest2) + bounds_d * 0.05f;
}

static int contains_index(const unsigned *target, unsigned index)
{
    return target[0] == index || target[1] == index;
}

int world_init(GrWorld *world)
{
    WorldState *state;
    unsigned i;
    if (!world) return 0;
    world->state = NULL;
    map_error_text[0] = '\0';
    state = (WorldState *)calloc(1, sizeof(*state));
    if (!state) {
        snprintf(map_error_text, sizeof(map_error_text), "not enough RAM for world manager");
        return 0;
    }
    for (i = 0; i < GR_WORLD_RESIDENT_MAPS; ++i) state->maps[i].index = (unsigned)-1;
    if (!read_map_index(state)) {
        free(state->index);
        free(state);
        return 0;
    }
    world->state = state;
    return 1;
}

void world_shutdown(GrWorld *world)
{
    WorldState *state;
    unsigned i;
    if (!world || !world->state) return;
    state = (WorldState *)world->state;
    for (i = 0; i < GR_WORLD_RESIDENT_MAPS; ++i) map_release(&state->maps[i]);
    free(state->index);
    free(state);
    world->state = NULL;
}

int world_update(GrWorld *world, const float position[3])
{
    WorldState *state;
    unsigned best[GR_WORLD_RESIDENT_MAPS] = {(unsigned)-1, (unsigned)-1};
    float best_score[GR_WORLD_RESIDENT_MAPS] = {1.0e30f, 1.0e30f};
    unsigned i, slot, rank;
    int changed = 0;
    if (!world || !world->state || !position || !finite3(position)) {
        snprintf(map_error_text, sizeof(map_error_text), "invalid world update arguments");
        return -1;
    }
    state = (WorldState *)world->state;
    for (i = 0; i < state->index_count; ++i) {
        float score = map_score(&state->index[i], position);
        for (rank = 0; rank < GR_WORLD_RESIDENT_MAPS; ++rank) {
            if (score < best_score[rank]) {
                unsigned shift;
                for (shift = GR_WORLD_RESIDENT_MAPS - 1; shift > rank; --shift) {
                    best[shift] = best[shift - 1];
                    best_score[shift] = best_score[shift - 1];
                }
                best[rank] = i;
                best_score[rank] = score;
                break;
            }
        }
    }
    /* Keep a resident chunk until a replacement is clearly closer. */
    for (slot = 0; slot < GR_WORLD_RESIDENT_MAPS; ++slot) {
        ResidentMap *resident = &state->maps[slot];
        if (resident->loaded && !contains_index(best, resident->index)) {
            float score = map_score(&state->index[resident->index], position);
            unsigned last = GR_WORLD_RESIDENT_MAPS - 1u;
            if (score <= best_score[last] + MAP_STREAM_HYSTERESIS) {
                best[last] = resident->index;
                best_score[last] = score;
                if (best_score[last] < best_score[0]) {
                    unsigned tmp_index = best[0];
                    float tmp_score = best_score[0];
                    best[0] = best[last];
                    best_score[0] = best_score[last];
                    best[last] = tmp_index;
                    best_score[last] = tmp_score;
                }
            }
        }
    }
    if (best[0] == (unsigned)-1 || best[1] == (unsigned)-1) {
        snprintf(map_error_text, sizeof(map_error_text), "map index has fewer than two maps");
        return -1;
    }
    for (slot = 0; slot < GR_WORLD_RESIDENT_MAPS; ++slot) {
        ResidentMap *map = &state->maps[slot];
        if (map->loaded && !contains_index(best, map->index)) {
            map_release(map);
            changed = 1;
        }
    }
    for (rank = 0; rank < GR_WORLD_RESIDENT_MAPS; ++rank) {
        int present = 0;
        for (slot = 0; slot < GR_WORLD_RESIDENT_MAPS; ++slot)
            if (state->maps[slot].loaded && state->maps[slot].index == best[rank]) present = 1;
        if (!present) {
            for (slot = 0; slot < GR_WORLD_RESIDENT_MAPS; ++slot)
                if (!state->maps[slot].loaded) break;
            if (slot == GR_WORLD_RESIDENT_MAPS || !load_map(state, best[rank], &state->maps[slot]))
                return -1;
            changed = 1;
        }
    }
    if (changed) {
        debug_log("MAP resident pair position=%.3f,%.3f,%.3f score=%.2f/%.2f index=%u,%u\n",
                  position[0], position[1], position[2], best_score[0], best_score[1],
                  best[0], best[1]);
        map_error_text[0] = '\0';
    }
    return changed;
}

unsigned world_material_count(const GrWorld *world)
{
    const WorldState *state = world ? (const WorldState *)world->state : NULL;
    unsigned total = 0, i;
    if (!state) return 0;
    for (i = 0; i < GR_WORLD_RESIDENT_MAPS; ++i)
        if (state->maps[i].loaded) total += state->maps[i].material_count;
    return total;
}

int world_fill_materials(GrWorld *world, GrRenderMaterial *out,
                         unsigned capacity, unsigned base_material)
{
    WorldState *state = world ? (WorldState *)world->state : NULL;
    unsigned slot, cursor = 0;
    if (!state || (capacity && !out) || capacity < world_material_count(world)) return 0;
    for (slot = 0; slot < GR_WORLD_RESIDENT_MAPS; ++slot) {
        ResidentMap *map = &state->maps[slot];
        unsigned i, p;
        if (!map->loaded) continue;
        map->material_base = base_material + cursor;
        for (p = 0; p < map->part_count; ++p) {
            map->draw_parts[p] = map->parts[p];
            map->draw_parts[p].material_index += map->material_base;
        }
        for (i = 0; i < map->material_count; ++i) {
            GrRenderMaterial *dst = &out[cursor + i];
            const GrAsset *texture = NULL;
            unsigned c;
            memset(dst, 0, sizeof(*dst));
            for (c = 0; c < 4; ++c) dst->rgba[c] = 1.0f;
            dst->flags = map->materials[i].flags;
            dst->texture_name = map->materials[i].texture[0] ? map->materials[i].texture : NULL;
            if (dst->texture_name) {
                texture = assets_find_texture(dst->texture_name);
                if (texture) {
                    dst->texture_data = texture->data;
                    dst->texture_size = texture->size;
                } else {
                    debug_log("MAP texture missing map=%u material=%s; using white fallback\n",
                              map->index, dst->texture_name);
                    dst->texture_name = NULL;
                }
            }
        }
        cursor += map->material_count;
    }
    return 1;
}

void world_draw(GrWorld *world)
{
    WorldState *state = world ? (WorldState *)world->state : NULL;
    unsigned i;
    if (!state) return;
    for (i = 0; i < GR_WORLD_RESIDENT_MAPS; ++i) {
        ResidentMap *map = &state->maps[i];
        if (!map->loaded) continue;
        renderer_draw(map->vertices, map->vertex_count, map->indices, map->index_count,
                      map->draw_parts, map->part_count);
    }
}

static void quat_rotate(const float q[4], const float v[3], float out[3])
{
    float n2 = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
    float x, y, z, w, tx, ty, tz;
    if (n2 < WORLD_EPSILON) {
        out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
        return;
    }
    {
        float inv = 1.0f / sqrtf(n2);
        x = q[0] * inv; y = q[1] * inv; z = q[2] * inv; w = q[3] * inv;
    }
    tx = 2.0f * (y * v[2] - z * v[1]);
    ty = 2.0f * (z * v[0] - x * v[2]);
    tz = 2.0f * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

static void point_to_local(const MapCollider *shape, const float p[3], float out[3])
{
    float q[4] = {-shape->rotation[0], -shape->rotation[1],
                  -shape->rotation[2], shape->rotation[3]};
    float d[3] = {p[0] - shape->center[0], p[1] - shape->center[1],
                  p[2] - shape->center[2]};
    quat_rotate(q, d, out);
}

static float point_segment_distance2(const float p[3], const float a[3], const float b[3])
{
    float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    float den = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    float t = den > WORLD_EPSILON ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / den : 0.0f;
    float dx, dy, dz;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    dx = p[0] - (a[0] + ab[0] * t);
    dy = p[1] - (a[1] + ab[1] * t);
    dz = p[2] - (a[2] + ab[2] * t);
    return dx * dx + dy * dy + dz * dz;
}

static float segment_segment_distance2(const float p1[3], const float q1[3],
                                       const float p2[3], const float q2[3])
{
    float d1[3], d2[3], r[3], a, e, f, s, t, c, b, den;
    unsigned i;
    for (i = 0; i < 3; ++i) {
        d1[i] = q1[i] - p1[i]; d2[i] = q2[i] - p2[i]; r[i] = p1[i] - p2[i];
    }
    a = d1[0]*d1[0] + d1[1]*d1[1] + d1[2]*d1[2];
    e = d2[0]*d2[0] + d2[1]*d2[1] + d2[2]*d2[2];
    f = d2[0]*r[0] + d2[1]*r[1] + d2[2]*r[2];
    if (a <= WORLD_EPSILON && e <= WORLD_EPSILON)
        return r[0]*r[0] + r[1]*r[1] + r[2]*r[2];
    if (a <= WORLD_EPSILON) { s = 0.0f; t = f / e; }
    else {
        c = d1[0]*r[0] + d1[1]*r[1] + d1[2]*r[2];
        if (e <= WORLD_EPSILON) { t = 0.0f; s = -c / a; }
        else {
            b = d1[0]*d2[0] + d1[1]*d2[1] + d1[2]*d2[2];
            den = a * e - b * b;
            s = den != 0.0f ? (b * f - c * e) / den : 0.0f;
            t = (b * s + f) / e;
        }
    }
    if (s < 0.0f) { s = 0.0f; t = f / e; }
    else if (s > 1.0f) { s = 1.0f; t = (f + (d1[0]*d2[0] + d1[1]*d2[1] + d1[2]*d2[2])) / e; }
    if (t < 0.0f) { t = 0.0f; s = -(d1[0]*r[0] + d1[1]*r[1] + d1[2]*r[2]) / a; if (s < 0.0f) s = 0.0f; else if (s > 1.0f) s = 1.0f; }
    else if (t > 1.0f) { t = 1.0f; s = (d1[0]*(q2[0]-p1[0]) + d1[1]*(q2[1]-p1[1]) + d1[2]*(q2[2]-p1[2])) / a; if (s < 0.0f) s = 0.0f; else if (s > 1.0f) s = 1.0f; }
    {
        float dx = r[0] + d1[0]*s - d2[0]*t;
        float dy = r[1] + d1[1]*s - d2[1]*t;
        float dz = r[2] + d1[2]*s - d2[2]*t;
        return dx*dx + dy*dy + dz*dz;
    }
}

static float point_box_distance2(const float p[3], const float half[3])
{
    float d2 = 0.0f;
    unsigned i;
    for (i = 0; i < 3; ++i) {
        float v = fabsf(p[i]) - half[i];
        if (v > 0.0f) d2 += v * v;
    }
    return d2;
}

static float segment_box_distance2(const float a[3], const float b[3], const float half[3])
{
    float lo = 0.0f, hi = 1.0f;
    unsigned i;
    for (i = 0; i < 18; ++i) {
        float m1 = (2.0f * lo + hi) / 3.0f;
        float m2 = (lo + 2.0f * hi) / 3.0f;
        float p1[3], p2[3];
        unsigned k;
        for (k = 0; k < 3; ++k) { p1[k] = a[k] + (b[k] - a[k]) * m1; p2[k] = a[k] + (b[k] - a[k]) * m2; }
        if (point_box_distance2(p1, half) < point_box_distance2(p2, half)) hi = m2;
        else lo = m1;
    }
    {
        float t = (lo + hi) * 0.5f, p[3];
        for (i = 0; i < 3; ++i) p[i] = a[i] + (b[i] - a[i]) * t;
        return point_box_distance2(p, half);
    }
}

static int capsule_intersects_shape(const float feet[3], float radius, float height,
                                    const MapCollider *shape)
{
    float p0[3] = {feet[0], feet[1] + radius, feet[2]};
    float p1[3] = {feet[0], feet[1] + height - radius, feet[2]};
    float r2;
    if (shape->flags & 1u) return 0;
    if (shape->type == 0u) {
        float a[3], b[3];
        point_to_local(shape, p0, a);
        point_to_local(shape, p1, b);
        r2 = radius * radius;
        return segment_box_distance2(a, b, shape->dimensions) < r2;
    }
    if (shape->type == 1u) {
        r2 = radius + shape->dimensions[0];
        return point_segment_distance2(shape->center, p0, p1) < r2 * r2;
    }
    if (shape->type == 2u) {
        float axis_local[3] = {0.0f, 0.0f, 0.0f}, axis[3], a[3], b[3];
        unsigned direction = (unsigned)(shape->dimensions[2] < 0.0f ? 1.0f : shape->dimensions[2]);
        float sum = shape->rotation[0]*shape->rotation[0] + shape->rotation[1]*shape->rotation[1] +
                    shape->rotation[2]*shape->rotation[2] + shape->rotation[3]*shape->rotation[3];
        if (direction > 2u) direction = 1u;
        axis_local[direction] = 1.0f;
        if (sum < WORLD_EPSILON) {
            axis[0] = axis_local[0]; axis[1] = axis_local[1]; axis[2] = axis_local[2];
        } else quat_rotate(shape->rotation, axis_local, axis);
        a[0] = shape->center[0] - axis[0]*shape->dimensions[1];
        a[1] = shape->center[1] - axis[1]*shape->dimensions[1];
        a[2] = shape->center[2] - axis[2]*shape->dimensions[1];
        b[0] = shape->center[0] + axis[0]*shape->dimensions[1];
        b[1] = shape->center[1] + axis[1]*shape->dimensions[1];
        b[2] = shape->center[2] + axis[2]*shape->dimensions[1];
        r2 = radius + shape->dimensions[0];
        return segment_segment_distance2(p0, p1, a, b) < r2 * r2;
    }
    return 0;
}

int world_capsule_blocked(const GrWorld *world, float x, float feet_y, float z,
                          float radius, float height)
{
    const WorldState *state = world ? (const WorldState *)world->state : NULL;
    float feet[3] = {x, feet_y, z};
    unsigned m, c;
    if (!state || !finite3(feet) || !isfinite(radius) || !isfinite(height) ||
        radius <= 0.0f || height < radius * 2.0f) return 0;
    for (m = 0; m < GR_WORLD_RESIDENT_MAPS; ++m) {
        const ResidentMap *map = &state->maps[m];
        if (!map->loaded) continue;
        for (c = 0; c < map->collider_count; ++c)
            if (capsule_intersects_shape(feet, radius, height, &map->colliders[c])) return 1;
    }
    return 0;
}

const char *world_error(void)
{
    return map_error_text[0] ? map_error_text : "unknown map error";
}
