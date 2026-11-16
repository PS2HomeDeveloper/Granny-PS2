#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
#define PLAYER_START_X 13.75f
#define PLAYER_START_Y 21.027f
#define PLAYER_START_Z 7.16f
#define GRANNY_START_X 0.53000f
#define GRANNY_START_Y 7.31400f
#define GRANNY_START_Z -0.67000f

static GrDevHud *active_hud;

static void place_granny(GrModel *model)
{
    uint32_t i;
    if (!model || !model->vertices) return;
    for (i = 0; i < model->vertex_count; ++i) {
        model->vertices[i].x += GRANNY_START_X;
        model->vertices[i].y += GRANNY_START_Y;
        model->vertices[i].z += GRANNY_START_Z;
    }
}

static int build_runtime_path(char *out, size_t capacity,
                              const char *argv0, const char *relative_path)
{
    char base[RUNTIME_PATH_CAP];
    const char *slash;
    size_t base_length, relative_length;

    if (!out || !capacity || !relative_path)
        return 0;
    relative_length = strlen(relative_path);
    base[0] = '\0';

    slash = argv0 ? strrchr(argv0, '/') : NULL;
    if (slash) {
        size_t prefix_length = (size_t)(slash - argv0) + 1;
        if (prefix_length >= sizeof(base))
            return 0;
        memcpy(base, argv0, prefix_length);
        base[prefix_length] = '\0';
    } else if (!getcwd(base, sizeof(base)) || !strstr(base, ":/")) {
        memcpy(base, "mass:/", sizeof("mass:/"));
    }

    base_length = strlen(base);
    while (base_length > 6 && base[base_length - 1] == '/')
        base[--base_length] = '\0';
    if (base_length + relative_length + 2 > sizeof(base) ||
        base_length + relative_length + 2 > capacity)
        return 0;

    memcpy(out, base, base_length);
    if (!base_length || base[base_length - 1] != '/')
        out[base_length++] = '/';
    memcpy(out + base_length, relative_path, relative_length + 1);
    return 1;
}

static void show_screen_message(const char *title, const char *detail,
                                u64 color)
{
    GSGLOBAL *gs;
    if (!active_hud || !active_hud->initialized || !(gs = renderer_get_global()))
        return;
    renderer_begin();
    renderer_begin_overlay();
    font_engine_draw_string_sized(gs, &active_hud->font,
                                  title ? title : "GRANNY PS2",
                                  20.0f, 36.0f, 2, color, 11.0f, 24.0f);
    if (detail)
        font_engine_draw_string_sized(gs, &active_hud->font, detail,
                                      20.0f, 72.0f, 2, color, 10.0f, 22.0f);
    renderer_end_overlay();
    renderer_end();
}

static void fatal(const char *stage, const char *message)
{
    debug_log("FATAL stage=%s message=%s\n", stage,
              message ? message : "(no detail)");
    printf("Granny: %s: %s\n", stage,
           message ? message : "(no detail)");
    fflush(stdout);
    show_screen_message(stage ? stage : "FATAL",
                        message ? message : "See debug_log.txt",
                        GS_SETREG_RGBAQ(0xff, 0x48, 0x48, 0x80, 0));
    SleepThread();
    for (;;) {}
}

static void release_texture_sources(GrRenderMaterial *materials, unsigned count)
{
    unsigned i;
    if (!materials) return;
    for (i = 0; i < count; ++i) {
        if (materials[i].texture_data)
            assets_release_data(materials[i].texture_data);
        materials[i].texture_data = NULL;
        materials[i].texture_size = 0;
    }
}

static int refresh_renderer_materials(GrWorld *world, GrModel *model,
                                      int include_model)
{
    unsigned model_count = include_model && model ? model->material_count : 0;
    unsigned map_count = world_material_count(world);
    size_t total_size = (size_t)model_count + (size_t)map_count;
    GrRenderMaterial *combined;
    unsigned i;
    int result;

    if (total_size > 0x7fffffffu)
        return GR_RENDER_ERROR;
    combined = total_size ?
        (GrRenderMaterial *)calloc(total_size, sizeof(*combined)) : NULL;
    if (total_size && !combined)
        return GR_RENDER_ERROR;

    if (model_count) {
        memcpy(combined, model->materials,
               (size_t)model_count * sizeof(*combined));
        for (i = 0; i < model_count; ++i) {
            combined[i].texture_data = NULL;
            combined[i].texture_size = 0;
            if (combined[i].texture_name && combined[i].texture_name[0]) {
                const GrAsset *texture = assets_find_texture(combined[i].texture_name);
                if (!texture) {
                    debug_log("RENDER material texture missing name=%s reason=%s\n",
                              combined[i].texture_name, assets_error());
                    release_texture_sources(combined, (unsigned)total_size);
                    free(combined);
                    return GR_RENDER_ERROR;
                }
                combined[i].texture_data = texture->data;
                combined[i].texture_size = texture->size;
            }
        }
    }

    if (map_count && !world_fill_materials(world, combined + model_count,
                                           map_count, model_count)) {
        debug_log("RENDER could not resolve map materials: %s\n", world_error());
        release_texture_sources(combined, (unsigned)total_size);
        free(combined);
        return GR_RENDER_ERROR;
    }

    debug_log("RENDER upload begin model_materials=%u map_materials=%u total=%u\n",
              model_count, map_count, (unsigned)total_size);
    result = renderer_load_materials(combined, (unsigned)total_size);
    release_texture_sources(combined, (unsigned)total_size);
    free(combined);
    if (result != GR_RENDER_OK)
        debug_log("RENDER upload FAILED: %s\n", renderer_error());
    else
        debug_log("RENDER upload complete; source texture buffers released\n");
    return result;
}

static int load_granny_model(GrModel *model, int with_animation)
{
    const GrAsset *mesh = NULL;
    const GrAsset *skeleton = NULL;
    const GrAsset *animation = NULL;
    const GrAsset *material = NULL;
    int ok;

    mesh = assets_find("3d/skeletal/Granny/Granny.mesh");
    if (!mesh) goto failed;
    skeleton = assets_find("3d/skeletal/Granny/Granny.skeleton");
    if (!skeleton) goto failed;
    if (with_animation) {
        animation = assets_find("3d/skeletal/Granny/Granny.anim");
        if (!animation) goto failed;
    }
    material = assets_find("3d/skeletal/Granny/Granny.material");
    if (!material) goto failed;

    debug_log("MODEL load begin animated=%d\n", with_animation);
    ok = model_load(model, mesh, skeleton, animation, material);
    if (!ok) {
        debug_log("MODEL load FAILED: %s\n", model_error());
        goto failed;
    }
    place_granny(model);

    assets_release(mesh);
    assets_release(skeleton);
    assets_release(material);
    /* The model owns the GANM cache entry until model_destroy(). */
    debug_log("MODEL ready vertices=%u indices=%u bones=%u materials=%u clips=%u animated=%d\n",
              (unsigned)model->vertex_count, (unsigned)model->index_count,
              (unsigned)model->bone_count, (unsigned)model->material_count,
              (unsigned)model->clip_count, with_animation);
    return 1;

failed:
    if (material) assets_release(material);
    if (animation) assets_release(animation);
    if (skeleton) assets_release(skeleton);
    if (mesh) assets_release(mesh);
    return 0;
}

int main(int argc, char **argv)
{
    GrPlayer player = {0};
    GrModel granny = {0};
    GrDevHud hud = {0};
    GrWorld world = {0};
    const GrAsset *font1;
    const GrAsset *font2;
    char package_path[RUNTIME_PATH_CAP];
    char package_sibling_path[RUNTIME_PATH_CAP];
    char log_path[RUNTIME_PATH_CAP];
    float direction[3];
    float map_position[3];
    unsigned debug_frame = 0;
    int granny_loaded = 0;

    sceSifInitRpc(0);
    SifLoadFileInit();

    if (fileXioInit() < 0) {
        printf("Granny: USB STORAGE INIT FAILED: Could not initialize fileXio\n");
        SleepThread();
        for (;;) {}
    }
    if (!build_runtime_path(log_path, sizeof(log_path),
                            argc > 0 ? argv[0] : NULL, "debug_log.txt") ||
        !build_runtime_path(package_path, sizeof(package_path),
                            argc > 0 ? argv[0] : NULL, "assets/DATA.PAK") ||
        !build_runtime_path(package_sibling_path, sizeof(package_sibling_path),
                            argc > 0 ? argv[0] : NULL, "DATA.PAK")) {
        printf("Granny: RUNTIME PATH ERROR: path beside ELF is too long\n");
        SleepThread();
        for (;;) {}
    }
    debug_init(log_path, argc, argv);
    debug_log("BOOT 00: main reached fileXio; SIF RPC/loadfile initialized\n");
    debug_log("BOOT 01: executable=%s log=%s package_candidates=%s,%s\n",
              argc > 0 && argv[0] ? argv[0] : "(none)", log_path,
              package_path, package_sibling_path);

    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0)
        fatal("CONTROLLER INIT FAILED", "Could not load SIO2MAN");
    debug_log("BOOT 02: SIO2MAN loaded\n");
    if (SifLoadModule("rom0:PADMAN", 0, NULL) < 0)
        fatal("CONTROLLER INIT FAILED", "Could not load PADMAN");
    debug_log("BOOT 03: PADMAN loaded\n");

    debug_log("BOOT 04: initializing renderer/screen/GS\n");
    if (renderer_init() != GR_RENDER_OK)
        fatal("GRAPHICS INIT FAILED", renderer_error());
    debug_log("BOOT 05: renderer initialized\n");

    debug_log("BOOT 06: opening sibling assets/DATA.PAK at %s\n", package_path);
    if (!assets_init_external(package_path) &&
        !assets_init_external(package_sibling_path) &&
        !assets_init_external("mass:/assets/DATA.PAK") &&
        !assets_init_external("mass:/DATA.PAK"))
        fatal("EXTERNAL DATA.PAK LOAD FAILED", assets_error());
    debug_log("BOOT 07: DATA.PAK index ready; resources load on demand\n");

    font1 = assets_find("font/font1.raw");
    if (!font1) fatal("FONT1 RESOURCE LOAD FAILED", assets_error());
    font2 = assets_find("font/font2.raw");
    if (!font2) fatal("FONT2 RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 08: font resources loaded bytes=%u,%u\n",
              (unsigned)font1->size, (unsigned)font2->size);
    if (!dev_hud_init(&hud, renderer_get_global(),
                      font1->data, font1->size, font2->data, font2->size))
        fatal("DEV HUD INIT FAILED", "Could not initialize font resources");
    active_hud = &hud;
    debug_log("BOOT 09: HUD initialized\n");
    show_screen_message("GRANNY PS2", "Loading world maps...",
                        GS_SETREG_RGBAQ(0x80, 0xd0, 0xff, 0x80, 0));

    if (!player_init(&player, PLAYER_START_X, PLAYER_START_Y, PLAYER_START_Z))
        fatal("PLAYER INIT FAILED", "Could not initialize controller");
    debug_log("BOOT 10: player/controller initialized spawn=%f,%f,%f\n",
              PLAYER_START_X, PLAYER_START_Y, PLAYER_START_Z);

    if (!world_init(&world))
        fatal("WORLD INDEX LOAD FAILED", world_error());
    debug_log("BOOT 11: main-scene map catalog loaded; max resident maps=%u\n",
              (unsigned)GR_WORLD_RESIDENT_MAPS);
    map_position[0] = player.position[0];
    map_position[1] = player.position[1];
    map_position[2] = player.position[2];
    if (world_update(&world, map_position) < 0)
        fatal("INITIAL MAP LOAD FAILED", world_error());
    debug_log("BOOT 12: initial map pair loaded at player spawn\n");

    show_screen_message("GRANNY PS2", "Loading Granny model...",
                        GS_SETREG_RGBAQ(0x80, 0xd0, 0xff, 0x80, 0));
    if (!load_granny_model(&granny, 1))
        fatal("MODEL LOAD FAILED", model_error()[0] ? model_error() : assets_error());
    granny_loaded = 1;
    if (refresh_renderer_materials(&world, &granny, granny_loaded) != GR_RENDER_OK)
        fatal("TEXTURE/MATERIAL UPLOAD FAILED", renderer_error());
    debug_log("BOOT 13: map and Granny materials uploaded; texture source RAM released\n");
    debug_log("BOOT 14: Granny positioned at Unity GrannyStartPos=(%f,%f,%f); R2 toggles HUD, L2 toggles Granny\n",
              GRANNY_START_X, GRANNY_START_Y, GRANNY_START_Z);

    for (;;) {
        float delta_seconds = renderer_frame_seconds();
        int map_changed;

        player_update(&player, delta_seconds, &world);
        if (player_debug_toggle_pressed())
            hud.visible = !hud.visible;
        map_position[0] = player.position[0];
        map_position[1] = player.position[1];
        map_position[2] = player.position[2];
        if (player_model_toggle_pressed()) {
            if (granny_loaded) {
                debug_log("INPUT L2: unloading Granny model\n");
                model_destroy(&granny);
                granny_loaded = 0;
                if (refresh_renderer_materials(&world, &granny, 0) != GR_RENDER_OK)
                    fatal("MAP MATERIAL RESTORE FAILED", renderer_error());
                debug_log("MODEL unloaded; model EE memory and Granny VRAM textures released\n");
            } else {
                debug_log("INPUT L2: reloading Granny as static bind pose (no GANM)\n");
                if (!load_granny_model(&granny, 0))
                    fatal("STATIC MODEL RELOAD FAILED",
                          model_error()[0] ? model_error() : assets_error());
                granny_loaded = 1;
                if (refresh_renderer_materials(&world, &granny, 1) != GR_RENDER_OK)
                    fatal("MODEL/MAP MATERIAL RESTORE FAILED", renderer_error());
                debug_log("MODEL reloaded in static bind pose; animation resource not resident\n");
            }
        }

        map_changed = world_update(&world, map_position);
        if (map_changed < 0)
            fatal("MAP STREAMING FAILED", world_error());
        if (map_changed > 0) {
            debug_log("MAP pair changed at player=%f,%f,%f; refreshing only resident textures\n",
                      player.position[0], player.position[1], player.position[2]);
            if (refresh_renderer_materials(&world, &granny, granny_loaded) != GR_RENDER_OK)
                fatal("MAP TEXTURE STREAM FAILED", renderer_error());
        }

        player_get_forward(&player, direction);
        if (granny_loaded)
            model_update(&granny, delta_seconds);
        if (granny_loaded)
            place_granny(&granny);
        dev_hud_update(&hud, &player, granny_loaded ? &granny : NULL, delta_seconds);
        renderer_set_camera(
            player.position[0], player.position[1], player.position[2],
            player.position[0] + direction[0],
            player.position[1] + direction[1],
            player.position[2] + direction[2]
        );

        renderer_begin();
        world_draw(&world);
        if (granny_loaded)
            renderer_draw(granny.vertices, granny.vertex_count,
                          granny.indices, granny.index_count,
                          granny.parts, granny.part_count);
        renderer_begin_overlay();
        dev_hud_draw(&hud, renderer_get_global(), &player,
                     granny_loaded ? &granny : NULL);
        renderer_end_overlay();
        renderer_end();

        ++debug_frame;
        if (debug_frame == 1u)
            debug_log("RUNNING first world/model frame rendered\n");
        if ((debug_frame % 300u) == 0u)
            debug_log("RUNNING frame=%u dt=%f fps=%f maps<=%u granny=%d pos=%f,%f,%f yaw=%f pitch=%f\n",
                      debug_frame, delta_seconds, hud.fps,
                      (unsigned)GR_WORLD_RESIDENT_MAPS, granny_loaded,
                      player.position[0], player.position[1], player.position[2],
                      player.yaw, player.pitch);
    }

    return 0;
}
