#include "game/camera.h"
#include "system/lt.h"
#include "system/stacktrace.h"
#include "system/nth_alloc.h"
#include "system/log.h"
#include "math/rect.h"
#include "color.h"
#include "rect_layer.h"
#include "dynarray.h"
#include "system/line_stream.h"
#include "proto_rect.h"
#include "color_picker.h"
#include "system/str.h"

#define RECT_LAYER_ID_MAX_SIZE 36

/* TODO(#886): RectLayer does not allow to modify ids of Rects */
struct RectLayer {
    Lt *lt;
    Dynarray *ids;
    Dynarray *rects;
    Dynarray *colors;
    ProtoRect proto_rect;
    ColorPicker color_picker;
};

LayerPtr rect_layer_as_layer(RectLayer *rect_layer)
{
    LayerPtr layer = {
        .type = LAYER_RECT,
        .ptr = rect_layer
    };
    return layer;
}

RectLayer *create_rect_layer(void)
{
    Lt *lt = create_lt();

    RectLayer *layer = PUSH_LT(lt, nth_calloc(1, sizeof(RectLayer)), free);
    if (layer == NULL) {
        RETURN_LT(lt, NULL);
    }
    layer->lt = lt;

    layer->ids = PUSH_LT(
        lt,
        create_dynarray(sizeof(char) * RECT_LAYER_ID_MAX_SIZE),
        destroy_dynarray);
    if (layer->ids == NULL) {
        RETURN_LT(lt, NULL);
    }

    layer->rects = PUSH_LT(
        lt,
        create_dynarray(sizeof(Rect)),
        destroy_dynarray);
    if (layer->rects == NULL) {
        RETURN_LT(lt, NULL);
    }

    layer->colors = PUSH_LT(
        lt,
        create_dynarray(sizeof(Color)),
        destroy_dynarray);
    if (layer->colors == NULL) {
        RETURN_LT(lt, NULL);
    }

    layer->color_picker = create_color_picker_from_rgba(rgba(1.0f, 0.0f, 0.0f, 1.0f));

    return layer;
}

RectLayer *create_rect_layer_from_line_stream(LineStream *line_stream)
{
    trace_assert(line_stream);

    RectLayer *layer = create_rect_layer();
    if (layer == NULL) {
        return NULL;
    }

    const char *line = line_stream_next(line_stream);
    if (line == NULL) {
        RETURN_LT(layer->lt, NULL);
    }

    size_t count = 0;
    if (sscanf(line, "%zu", &count) < 0) {
        RETURN_LT(layer->lt, NULL);
    }

    for (size_t i = 0; i < count; ++i) {
        line = line_stream_next(line_stream);
        if (line == NULL) {
            RETURN_LT(layer->lt, NULL);
        }

        char hex[7];
        Rect rect;
        char id[RECT_LAYER_ID_MAX_SIZE];

        if (sscanf(line,
                   "%"STRINGIFY(RECT_LAYER_ID_MAX_SIZE)"s%f%f%f%f%6s\n",
                   id,
                   &rect.x, &rect.y,
                   &rect.w, &rect.h,
                   hex) < 0) {
            RETURN_LT(layer->lt, NULL);
        }

        Color color = hexstr(hex);

        dynarray_push(layer->rects, &rect);
        dynarray_push(layer->ids, id);
        dynarray_push(layer->colors, &color);
    }

    return layer;
}

void destroy_rect_layer(RectLayer *layer)
{
    trace_assert(layer);
    RETURN_LT0(layer->lt);
}

int rect_layer_render(const RectLayer *layer, Camera *camera, int active)
{
    trace_assert(layer);
    trace_assert(camera);

    const size_t n = dynarray_count(layer->rects);
    Rect *rects = dynarray_data(layer->rects);
    Color *colors = dynarray_data(layer->colors);

    for (size_t i = 0; i < n; ++i) {
        if (camera_fill_rect(
                camera,
                rects[i],
                color_scale(
                    colors[i],
                    rgba(1.0f, 1.0f, 1.0f, active ? 1.0f : 0.5f))) < 0) {
            return -1;
        }
    }

    if (proto_rect_render(
            &layer->proto_rect,
            camera,
            color_picker_rgba(&layer->color_picker)) < 0) {
        return -1;
    }

    if (active && color_picker_render(&layer->color_picker, camera) < 0) {
        return -1;
    }

    return 0;
}

int rect_layer_event(RectLayer *layer, const SDL_Event *event, const Camera *camera)
{
    trace_assert(layer);
    trace_assert(event);

    int selected = 0;
    if (color_picker_event(&layer->color_picker, event, &selected) < 0) {
        return -1;
    }

    if (!selected &&
        proto_rect_event(
            &layer->proto_rect,
            event,
            camera,
            color_picker_rgba(&layer->color_picker),
            layer) < 0) {
        return -1;
    }

    return 0;
}

int rect_layer_add_rect(RectLayer *layer, Rect rect, Color color)
{
    trace_assert(layer);

    if (dynarray_push(layer->rects, &rect) < 0) {
        return -1;
    }

    if (dynarray_push(layer->colors, &color) < 0) {
        return -1;
    }

    char id[RECT_LAYER_ID_MAX_SIZE];
    for (size_t i = 0; i < RECT_LAYER_ID_MAX_SIZE - 1; ++i) {
        id[i] = (char) ('a' + rand() % ('z' - 'a' + 1));
    }
    id[RECT_LAYER_ID_MAX_SIZE - 1] = '\0';

    if (dynarray_push(layer->ids, id)) {
        return -1;
    }

    return 0;
}

int rect_layer_delete_rect_at(RectLayer *layer, Vec position)
{
    trace_assert(layer);

    const size_t n = dynarray_count(layer->rects);
    Rect *rects = dynarray_data(layer->rects);

    for (size_t i = 0; i < n; ++i) {
        if (rect_contains_point(rects[i], position)) {
            dynarray_delete_at(layer->rects, i);
            dynarray_delete_at(layer->colors, i);
            return 0;
        }
    }

    return 0;
}

size_t rect_layer_count(const RectLayer *layer)
{
    return dynarray_count(layer->rects);
}

const Rect *rect_layer_rects(const RectLayer *layer)
{
    return dynarray_data(layer->rects);
}

const Color *rect_layer_colors(const RectLayer *layer)
{
    return dynarray_data(layer->colors);
}

const char *rect_layer_ids(const RectLayer *layer)
{
    return dynarray_data(layer->ids);
}

int rect_layer_dump_stream(const RectLayer *layer, FILE *filedump)
{
    trace_assert(layer);
    trace_assert(filedump);

    size_t n = dynarray_count(layer->ids);
    char *ids = dynarray_data(layer->ids);
    Rect *rects = dynarray_data(layer->rects);
    Color *colors = dynarray_data(layer->colors);

    fprintf(filedump, "%zd\n", n);
    for (size_t i = 0; i < n; ++i) {
        fprintf(filedump, "%s %f %f %f %f ",
                ids + RECT_LAYER_ID_MAX_SIZE * i,
                rects[i].x, rects[i].y, rects[i].w, rects[i].h);
        color_hex_to_stream(colors[i], filedump);
        fprintf(filedump, "\n");
    }

    return 0;
}
