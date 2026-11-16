#ifndef GRANNY_PLAYER_H
#define GRANNY_PLAYER_H

#include "map.h"

#define GR_PLAYER_EYE_HEIGHT 1.65f
#define GR_PLAYER_CAPSULE_RADIUS 0.35f
#define GR_PLAYER_CAPSULE_HEIGHT 1.80f

typedef struct {
    float position[3];
    float feet_y;

    float yaw;
    float pitch;

    float move_speed;
    float look_sensitivity;

    int pad_ready;
    int free_camera;
} GrPlayer;

int player_init(GrPlayer *player, float x, float y, float z);

void player_update(GrPlayer *player, float delta_seconds, const GrWorld *world);

void player_get_forward(const GrPlayer *player, float direction[3]);

int player_debug_toggle_pressed(void);

void player_shutdown(void);

#endif
