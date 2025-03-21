#include "interpolators.h"
#include <raymath.h>

typedef struct {
    float currentTime; // time since start of the animation preserved between frames
    float clipStartTime; // where the code is in the animation, updated by each "wait" operation, reset every update.
    float globEnd; // when the animation ends, reset every update.
    float deltaTime;

} AnimState;

float clipTime(AnimState *a, float duration);
float prevClipTime(AnimState *a, float duration);
void anim_wait(AnimState *a, float duration) ;
void wait_for_end(AnimState *a);

void anim_move_scalar(AnimState *anim, float *src, float dst, float duration, Interp_Func func);
void anim_move_vec2(AnimState *anim, Vector2 *src, Vector2 dst, float duration, Interp_Func func);
void anim_move_vec4(AnimState *anim, Vector4 *src, Vector4 dst, float duration, Interp_Func func);