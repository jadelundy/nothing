#include <stdbool.h>

#include "game/camera.h"
#include "game/sound_samples.h"
#include "game/level/boxes.h"
#include "game/level/level_editor/color_picker.h"
#include "game/level/level_editor/rect_layer.h"
#include "game/level/level_editor/point_layer.h"
#include "game/level/level_editor/player_layer.h"
#include "game/level/level_editor/label_layer.h"
#include "game/level/level_editor/background_layer.h"
#include "ui/edit_field.h"
#include "system/stacktrace.h"
#include "system/nth_alloc.h"
#include "system/lt.h"
#include "system/lt_adapters.h"
#include "system/log.h"
#include "system/str.h"
#include "config.h"
#include "math/extrema.h"
#include "system/file.h"

#include "level_editor.h"

#define LEVEL_FOLDER_MAX_LENGTH 512
#define LEVEL_EDITOR_EDIT_FIELD_SIZE vec(5.0f, 5.0f)
#define LEVEL_EDITOR_EDIT_FIELD_COLOR COLOR_BLACK

#define LEVEL_EDITOR_NOTICE_SCALE vec(10.0f, 10.0f)
#define LEVEL_EDITOR_NOTICE_DURATION 1.0f
#define LEVEL_EDITOR_NOTICE_PADDING_TOP 100.0f
#define LEVEL_EDITOR_TMPMEM_CAPACITY (640 * KILO)

static int level_editor_dump(LevelEditor *level_editor);

// TODO(#994): too much duplicate code between create_level_editor and create_level_editor_from_file

LevelEditor *create_level_editor(Cursor *cursor)
{
    Lt *lt = create_lt();
    LevelEditor *level_editor = PUSH_LT(
        lt,
        nth_calloc(1, sizeof(LevelEditor)),
        free);
    if (level_editor == NULL) {
        RETURN_LT(lt, NULL);
    }
    level_editor->lt = lt;

    level_editor->edit_field_filename.font_size = LEVEL_EDITOR_EDIT_FIELD_SIZE;
    level_editor->edit_field_filename.font_color = LEVEL_EDITOR_EDIT_FIELD_COLOR;

    level_editor->background_layer = create_background_layer(hexstr("fffda5"));
    level_editor->player_layer = create_player_layer(vec(0.0f, 0.0f), hexstr("ff8080"));
    level_editor->platforms_layer = create_rect_layer("platform", cursor);
    level_editor->goals_layer = create_point_layer("goal"),
    level_editor->lava_layer = create_rect_layer("lava", cursor);
    level_editor->back_platforms_layer = create_rect_layer("back_platform", cursor);
    level_editor->boxes_layer = create_rect_layer("box", cursor);
    level_editor->label_layer = create_label_layer("label");
    level_editor->regions_layer = create_rect_layer("region", cursor),

    level_editor->layers[LAYER_PICKER_BOXES] = rect_layer_as_layer(&level_editor->boxes_layer);
    level_editor->layers[LAYER_PICKER_PLATFORMS] = rect_layer_as_layer(&level_editor->platforms_layer);
    level_editor->layers[LAYER_PICKER_BACK_PLATFORMS] = rect_layer_as_layer(&level_editor->back_platforms_layer);
    level_editor->layers[LAYER_PICKER_GOALS] = point_layer_as_layer(&level_editor->goals_layer);
    level_editor->layers[LAYER_PICKER_PLAYER] = player_layer_as_layer(&level_editor->player_layer);
    level_editor->layers[LAYER_PICKER_LAVA] = rect_layer_as_layer(&level_editor->lava_layer);
    level_editor->layers[LAYER_PICKER_REGIONS] = rect_layer_as_layer(&level_editor->regions_layer);
    level_editor->layers[LAYER_PICKER_BACKGROUND] = background_layer_as_layer(&level_editor->background_layer);
    level_editor->layers[LAYER_PICKER_LABELS] = label_layer_as_layer(&level_editor->label_layer);

    level_editor->notice = (FadingWigglyText) {
        .wiggly_text = {
            .text = "Level saved",
            .color = COLOR_BLACK,
            .scale = LEVEL_EDITOR_NOTICE_SCALE
        },
        .duration = LEVEL_EDITOR_NOTICE_DURATION,
    };

    level_editor->camera_scale = 1.0f;

    level_editor->undo_history = create_undo_history();

    return level_editor;
}

LevelEditor *create_level_editor_from_file(const char *file_name, Cursor *cursor)
{
    trace_assert(file_name);

    Lt *lt = create_lt();
    LevelEditor *level_editor = PUSH_LT(
        lt,
        nth_calloc(1, sizeof(LevelEditor)),
        free);
    if (level_editor == NULL) {
        RETURN_LT(lt, NULL);
    }
    level_editor->lt = lt;

    level_editor->edit_field_filename.font_size = LEVEL_EDITOR_EDIT_FIELD_SIZE;
    level_editor->edit_field_filename.font_color = LEVEL_EDITOR_EDIT_FIELD_COLOR;

    level_editor->file_name =
        PUSH_LT(
            lt,
            string_duplicate(file_name, NULL),
            free);

    Memory tmpmem = {
        .capacity = LEVEL_EDITOR_TMPMEM_CAPACITY,
        .buffer = malloc(LEVEL_EDITOR_TMPMEM_CAPACITY),
    };
    trace_assert(tmpmem.buffer);

    String input = read_whole_file(&tmpmem, file_name);
    trace_assert(input.data);

    String version = trim(chop_by_delim(&input, '\n'));

    if (string_equal(version, STRING_LIT("1"))) {
        chop_by_delim(&input, '\n');
    } else if (string_equal(version, STRING_LIT("2"))) {
        // Nothing
    } else {
        log_fail("Version `%s` is not supported. Expected version `%s`.\n",
                 string_to_cstr(&tmpmem, version),
                 VERSION);
        RETURN_LT(lt, NULL);
    }

    level_editor->background_layer = chop_background_layer(&input);
    level_editor->player_layer = chop_player_layer(&tmpmem, &input);
    level_editor->platforms_layer = chop_rect_layer(&tmpmem, &input, "platform", cursor);
    level_editor->goals_layer = chop_point_layer(&tmpmem, &input, "goal");
    level_editor->lava_layer = chop_rect_layer(&tmpmem, &input, "lava", cursor);
    level_editor->back_platforms_layer = chop_rect_layer(&tmpmem, &input, "back_platform", cursor);
    level_editor->boxes_layer = chop_rect_layer(&tmpmem, &input, "box", cursor);
    level_editor->label_layer = chop_label_layer(&tmpmem, &input, "label");
    level_editor->regions_layer = chop_rect_layer(&tmpmem, &input, "region", cursor),

    level_editor->layers[LAYER_PICKER_BOXES] = rect_layer_as_layer(&level_editor->boxes_layer);
    level_editor->layers[LAYER_PICKER_PLATFORMS] = rect_layer_as_layer(&level_editor->platforms_layer);
    level_editor->layers[LAYER_PICKER_BACK_PLATFORMS] = rect_layer_as_layer(&level_editor->back_platforms_layer);
    level_editor->layers[LAYER_PICKER_GOALS] = point_layer_as_layer(&level_editor->goals_layer);
    level_editor->layers[LAYER_PICKER_PLAYER] = player_layer_as_layer(&level_editor->player_layer);
    level_editor->layers[LAYER_PICKER_LAVA] = rect_layer_as_layer(&level_editor->lava_layer);
    level_editor->layers[LAYER_PICKER_REGIONS] = rect_layer_as_layer(&level_editor->regions_layer);
    level_editor->layers[LAYER_PICKER_BACKGROUND] = background_layer_as_layer(&level_editor->background_layer);
    level_editor->layers[LAYER_PICKER_LABELS] = label_layer_as_layer(&level_editor->label_layer);

    level_editor->drag = false;

    level_editor->notice = (FadingWigglyText) {
        .wiggly_text = {
            .text = "Level saved",
            .color = COLOR_BLACK,
            .scale = LEVEL_EDITOR_NOTICE_SCALE
        },
        .duration = LEVEL_EDITOR_NOTICE_DURATION,
    };

    level_editor->camera_scale = 1.0f;

    level_editor->undo_history = create_undo_history();

    free(tmpmem.buffer);

    return level_editor;
}

void destroy_level_editor(LevelEditor *level_editor)
{
    trace_assert(level_editor);
    destroy_undo_history(level_editor->undo_history);
    destroy_rect_layer(level_editor->boxes_layer);
    destroy_rect_layer(level_editor->platforms_layer);
    destroy_rect_layer(level_editor->back_platforms_layer);
    destroy_point_layer(level_editor->goals_layer);
    destroy_rect_layer(level_editor->lava_layer);
    destroy_rect_layer(level_editor->regions_layer);
    destroy_label_layer(level_editor->label_layer);
}

int level_editor_render(const LevelEditor *level_editor,
                        const Camera *camera)
{
    trace_assert(level_editor);
    trace_assert(camera);

    if (camera_clear_background(camera, color_picker_rgba(&level_editor->background_layer.color_picker)) < 0) {
        return -1;
    }

    const Rect world_viewport = camera_view_port(camera);

    if (PLAYER_DEATH_LEVEL < world_viewport.y + world_viewport.h) {
        if (camera_fill_rect(
                camera,
                rect(
                    world_viewport.x, PLAYER_DEATH_LEVEL,
                    world_viewport.w, world_viewport.h + fmaxf(0.0f, world_viewport.y - PLAYER_DEATH_LEVEL)),
                LEVEL_EDITOR_DETH_LEVEL_COLOR) < 0) {
            return -1;
        }
    }

    for (size_t i = 0; i < LAYER_PICKER_N; ++i) {
        if (layer_render(
                level_editor->layers[i],
                camera,
                i == level_editor->layer_picker) < 0) {
            return -1;
        }
    }

    if (layer_picker_render(&level_editor->layer_picker, camera) < 0) {
        return -1;
    }

    if (level_editor->state == LEVEL_EDITOR_SAVEAS) {
        /* CSS */
        const Vec2f size = LEVEL_EDITOR_EDIT_FIELD_SIZE;
        const char *save_as_text = "Save as: ";
        const Vec2f position = vec(200.0f, 200.0f);
        const float save_as_width =
            (float) strlen(save_as_text) * FONT_CHAR_WIDTH * size.x;

        /* HTML */
        camera_render_text_screen(
            camera,
            save_as_text,
            LEVEL_EDITOR_EDIT_FIELD_SIZE,
            LEVEL_EDITOR_EDIT_FIELD_COLOR,
            position);

        if (edit_field_render_screen(
                &level_editor->edit_field_filename,
                camera,
                vec(position.x + save_as_width, position.y)) < 0) {
            return -1;
        }
    }

    const Rect screen_viewport = camera_view_port_screen(camera);
    const Vec2f text_size = fading_wiggly_text_size(&level_editor->notice);

    fading_wiggly_text_render(
        &level_editor->notice, camera,
        vec(screen_viewport.w * 0.5f - text_size.x * 0.5f,
            LEVEL_EDITOR_NOTICE_PADDING_TOP));

    return 0;
}

static
int level_editor_saveas_event(LevelEditor *level_editor,
                              const SDL_Event *event,
                              const Camera *camera)
{
    trace_assert(level_editor);
    trace_assert(event);
    trace_assert(camera);

    switch (event->type) {
    case SDL_KEYDOWN: {
        if (event->key.keysym.sym == SDLK_RETURN) {
            trace_assert(level_editor->file_name == NULL);
            char path[LEVEL_FOLDER_MAX_LENGTH];
            snprintf(
                path,
                LEVEL_FOLDER_MAX_LENGTH,
                "./assets/levels/%s.txt",
                edit_field_as_text(&level_editor->edit_field_filename));
            level_editor->file_name = PUSH_LT(
                level_editor->lt,
                string_duplicate(path, NULL),
                free);
            level_editor_dump(level_editor);
            SDL_StopTextInput();
            level_editor->state = LEVEL_EDITOR_IDLE;
            return 0;
        }
    } break;
    }

    return edit_field_event(&level_editor->edit_field_filename, event);
}

static
int level_editor_idle_event(LevelEditor *level_editor,
                            const SDL_Event *event,
                            Camera *camera)
{
    trace_assert(level_editor);
    trace_assert(event);
    trace_assert(camera);

    switch (event->type) {
    case SDL_KEYDOWN: {
        switch(event->key.keysym.sym) {
        case SDLK_s: {
            if (!SDL_IsTextInputActive()) {
                if (level_editor->file_name) {
                    level_editor_dump(level_editor);
                    log_info("Saving level to `%s`\n", level_editor->file_name);
                } else {
                    SDL_StartTextInput();
                    level_editor->state = LEVEL_EDITOR_SAVEAS;
                }
            }
        } break;

        case SDLK_z: {
            if (event->key.keysym.mod & KMOD_CTRL) {
                if (undo_history_empty(&level_editor->undo_history)) {
                    level_editor->bell = 1;
                }
                undo_history_pop(&level_editor->undo_history);
            }
        } break;
        }
    } break;

    case SDL_MOUSEWHEEL: {
        int x, y;
        SDL_GetMouseState(&x, &y);

        Vec2f position = camera_map_screen(camera, x, y);
        if (event->wheel.y > 0) {
            level_editor->camera_scale += 0.1f;
        } else if (event->wheel.y < 0) {
            level_editor->camera_scale = fmaxf(0.1f, level_editor->camera_scale - 0.1f);
        }
        camera_scale(camera, level_editor->camera_scale);
        Vec2f zoomed_position = camera_map_screen(camera, x, y);

        level_editor->camera_position =
            vec_sum(
                level_editor->camera_position,
                vec_sub(position, zoomed_position));
        camera_center_at(camera, level_editor->camera_position);
    } break;

    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEBUTTONDOWN: {
        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_MIDDLE) {
            level_editor->drag = true;
        }

        if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_MIDDLE) {
            level_editor->drag = false;
        }
    } break;

    case SDL_MOUSEMOTION: {
        if (level_editor->drag) {
            const Vec2f next_position = camera_map_screen(camera, event->motion.x, event->motion.y);
            const Vec2f prev_position = camera_map_screen(
                camera,
                event->motion.x + event->motion.xrel,
                event->motion.y + event->motion.yrel);

            vec_add(&level_editor->camera_position,
                    vec_sub(next_position, prev_position));
            camera_center_at(camera, level_editor->camera_position);
        }

    } break;
    }

    bool selected = false;
    if (layer_picker_event(
            &level_editor->layer_picker,
            event,
            camera,
            &selected) < 0) {
        return -1;
    }

    if (!selected) {
        if (layer_event(
                level_editor->layers[level_editor->layer_picker],
                event,
                camera,
                &level_editor->undo_history) < 0) {
            return -1;
        }
    } else {
        level_editor->click = 1;
    }


    return 0;
}

int level_editor_event(LevelEditor *level_editor,
                       const SDL_Event *event,
                       Camera *camera)
{
    trace_assert(level_editor);
    trace_assert(event);
    trace_assert(camera);

    switch (level_editor->state) {
    case LEVEL_EDITOR_IDLE:
        return level_editor_idle_event(level_editor, event, camera);

    case LEVEL_EDITOR_SAVEAS:
        return level_editor_saveas_event(level_editor, event, camera);
    }

    return 0;
}

int level_editor_focus_camera(LevelEditor *level_editor,
                              Camera *camera)
{
    camera_center_at(camera, level_editor->camera_position);
    camera_scale(camera, level_editor->camera_scale);
    return 0;
}

static LayerPicker level_format_layer_order[LAYER_PICKER_N] = {
    LAYER_PICKER_BACKGROUND,
    LAYER_PICKER_PLAYER,
    LAYER_PICKER_PLATFORMS,
    LAYER_PICKER_GOALS,
    LAYER_PICKER_LAVA,
    LAYER_PICKER_BACK_PLATFORMS,
    LAYER_PICKER_BOXES,
    LAYER_PICKER_LABELS,
    LAYER_PICKER_REGIONS
};

/* TODO(#904): LevelEditor does not check that the saved level file is modified by external program */
static int level_editor_dump(LevelEditor *level_editor)
{
    trace_assert(level_editor);

    FILE *filedump = PUSH_LT(
        level_editor->lt,
        fopen(level_editor->file_name, "w"),
        fclose_lt);

    if (fprintf(filedump, "%s\n", VERSION) < 0) {
        return -1;
    }

    for (size_t i = 0; i < LAYER_PICKER_N; ++i) {
        if (layer_dump_stream(
                level_editor->layers[level_format_layer_order[i]],
                filedump) < 0) {
            return -1;
        }
    }

    fclose(RELEASE_LT(level_editor->lt, filedump));

    fading_wiggly_text_reset(&level_editor->notice);
    level_editor->save = 1;

    return 0;
}

int level_editor_update(LevelEditor *level_editor, float delta_time)
{
    return fading_wiggly_text_update(&level_editor->notice, delta_time);
}

void level_editor_sound(LevelEditor *level_editor, Sound_samples *sound_samples)
{
    trace_assert(sound_samples);

    if (level_editor) {
        if (level_editor->bell) {
            level_editor->bell = 0;
            sound_samples_play_sound(sound_samples, 2);
        }

        if (level_editor->click) {
            level_editor->click = 0;
            sound_samples_play_sound(sound_samples, 3);
        }

        if (level_editor->save) {
            level_editor->save = 0;
            sound_samples_play_sound(sound_samples, 4);
        }
    }
}
