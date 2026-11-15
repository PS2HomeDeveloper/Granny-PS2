#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sifrpc.h>
#include <loadfile.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#include "assets.h"
#include "dbglog.h"
#include "dev_hud.h"
#include "map.h"
#include "model.h"
#include "player.h"
#include "renderer.h"

#define RUNTIME_PATH_CAP 512

static int build_runtime_path(char *out, size_t capacity,
                              const char *argv0, const char *relative_path)
{
    size_t prefix_length = 0;
    size_t relative_length = strlen(relative_path);
    const char *slash;

    if (argv0 && strstr(argv0, ":/")) {
        slash = strrchr(argv0, '/');
        if (slash)
            prefix_length = (size_t)(slash - argv0) + 1;
    }

    /* If argv[0] is only a filename, use the launcher's working directory. */
    if (prefix_length + relative_length + 1 > capacity)
        prefix_length = 0;
    if (relative_length + 1 > capacity)
        return 0;

    if (prefix_length)
        memcpy(out, argv0, prefix_length);
    memcpy(out + prefix_length, relative_path, relative_length + 1);
    return 1;
}

static void fatal(const char *stage, const char *message)
{
    debug_log("FATAL stage=%s message=%s\n", stage,
              message ? message : "(no detail)");
    printf("Granny: %s: %s\n", stage,
           message ? message : "(no detail)");
    fflush(stdout);
    SleepThread();
    for (;;) {}
}

static void place_granny(GrModel *granny)
{
    uint32_t i;
    const float granny_start[3] = {0.53000f, 7.31400f, -0.67000f};
    for (i = 0; i < granny->vertex_count; ++i) {
        granny->vertices[i].x += granny_start[0];
        granny->vertices[i].y += granny_start[1];
        granny->vertices[i].z += granny_start[2];
    }
}

static int load_scene_materials(const GrMap *map, GrModel *granny)
{
    unsigned map_count = map_material_count(map);
    unsigned granny_count = granny ? granny->material_count : 0;
    unsigned total = map_count + granny_count;
    GrRenderMaterial *materials = total ? calloc(total, sizeof(*materials)) : NULL;
    unsigned i;
    int result;
    if (total && !materials)
        return 0;
    if (map_count)
        memcpy(materials, map_materials(map), map_count * sizeof(*materials));
    if (granny_count) {
        memcpy(materials + map_count, granny->materials,
               granny_count * sizeof(*materials));
        for (i = 0; i < granny->part_count; ++i)
            granny->parts[i].material_index += map_count;
    }
    debug_log("MATERIALS reload map=%u granny=%u total=%u\n",
              map_count, granny_count, total);
    result = renderer_load_materials(materials, total);
    free(materials);
    return result == GR_RENDER_OK;
}

int main(int argc, char **argv)
{
    GrPlayer player = {0};
    GrModel granny = {0};
    GrMap map = {0};
    GrDevHud hud = {0};
    const GrAsset *font1, *font2;
    const GrAsset *mesh, *skeleton, *animation, *material;
    unsigned all_material_count;
    char package_path[RUNTIME_PATH_CAP];
    char package_sibling_path[RUNTIME_PATH_CAP];
    char log_path[RUNTIME_PATH_CAP];
    float direction[3];
    unsigned debug_frame = 0;
    int granny_loaded = 1;

    sceSifInitRpc(0);
    SifLoadFileInit();

    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0)
        fatal("CONTROLLER INIT FAILED", "Could not load SIO2MAN");
    if (SifLoadModule("rom0:PADMAN", 0, NULL) < 0)
        fatal("CONTROLLER INIT FAILED", "Could not load PADMAN");

    /* The ELF is launched from mass:; preserve the loader's mounted USB device. */
    if (fileXioInit() < 0)
        fatal("USB STORAGE INIT FAILED", "Could not initialize fileXio");
    if (!build_runtime_path(log_path, sizeof(log_path),
                            argc > 0 ? argv[0] : NULL, "debug_log.txt") ||
        !build_runtime_path(package_path, sizeof(package_path),
                            argc > 0 ? argv[0] : NULL, "assets/DATA.PAK") ||
        !build_runtime_path(package_sibling_path, sizeof(package_sibling_path),
                            argc > 0 ? argv[0] : NULL, "DATA.PAK"))
        fatal("RUNTIME PATH ERROR", "Could not build paths beside the ELF");
    debug_init(log_path, argc, argv);
    debug_log("BOOT 00: main reached fileXio; SIF RPC, loadfile, SIO2MAN and PADMAN initialized\n");
    debug_log("BOOT 01: fileXio initialized; exe=%s log=%s package_candidates=%s,%s\n",
              argc > 0 && argv[0] ? argv[0] : "(none)", log_path,
              package_path, package_sibling_path);

    debug_log("BOOT 02: opening DATA.PAK beside ELF or in assets\n");
    if (!assets_init_external(package_path) &&
        !assets_init_external(package_sibling_path) &&
        !assets_init_external("mass:/assets/DATA.PAK") &&
        !assets_init_external("mass:/DATA.PAK"))
        fatal("EXTERNAL DATA.PAK LOAD FAILED", assets_error());
    debug_log("BOOT 03: DATA.PAK index ready; resources are loaded on demand\n");

    debug_log("BOOT 04: loading bounded start map\n");
    if (!map_load_start(&map)) {
        debug_log("MAP LOAD FAILED but continuing with map disabled: %s\n", map_error());
        printf("Granny: map disabled: %s\n", map_error());
    }
    debug_log("BOOT 05: start map ready; bounded single-chunk residency enabled\n");

    debug_log("BOOT 06: initializing player/controller at Unity PlayerStartPos\n");
    if (!player_init(&player, 13.75f, 21.027f, 7.16f))
        fatal("PLAYER INIT FAILED", "Could not initialize controller");
    debug_log("BOOT 07: player/controller initialized position=%f,%f,%f\n",
              player.position[0], player.position[1], player.position[2]);

    debug_log("BOOT 08: locating font/font1.raw\n");
    font1 = assets_find("font/font1.raw");
    if (!font1)
        fatal("FONT1 RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 09: locating font/font2.raw\n");
    font2 = assets_find("font/font2.raw");
    if (!font2)
        fatal("FONT2 RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 10: fonts loaded font1=%u font2=%u bytes\n",
              (unsigned)font1->size, (unsigned)font2->size);

    debug_log("BOOT 11: locating Granny.mesh\n");
    mesh = assets_find("3d/skeletal/Granny/Granny.mesh");
    if (!mesh)
        fatal("MESH RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 12: locating Granny.skeleton\n");
    skeleton = assets_find("3d/skeletal/Granny/Granny.skeleton");
    if (!skeleton)
        fatal("SKELETON RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 13: locating Granny.anim\n");
    animation = assets_find("3d/skeletal/Granny/Granny.anim");
    if (!animation)
        fatal("ANIMATION RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 14: locating Granny.material\n");
    material = assets_find("3d/skeletal/Granny/Granny.material");
    if (!material)
        fatal("MATERIAL RESOURCE LOAD FAILED", assets_error());

    debug_log("BOOT 15: parsing model and resolving all material textures\n");
    if (!model_load(&granny, mesh, skeleton, animation, material))
        fatal("MODEL LOAD FAILED", model_error());
    debug_log("BOOT 16: model loaded vertices=%u indices=%u bones=%u materials=%u clips=%u\n",
              (unsigned)granny.vertex_count,
              (unsigned)granny.index_count,
              (unsigned)granny.bone_count,
              (unsigned)granny.material_count,
              (unsigned)granny.clip_count);
    {
        place_granny(&granny);
        debug_log("BOOT 16A: Granny fixed at Unity GrannyStartPos1=%f,%f,%f\n",
                  0.53000f, 7.31400f, -0.67000f);
    }

    debug_log("BOOT 17: initializing renderer/screen/GS\n");
    if (renderer_init() != GR_RENDER_OK)
        fatal("GRAPHICS INIT FAILED", renderer_error());
    debug_log("BOOT 18: renderer initialized\n");
    all_material_count = map_material_count(&map) + granny.material_count;
    debug_log("BOOT 19: decoding and uploading combined map/Granny textures materials=%u\n",
              all_material_count);
    if (!load_scene_materials(&map, &granny))
        fatal("TEXTURE LOAD FAILED", renderer_error());
    debug_log("BOOT 20: map and Granny materials and textures ready\n");

    debug_log("BOOT 21: initializing debug HUD/fonts\n");
    if (!dev_hud_init(&hud, renderer_get_global(),
                      font1->data, font1->size, font2->data, font2->size))
        fatal("DEV HUD INIT FAILED", "Could not initialize font resources");
    debug_log("BOOT 22: HUD initialized; Granny animation disabled for foundation build\n");

    for (;;) {
        float delta_seconds = renderer_frame_seconds();

        player_update(&player, delta_seconds);
        map_resolve_player(&map, player.position, 1.65f, 0.28f);
        if (player_debug_toggle_pressed()) {
            if (granny_loaded) {
                debug_log("GRANNY toggle OFF: destroying model buffers and reloading map-only materials\n");
                model_destroy(&granny);
                granny_loaded = 0;
                if (!load_scene_materials(&map, NULL))
                    fatal("GRANNY UNLOAD FAILED", renderer_error());
            } else {
                debug_log("GRANNY toggle ON: loading model buffers and combined materials\n");
                if (!model_load(&granny, mesh, skeleton, animation, material))
                    fatal("GRANNY RELOAD FAILED", model_error());
                place_granny(&granny);
                granny_loaded = 1;
                if (!load_scene_materials(&map, &granny))
                    fatal("GRANNY RELOAD TEXTURE FAILED", renderer_error());
            }
        }
        player_get_forward(&player, direction);
        dev_hud_update(&hud, &player, granny_loaded ? &granny : NULL, delta_seconds);

        renderer_set_camera(
            player.position[0], player.position[1], player.position[2],
            player.position[0] + direction[0],
            player.position[1] + direction[1],
            player.position[2] + direction[2]
        );

        renderer_begin();
        map_draw(&map);
        if (granny_loaded)
            renderer_draw(granny.vertices, granny.vertex_count,
                          granny.indices, granny.index_count,
                          granny.parts, granny.part_count);
        renderer_begin_overlay();
        dev_hud_draw(&hud, renderer_get_global(), &player,
                     granny_loaded ? &granny : NULL);
        renderer_end_overlay();
        renderer_end();

        debug_frame++;
        if (debug_frame == 1u)
            debug_log("RUNNING first rendered frame completed\n");
        if ((debug_frame % 60u) == 0u)
            debug_log("RUNNING frame=%u dt=%f fps=%f granny_loaded=%d pos=%f,%f,%f yaw=%f pitch=%f\n",
                      debug_frame, delta_seconds, hud.fps, granny_loaded,
                      player.position[0], player.position[1], player.position[2],
                      player.yaw, player.pitch);
    }

    return 0;
}
