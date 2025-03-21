#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <raylib.h>
#include <raymath.h>

#include "nob.h"
#include "env.h"
#include "interpolators.h"
#include "arena.h"
#include "imanim.h"
#include "plug.h"

#define PLUG(name, ret, ...) ret name(__VA_ARGS__);
LIST_OF_PLUGS
#undef PLUG

#if 0
    #define CELL_COLOR ColorFromHSV(0, 0.0, 0.15)
    #define HEAD_COLOR ColorFromHSV(200, 0.8, 0.8)
    #define BACKGROUND_COLOR ColorFromHSV(120, 0.0, 0.88)
#else
    #define CELL_COLOR ColorFromHSV(0, 0.0, 1 - 0.15)
    #define HEAD_COLOR ColorFromHSV(200, 0.8, 0.8)
    #define BACKGROUND_COLOR ColorFromHSV(120, 0.0, 1 - 0.88)
#endif

#define CELL_WIDTH 200.0f
#define CELL_HEIGHT 200.0f
#define FONT_SIZE (CELL_WIDTH*0.52f)
#define CELL_PAD (CELL_WIDTH*0.15f)
#define START_AT_CELL_INDEX 5
#define HEAD_MOVING_DURATION 0.25f
#define HEAD_WRITING_DURATION 0.25f
#define INTRO_DURATION 1.0f
#define TAPE_SIZE 50
#define BUMP_DECIPATE 0.8f

typedef enum {
    DIR_LEFT = -1,
    DIR_RIGHT = 1,
} Direction;

typedef enum {
    IMAGE_EGGPLANT,
    IMAGE_100,
    IMAGE_FIRE,
    IMAGE_JOY,
    IMAGE_OK,
    COUNT_IMAGES,
} Image_Index;

static_assert(COUNT_IMAGES == 5, "Amount of images is updated");
static const char *image_file_paths[COUNT_IMAGES] = {
    [IMAGE_EGGPLANT] = "./assets/images/eggplant.png",
    [IMAGE_100] = "./assets/images/100.png",
    [IMAGE_FIRE] = "./assets/images/fire.png",
    [IMAGE_JOY] = "./assets/images/joy.png",
    [IMAGE_OK] = "./assets/images/ok.png",
};

typedef enum {
    SYMBOL_TEXT,
    SYMBOL_IMAGE,
} Symbol_Kind;

typedef struct {
    Symbol_Kind kind;
    const char *text;
    Image_Index image_index;
} Symbol;

static Symbol symbol_text(const char *text)
{
    return (Symbol) {
        .kind = SYMBOL_TEXT,
        .text = text,
    };
}

static Symbol symbol_image(Image_Index image_index)
{
    return (Symbol) {
        .kind = SYMBOL_IMAGE,
        .image_index = image_index,
    };
}

typedef enum {
    RULE_STATE = 0,
    RULE_READ,
    RULE_WRITE,
    RULE_STEP,
    RULE_NEXT,
    COUNT_RULE_SYMBOLS,
} Rule_Symbol;

typedef struct {
    Symbol symbols[COUNT_RULE_SYMBOLS];
    float bump[COUNT_RULE_SYMBOLS];
} Rule;

typedef struct {
    Rule *items;
    size_t count;
    size_t capacity;

    float lines_t;
    float symbols_t;
    float head_t;
    float head_offset_t;
} Table;

typedef struct {
    Symbol symbol_a;
    Symbol symbol_b;
    float t;
    float bump;
} Cell;

typedef struct {
    Cell *items;
    size_t count;
    size_t capacity;
} Tape;

typedef struct {
    int index;
    float offset;

    Cell state;
    float state_t;
} Head;

typedef enum {
    FONT_REGULAR = 0,
    FONT_BOLD,
    COUNT_FONT_STYLE,
} Font_Style;

typedef struct {
    size_t size;

    // State (survives the plugin reload, resets on plug_reset)
    Arena arena_state;
    struct {
        float t;
        Head head;
        Tape tape;
        Table table;
        float tape_y_offset;
        bool finished;
    } scene;

    // Assets (reloads along with the plugin, does not change throughout the animation)
    Arena arena_assets;
    Font iosevka[COUNT_FONT_STYLE];
    Sound write_sound;
    Wave write_wave;
    Texture2D images[COUNT_IMAGES];
    AnimState anim;
    /*Tag TASK_INTRO_TAG;
    Tag TASK_MOVE_HEAD_TAG;
    Tag TASK_WRITE_HEAD_TAG;
    Tag TASK_WRITE_ALL_TAG;
    Tag TASK_WRITE_CELL_TAG;
    Tag TASK_BUMP_TAG;*/
} Plug;

static Plug *p = NULL;


static void task_intro(AnimState *anim, size_t head)
{
    float t = clipTime(anim, INTRO_DURATION);
    if(t<=0.f) return; 
    p->scene.head.index = head;
    p->scene.t = smoothstep(t);
    
}

static void task_move_head(AnimState *a, Direction dir, float duration)
{
    
    float t = clipTime(a, duration);
    if(t<=0.f)return;
    if (t>=1.f) {
        p->scene.head.offset = 0.0f;
        p->scene.head.index += dir;
        return;
    }
    p->scene.head.offset = Lerp(0, dir, smoothstep(t));
    
}

static void task_write_cell(AnimState *anim, Cell *cell, Symbol write, Env env)
{
    float t = clipTime(anim, HEAD_WRITING_DURATION);
    if(t<=0.f)return;

    if (t>=1.f) {
        cell->symbol_b = write;
        cell->t = 0.0;
        
    }

    float t1 = prevClipTime(anim, HEAD_WRITING_DURATION);

    if (t1 < 0.5 && t >= 0.5) {
        env.play_sound(p->write_sound, p->write_wave);
    }

    if (cell) cell->t = smoothstep(t);

    if (t>=1.f && cell) {
        cell->symbol_a = cell->symbol_b;
        cell->t = 0.0;
    }

}

static void task_write_head(AnimState *anim, Symbol write, float duration, Env env)
{
    float t = clipTime(anim, duration);
    if(t<=0.f) return;
    
    Cell *cell = NULL;
    if ((size_t)p->scene.head.index < p->scene.tape.count) {
        cell = &p->scene.tape.items[(size_t)p->scene.head.index];
    }

    if (cell) {
        cell->symbol_b = write;
        cell->t = 0.0;
    }

    float t1 = prevClipTime(anim, HEAD_WRITING_DURATION);

    if (t1 < 0.5 && t >= 0.5) {
        env.play_sound(p->write_sound, p->write_wave);
    }

    if (cell) cell->t = smoothstep(t);

    if (t>=1.f && cell) {
        cell->symbol_a = cell->symbol_b;
        cell->t = 0.0;
    }

}

static void task_write_all(AnimState *anim, Symbol write, Env env)
{
    
    float t = clipTime(anim, HEAD_WRITING_DURATION);
    if(t<=0.f) return;
    
    
    {
        for (size_t i = 0; i < p->scene.tape.count; ++i) {
            p->scene.tape.items[i].t = 0.0f;
            p->scene.tape.items[i].symbol_b = write;
        }
    }

    float t1 = prevClipTime(anim, HEAD_WRITING_DURATION);

    if (t1 < 0.5 && t >= 0.5) {
        env.play_sound(p->write_sound, p->write_wave);
    }

    for (size_t i = 0; i < p->scene.tape.count; ++i) {
        p->scene.tape.items[i].t = smoothstep(t);
    }

    if (t>=1.f) {
        for (size_t i = 0; i < p->scene.tape.count; ++i) {
            p->scene.tape.items[i].t = 0.0f;
            p->scene.tape.items[i].symbol_a = p->scene.tape.items[i].symbol_b;
        }
    }

}

static void task_bump(AnimState *anim, size_t row, size_t column)
{
    
    if(anim->currentTime < anim->clipStartTime)return;
    
    float delta = (anim->currentTime - anim->clipStartTime);
    float bump = 1-delta/BUMP_DECIPATE;
    if(bump < 0) bump=0;
    p->scene.table.items[row].bump[column] = bump;
    
    
    
}

static Rule rule(Symbol state, Symbol read, Symbol write, Symbol step, Symbol next)
{
    return (Rule) {
        .symbols = {
            [RULE_STATE] = state,
            [RULE_READ] = read,
            [RULE_WRITE] = write,
            [RULE_STEP] = step,
            [RULE_NEXT] = next,
        },
    };
}

static void load_assets(void)
{
    Arena *a = &p->arena_assets;
    arena_reset(a);

    int codepoints_count = 0;
    int *codepoints = LoadCodepoints("?abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-@./:)→←", &codepoints_count);
    p->iosevka[FONT_REGULAR] = LoadFontEx("./assets/fonts/iosevka-regular.ttf", FONT_SIZE*3, codepoints, codepoints_count);
    p->iosevka[FONT_BOLD] = LoadFontEx("./assets/fonts/iosevka-bold.ttf", FONT_SIZE*3, codepoints, codepoints_count);
    UnloadCodepoints(codepoints);
    for (size_t i = 0; i < COUNT_FONT_STYLE; ++i) {
        GenTextureMipmaps(&p->iosevka[i].texture);
        SetTextureFilter(p->iosevka[i].texture, TEXTURE_FILTER_BILINEAR);
    }

    for (size_t i = 0; i < COUNT_IMAGES; ++i) {
        p->images[i] = LoadTexture(image_file_paths[i]);
        GenTextureMipmaps(&p->images[i]);
        SetTextureFilter(p->images[i], TEXTURE_FILTER_BILINEAR);
    }

    p->write_wave = LoadWave("./assets/sounds/plant-bomb.wav");
    p->write_sound = LoadSoundFromWave(p->write_wave);

    
    
}

static void unload_assets(void)
{
    for (size_t i = 0; i < COUNT_FONT_STYLE; ++i) {
        UnloadFont(p->iosevka[i]);
    }
    UnloadSound(p->write_sound);
    UnloadWave(p->write_wave);
    for (size_t i = 0; i < COUNT_IMAGES; ++i) {
        UnloadTexture(p->images[i]);
    }
}

static void task_outro(AnimState *anim, float duration)
{
    Interp_Func func = FUNC_SMOOTHSTEP;
    
    anim_move_scalar(anim, &p->scene.t, 0.0, duration, func);
    anim_move_scalar(anim, &p->scene.tape_y_offset, 0.0, duration, func);
    anim_move_scalar(anim, &p->scene.table.lines_t, 0.0, duration, func);
    anim_move_scalar(anim, &p->scene.table.symbols_t, 0.0, duration, func);
    anim_move_scalar(anim, &p->scene.table.head_t, 0.0, duration, func);
    anim_move_scalar(anim, &p->scene.head.state_t, 0.0, duration, func);
    wait_for_end(anim);
}

static void task_fun(AnimState *anim, Env env)
{
    
    {
        task_write_head(anim, symbol_text( "1"), HEAD_WRITING_DURATION, env);
        wait_for_end(anim);
        task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
        wait_for_end(anim);
        task_write_head(anim, symbol_text( "2"), HEAD_WRITING_DURATION, env);
        wait_for_end(anim);
        task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
        wait_for_end(anim);
        task_write_head(anim, symbol_text( "69"), HEAD_WRITING_DURATION, env);
        wait_for_end(anim);
        task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
        wait_for_end(anim);
        task_write_head(anim, symbol_text( "420"), HEAD_WRITING_DURATION, env);
        wait_for_end(anim);
        task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
        wait_for_end(anim);
        task_write_head(anim, symbol_text( ":)"), HEAD_WRITING_DURATION, env);
        wait_for_end(anim);
        task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
        wait_for_end(anim);
        task_write_head(anim, symbol_image(IMAGE_JOY), HEAD_WRITING_DURATION, env);
        wait_for_end(anim);
        task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
        wait_for_end(anim);
        task_write_head(anim, symbol_image(IMAGE_FIRE), HEAD_WRITING_DURATION, env);
        wait_for_end(anim);
        task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
        wait_for_end(anim);
        task_write_head(anim, symbol_image(IMAGE_OK), HEAD_WRITING_DURATION, env);
        wait_for_end(anim);
        task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
        wait_for_end(anim);
        task_write_head(anim, symbol_image(IMAGE_100), HEAD_WRITING_DURATION, env);
        wait_for_end(anim);
        task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
        wait_for_end(anim);
        task_write_head(anim, symbol_image(IMAGE_EGGPLANT), HEAD_WRITING_DURATION, env);
        wait_for_end(anim);
        task_write_all(anim, symbol_text( "0"), env);
        wait_for_end(anim);
        task_write_all(anim, symbol_text( "69"), env);
        wait_for_end(anim);
        task_write_all(anim, symbol_image(IMAGE_EGGPLANT), env);
        wait_for_end(anim);
        task_write_all(anim, symbol_text( "0"), env);
        wait_for_end(anim);
        
    };
}

static void task_inc(AnimState *anim, Symbol zero, Symbol one, Env env)
{
    float delay = 0.8;
    
    {
        anim_wait(anim, delay);
        {
            task_write_head(anim, zero, HEAD_WRITING_DURATION, env);
            task_bump(anim, 1, RULE_WRITE);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
        {
            task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION, env);
            task_bump(anim, 1, RULE_STEP);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
        {
            task_write_cell(anim, &p->scene.head.state, symbol_text( "Inc"), env);
            task_bump(anim, 1, RULE_NEXT);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
        {
            task_write_head(anim, zero, HEAD_WRITING_DURATION, env);
            task_bump(anim, 1, RULE_WRITE);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
        {
            task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
            task_bump(anim, 1, RULE_STEP);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
        {
            task_write_cell(anim, &p->scene.head.state, symbol_text( "Inc"), env);
            task_bump(anim, 1, RULE_NEXT);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
        {
            task_write_head(anim, zero, HEAD_WRITING_DURATION, env);
            task_bump(anim, 1, RULE_WRITE);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
        {
            task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
            task_bump(anim, 1, RULE_STEP);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
        {
            anim_move_scalar(anim, &p->scene.table.head_offset_t, 0.0, HEAD_WRITING_DURATION, FUNC_SMOOTHSTEP);
            task_write_cell(anim, &p->scene.head.state, symbol_text( "Inc"), env);
            task_bump(anim, 1, RULE_NEXT);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);

        // anim_wait(anim, delay);

        {
            task_write_head(anim, one, HEAD_WRITING_DURATION, env);
            task_bump(anim, 0, RULE_WRITE);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
        {
            task_move_head(anim, DIR_RIGHT, HEAD_MOVING_DURATION);
            task_bump(anim, 0, RULE_STEP);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
        {
            task_write_cell(anim, &p->scene.head.state, symbol_text( "Halt"), env);
            task_bump(anim, 0, RULE_NEXT);
        }
        wait_for_end(anim);
        anim_wait(anim, delay);
    };
}

void plug_reset(void)
{
    p->anim.currentTime = 0.f;
    

}

void play(Env env)
{
    Arena *a = &p->arena_state;
    arena_reset(a);
    memset(&p->scene, 0, sizeof(p->scene));

    // Table
    {
        arena_da_append(a, &p->scene.table, rule(
            symbol_text( "Inc"),
            symbol_text( "0"),
            symbol_text( "1"),
            symbol_text( "→"),
            symbol_text( "Halt")));
        arena_da_append(a, &p->scene.table, rule(
            symbol_text( "Inc"),
            symbol_text( "1"),
            symbol_text( "0"),
            symbol_text( "→"),
            symbol_text( "Inc")));
    }

    Symbol zero = symbol_text( "0");
    Symbol one = symbol_text( "1");
    Symbol nothing = symbol_text( " ");
    for (size_t i = 0; i < START_AT_CELL_INDEX; ++i) {
        Cell cell = {.symbol_a = nothing,};
        nob_da_append(&p->scene.tape, cell);
    }
    for (size_t i = START_AT_CELL_INDEX; i < START_AT_CELL_INDEX + 3; ++i) {
        Cell cell = {.symbol_a = one,};
        nob_da_append(&p->scene.tape, cell);
    }
    for (size_t i = START_AT_CELL_INDEX + 3; i < TAPE_SIZE; ++i) {
        Cell cell = {.symbol_a = zero,};
        nob_da_append(&p->scene.tape, cell);
    }

    p->scene.head.state.symbol_a = symbol_text( "Inc");
    p->scene.table.head_offset_t = 1.0f;
    
    AnimState *anim = &p->anim;


    task_intro(anim, START_AT_CELL_INDEX);
    wait_for_end(anim);
    anim_wait(anim, 0.5);
    wait_for_end(anim);
    anim_move_scalar(anim, &p->scene.tape_y_offset, -280.0, 0.5, FUNC_SMOOTHSTEP);
    wait_for_end(anim);
    anim_wait(anim, 0.5);
    wait_for_end(anim);
    
    
    {
        anim_move_scalar(anim, &p->scene.table.lines_t, 1.0, 0.5, FUNC_SMOOTHSTEP);
        anim_move_scalar(anim, &p->scene.table.symbols_t, 1.0, 0.5, FUNC_SMOOTHSTEP);
        anim_move_scalar(anim, &p->scene.head.state_t, 1.0, 0.5, FUNC_SMOOTHSTEP);
        anim_move_scalar(anim, &p->scene.table.head_t, 1.0, 0.5, FUNC_SMOOTHSTEP);
        wait_for_end(anim);
        
    }

    task_inc(anim, zero, one, env),

    //task_fun(anim, env);

    anim_wait(anim, 1.5);
    task_outro(anim, INTRO_DURATION);
    wait_for_end(anim);
    anim_wait(anim, 0.5);
        
}

void plug_init(void)
{
    p = malloc(sizeof(*p));
    assert(p != NULL);
    memset(p, 0, sizeof(*p));
    p->size = sizeof(*p);

    load_assets();
    plug_reset();
}

void *plug_pre_reload(void)
{
    unload_assets();
    return p;
}

void plug_post_reload(void *state)
{
    p = state;
    if (p->size < sizeof(*p)) {
        TraceLog(LOG_INFO, "Migrating plug state schema %zu bytes -> %zu bytes", p->size, sizeof(*p));
        p = realloc(p, sizeof(*p));
        p->size = sizeof(*p);
    }

    load_assets();
}

static void text_in_rec(Rectangle rec, const char *text, Font_Style style, float size, Color color)
{
    Vector2 rec_size = {rec.width, rec.height};
    float font_size = size;
    Vector2 text_size = MeasureTextEx(p->iosevka[style], text, font_size, 0);
    Vector2 position = { .x = rec.x, .y = rec.y };

    position = Vector2Add(position, Vector2Scale(rec_size, 0.5));
    position = Vector2Subtract(position, Vector2Scale(text_size, 0.5));

    DrawTextEx(p->iosevka[style], text, position, font_size, 0, color);
}

static void image_in_rec(Rectangle rec, Texture2D image, float size, Color color)
{
    Vector2 rec_size = {rec.width, rec.height};
    Vector2 image_size = {size, size};
    Vector2 position = {rec.x, rec.y};

    position = Vector2Add(position, Vector2Scale(rec_size, 0.5));
    position = Vector2Subtract(position, Vector2Scale(image_size, 0.5));

    Rectangle source = { 0, 0, image.width, image.height };
    Rectangle dest = { position.x, position.y, image_size.x, image_size.y };
    DrawTexturePro(image, source, dest, Vector2Zero(), 0.0, color);
}

static void symbol_in_rec(Rectangle rec, Symbol symbol, float size, Color color)
{
    switch (symbol.kind) {
        case SYMBOL_TEXT: {
            text_in_rec(rec, symbol.text, FONT_REGULAR, size, color);
        } break;
        case SYMBOL_IMAGE: {
            image_in_rec(rec, p->images[symbol.image_index], size, WHITE);
        } break;
    }
}

static void interp_symbol_in_rec(Rectangle rec, Symbol from_symbol, Symbol to_symbol, float size, float t, Color color)
{
    symbol_in_rec(rec, from_symbol, size*(1 - t), ColorAlpha(color, 1 - t));
    symbol_in_rec(rec, to_symbol, size*t, ColorAlpha(color, t));
}

static void cell_in_rec(Rectangle rec, Cell cell, float size, Color color)
{
    interp_symbol_in_rec(rec, cell.symbol_a, cell.symbol_b, size + (cell.bump > 0 ? 1 - cell.bump : 0)*size*3, cell.t, color);
}

static void render_table_lines(float x, float y, float field_width, float field_height, size_t table_columns, size_t table_rows, float t, float thick, Color color)
{
    thick *= t;
    for (size_t i = 0; i < table_rows + 1; ++i) {
        Vector2 start_pos = {
            .x = x - thick/2,
            .y = y + i*field_height,
        };
        Vector2 end_pos = {
            .x = x + field_width*table_columns + thick/2,
            .y = y + i*field_height,
        };
        if (i >= table_rows) {
            Vector2 t = start_pos;
            start_pos = end_pos;
            end_pos = t;
        }
        end_pos = Vector2Lerp(start_pos, end_pos, t);
        DrawLineEx(start_pos, end_pos, thick, color);
    }

    for (size_t i = 0; i < table_columns + 1; ++i) {
        Vector2 start_pos = {
            .x = x + i*field_width,
            .y = y,
        };
        Vector2 end_pos = {
            .x = x + i*field_width,
            .y = y + field_height*table_rows,
        };
        if (i > 0) {
            Vector2 t = start_pos;
            start_pos = end_pos;
            end_pos = t;
        }
        end_pos = Vector2Lerp(start_pos, end_pos, t);
        DrawLineEx(start_pos, end_pos, thick, color);
    }
}

void plug_update(Env env)
{
    ClearBackground(BACKGROUND_COLOR);

    p->anim.clipStartTime = 0;
    p->anim.globEnd = 0;
    p->anim.currentTime += env.delta_time;
    p->anim.deltaTime = env.delta_time;
    play(env);
    
        
    p->scene.finished = p->anim.currentTime >= p->anim.clipStartTime;

/*
    for (size_t i = 0; i < p->scene.table.count; ++i) {
        for (size_t j = 0; j < COUNT_RULE_SYMBOLS; ++j) {
            float *t = &p->scene.table.items[i].bump[j];
            if (*t > 0) {
                *t = ((*t)*BUMP_DECIPATE - env.delta_time)/BUMP_DECIPATE;
            }
        }
    }
    {
        float *t = &p->scene.head.state.bump;
        if (*t > 0) {
            *t = ((*t)*BUMP_DECIPATE - env.delta_time)/BUMP_DECIPATE;
        }
    }
*/
    float head_thick = 20.0;
    float head_padding = head_thick*2.5;
    Rectangle head_rec = {
        .width = CELL_WIDTH + head_padding,
        .height = CELL_HEIGHT + head_padding,
    };
    float t = ((float)p->scene.head.index + p->scene.head.offset);
    head_rec.x = CELL_WIDTH/2 - head_rec.width/2 + Lerp(-20.0, t, p->scene.t)*(CELL_WIDTH + CELL_PAD);
    head_rec.y = CELL_HEIGHT/2 - head_rec.height/2;
    Camera2D camera = {
        .target = {
            .x = head_rec.x + head_rec.width/2,
            .y = head_rec.y + head_rec.height/2 - p->scene.tape_y_offset,
        },
        .zoom = Lerp(0.5, 0.93, p->scene.t),
        .offset = {
            .x = env.screen_width/2,
            .y = env.screen_height/2,
        },
    };

    // Scene
    BeginMode2D(camera);
    {
        // Tape
        {
            for (size_t i = 0; i < p->scene.tape.count; ++i) {
                Rectangle rec = {
                    .x = i*(CELL_WIDTH + CELL_PAD),
                    .y = 0,
                    .width = CELL_WIDTH,
                    .height = CELL_HEIGHT,
                };
                DrawRectangleRec(rec, CELL_COLOR);
                cell_in_rec(rec, p->scene.tape.items[i], FONT_SIZE, BACKGROUND_COLOR);
            }
        }

        // Head
        {
            Rectangle state_rec = {
                .width = head_rec.width,
                .height = head_rec.height*0.5,
            };
            state_rec.x = head_rec.x,
            state_rec.y = head_rec.y + head_rec.height - state_rec.height*(1 - p->scene.head.state_t),
            cell_in_rec(state_rec, p->scene.head.state, FONT_SIZE*0.75*p->scene.head.state_t, ColorAlpha(CELL_COLOR, p->scene.head.state_t));
            float h = head_rec.height;
            if (state_rec.y + state_rec.height > head_rec.y + head_rec.height) {
                h += state_rec.y + state_rec.height - (head_rec.y + head_rec.height);
            }
            render_table_lines(head_rec.x, head_rec.y, head_rec.width, h, 1, 1, p->scene.t, head_thick, HEAD_COLOR);
            Rectangle watermark = {
                .width = state_rec.width,
                .height = FONT_SIZE*0.5,
            };
            watermark.x = state_rec.x,
            watermark.y = state_rec.y + state_rec.height;
            text_in_rec(watermark, "tsoding.bsky.social", FONT_REGULAR, FONT_SIZE*0.25, ColorAlpha(CELL_COLOR, p->scene.t*0.5));
        }

        // Table
        {
            float top_margin = 300.0;
            float right_margin = 70.0;
            float symbol_size = FONT_SIZE*0.75;
            float field_width = 20.0f*9 + CELL_PAD*0.5;
            float field_height = 15.0f*9 + CELL_PAD*0.5;
            float x = head_rec.x + head_rec.width/2 - (field_width*COUNT_RULE_SYMBOLS + right_margin)/2;
            float y = head_rec.y + head_rec.height + top_margin;

            // Table Header
            if (0) {
                static const char *header_names[COUNT_RULE_SYMBOLS] = {
                    [RULE_STATE] = "State",
                    [RULE_READ]  = "Read",
                    [RULE_WRITE] = "Write",
                    [RULE_STEP]  = "Step",
                    [RULE_NEXT]  = "Next",
                };

                float factor = 0.52;
                float margin = head_thick;
                for (size_t j = 0; j < COUNT_RULE_SYMBOLS; ++j) {
                    Rectangle rec = {
                        .x = x + j*field_width + (j >= 2 ? right_margin : 0.0f),
                        .y = y + (-1)*field_height*factor - margin,
                        .width = field_width,
                        .height = field_height*factor,
                    };

                    text_in_rec(rec, header_names[j], FONT_BOLD,
                                symbol_size*factor*p->scene.table.symbols_t,
                                ColorAlpha(CELL_COLOR, p->scene.table.symbols_t*t));
                }
            }

            for (size_t i = 0; i < p->scene.table.count; ++i) {
                for (size_t j = 0; j < COUNT_RULE_SYMBOLS; ++j) {
                    Rectangle rec = {
                        .x = x + j*field_width + (j >= 2 ? right_margin : 0.0f),
                        .y = y + i*field_height,
                        .width = field_width,
                        .height = field_height,
                    };
                    symbol_in_rec(rec,
                                  p->scene.table.items[i].symbols[j],
                                  symbol_size*p->scene.table.symbols_t,
                                  ColorAlpha(CELL_COLOR, p->scene.table.symbols_t));
                    if (p->scene.table.items[i].bump[j] > 0.0) {
                        float t = (p->scene.table.items[i].bump[j]);

                        t *= t;
                        symbol_in_rec(rec,
                                      p->scene.table.items[i].symbols[j],
                                      symbol_size*p->scene.table.symbols_t + (1 - t)*symbol_size*3,
                                      ColorAlpha(CELL_COLOR, p->scene.table.symbols_t*t));
                    }
                }
            }

            render_table_lines(x, y, field_width, field_height, 2, p->scene.table.count, p->scene.table.lines_t, 7.0f, CELL_COLOR);

            render_table_lines(x + 2*field_width + right_margin, y, field_width, field_height, 3, p->scene.table.count, p->scene.table.lines_t, 7.0f, CELL_COLOR);

            render_table_lines(
                x - head_padding/2,
                y - head_padding/2 + p->scene.table.head_offset_t*field_height,
                2*field_width + head_padding,
                field_height + head_padding,
                1, 1,
                p->scene.table.head_t, head_thick, HEAD_COLOR);
        }
    }
    EndMode2D();
}

bool plug_finished(void)
{
    return p->scene.finished;
}

#define ARENA_IMPLEMENTATION
#include "arena.h"
#include "imanim.c"
