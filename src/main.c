#include <kernel.h>
#include <stdio.h>
#include <string.h>
#include <sifrpc.h>
#include <loadfile.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#include "assets.h"
#include "dbglog.h"
#include "dev_hud.h"
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

int main(int argc, char **argv)
{
    GrPlayer player = {0};
    GrModel granny = {0};
    GrDevHud hud = {0};
    const GrAsset *font1, *font2;
    const GrAsset *mesh, *skeleton, *animation, *material;
    char package_path[RUNTIME_PATH_CAP];
    char log_path[RUNTIME_PATH_CAP];
    float direction[3];
    unsigned debug_frame = 0;

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
                            argc > 0 ? argv[0] : NULL, "assets/DATA.PAK"))
        fatal("RUNTIME PATH ERROR", "Could not build paths beside the ELF");
    debug_init(log_path, argc, argv);
    debug_log("BOOT 00: main reached fileXio; SIF RPC, loadfile, SIO2MAN and PADMAN initialized\n");
    debug_log("BOOT 01: fileXio initialized; exe=%s log=%s package=%s\n",
              argc > 0 && argv[0] ? argv[0] : "(none)", log_path, package_path);

    debug_log("BOOT 02: initializing player/controller\n");
    if (!player_init(&player, 0.0f, 1.65f, 3.5f))
        fatal("PLAYER INIT FAILED", "Could not initialize controller");
    debug_log("BOOT 03: player/controller initialized\n");

    debug_log("BOOT 04: opening sibling assets/DATA.PAK at %s\n", package_path);
    if (!assets_init_external(package_path))
        fatal("EXTERNAL DATA.PAK LOAD FAILED", assets_error());
    debug_log("BOOT 05: DATA.PAK index ready; resources are loaded on demand\n");

    debug_log("BOOT 06: locating font/font1.raw\n");
    font1 = assets_find("font/font1.raw");
    if (!font1)
        fatal("FONT1 RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 07: locating font/font2.raw\n");
    font2 = assets_find("font/font2.raw");
    if (!font2)
        fatal("FONT2 RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 08: fonts loaded font1=%u font2=%u bytes\n",
              (unsigned)font1->size, (unsigned)font2->size);

    debug_log("BOOT 09: locating Granny.mesh\n");
    mesh = assets_find("3d/skeletal/Granny/Granny.mesh");
    if (!mesh)
        fatal("MESH RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 10: locating Granny.skeleton\n");
    skeleton = assets_find("3d/skeletal/Granny/Granny.skeleton");
    if (!skeleton)
        fatal("SKELETON RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 11: locating Granny.anim\n");
    animation = assets_find("3d/skeletal/Granny/Granny.anim");
    if (!animation)
        fatal("ANIMATION RESOURCE LOAD FAILED", assets_error());
    debug_log("BOOT 12: locating Granny.material\n");
    material = assets_find("3d/skeletal/Granny/Granny.material");
    if (!material)
        fatal("MATERIAL RESOURCE LOAD FAILED", assets_error());

    debug_log("BOOT 13: parsing model and resolving all material textures\n");
    if (!model_load(&granny, mesh, skeleton, animation, material))
        fatal("MODEL LOAD FAILED", model_error());
    debug_log("BOOT 14: model loaded vertices=%u indices=%u bones=%u materials=%u clips=%u\n",
              (unsigned)granny.vertex_count,
              (unsigned)granny.index_count,
              (unsigned)granny.bone_count,
              (unsigned)granny.material_count,
              (unsigned)granny.clip_count);

    debug_log("BOOT 15: initializing renderer/screen/GS\n");
    if (renderer_init() != GR_RENDER_OK)
        fatal("GRAPHICS INIT FAILED", renderer_error());
    debug_log("BOOT 16: renderer initialized\n");
    debug_log("BOOT 17: decoding and uploading model textures\n");
    if (renderer_load_materials(granny.materials, granny.material_count) != GR_RENDER_OK)
        fatal("TEXTURE LOAD FAILED", renderer_error());
    debug_log("BOOT 18: model materials and textures ready\n");

    debug_log("BOOT 19: initializing debug HUD/fonts\n");
    if (!dev_hud_init(&hud, renderer_get_global(),
                      font1->data, font1->size, font2->data, font2->size))
        fatal("DEV HUD INIT FAILED", "Could not initialize font resources");
    debug_log("BOOT 20: HUD initialized; entering game loop; press R2 to toggle\n");

    for (;;) {
        float delta_seconds = renderer_frame_seconds();

        player_update(&player, delta_seconds);
        player_get_forward(&player, direction);
        model_update(&granny, delta_seconds);
        dev_hud_update(&hud, &player, &granny, delta_seconds);

        renderer_set_camera(
            player.position[0], player.position[1], player.position[2],
            player.position[0] + direction[0],
            player.position[1] + direction[1],
            player.position[2] + direction[2]
        );

        renderer_begin();
        renderer_draw(granny.vertices, granny.vertex_count,
                      granny.indices, granny.index_count,
                      granny.parts, granny.part_count);
        renderer_begin_overlay();
        dev_hud_draw(&hud, renderer_get_global(), &player, &granny);
        renderer_end_overlay();
        renderer_end();

        debug_frame++;
        if (debug_frame == 1u)
            debug_log("RUNNING first rendered frame completed\n");
        if ((debug_frame % 300u) == 0u)
            debug_log("RUNNING frame=%u dt=%f fps=%f pos=%f,%f,%f yaw=%f pitch=%f\n",
                      debug_frame, delta_seconds, hud.fps,
                      player.position[0], player.position[1], player.position[2],
                      player.yaw, player.pitch);
    }

    return 0;
}
