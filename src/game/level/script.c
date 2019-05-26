#include "system/stacktrace.h"
#include "ebisp/gc.h"
#include "ebisp/interpreter.h"
#include "ebisp/parser.h"
#include "ebisp/scope.h"
#include "ebisp/std.h"
#include "game/level.h"
#include "script.h"
#include "system/str.h"
#include "system/line_stream.h"
#include "system/log.h"
#include "system/log_script.h"
#include "system/lt.h"
#include "system/nth_alloc.h"
#include "ui/console.h"
#include "broadcast.h"

struct Script
{
    Lt *lt;
    Gc *gc;
    struct Scope scope;
    const char *source_code;
};

static Script *create_script(Broadcast *broadcast, const char *source_code)
{
    trace_assert(source_code);

    Lt *lt = create_lt();

    Script *script = PUSH_LT(lt, nth_calloc(1, sizeof(Script)), free);
    if (script == NULL) {
        RETURN_LT(lt, NULL);
    }
    script->lt = lt;

    script->gc = PUSH_LT(lt, create_gc(), destroy_gc);
    if (script->gc == NULL) {
        RETURN_LT(lt, NULL);
    }

    script->scope = create_scope(script->gc);
    load_std_library(script->gc, &script->scope);
    load_log_library(script->gc, &script->scope);
    broadcast_load_library(broadcast, script->gc, &script->scope);

    script->source_code = PUSH_LT(
        lt,
        string_duplicate(source_code, NULL),
        free);
    if (script->source_code == NULL) {
        RETURN_LT(lt, NULL);
    }

    struct ParseResult parse_result =
        read_all_exprs_from_string(
            script->gc,
            source_code);
    if (parse_result.is_error) {
        log_fail("Parsing error: %s\n", parse_result.error_message);
        RETURN_LT(lt, NULL);
    }

    struct EvalResult eval_result = eval(
        script->gc,
        &script->scope,
        CONS(script->gc,
             SYMBOL(script->gc, "begin"),
             parse_result.expr));
    if (eval_result.is_error) {
        print_expr_as_sexpr(stderr, eval_result.expr);
        log_fail("\n");
        RETURN_LT(lt, NULL);
    }

    gc_collect(script->gc, script->scope.expr);

    return script;
}

Script *create_script_from_string(Broadcast *broadcast, const char *source_code)
{
    return create_script(broadcast, string_duplicate(source_code, NULL));
}

Script *create_script_from_line_stream(LineStream *line_stream, Broadcast *broadcast)
{
    trace_assert(line_stream);

    const char *line = line_stream_next(line_stream);
    if (line == NULL) {
        return NULL;
    }

    size_t n = 0;
    if (sscanf(line, "%lu", &n) == EOF) {
        return NULL;
    }

    const char *source_code = line_stream_collect_n_lines(line_stream, n);
    if (source_code == NULL) {
        return NULL;
    }

    return create_script(broadcast, source_code);
}

void destroy_script(Script *script)
{
    trace_assert(script);
    RETURN_LT0(script->lt);
}

const char *script_source_code(const Script *script)
{
    return script->source_code;
}

int script_eval(Script *script, const char *source_code)
{
    trace_assert(script);
    trace_assert(source_code);

    struct ParseResult parse_result = read_expr_from_string(
        script->gc,
        source_code);
    if (parse_result.is_error) {
        log_fail("Parsing error: %s\n", parse_result.error_message);
        return -1;
    }

    struct EvalResult eval_result = eval(
        script->gc,
        &script->scope,
        parse_result.expr);
    if (eval_result.is_error) {
        log_fail("Evaluation error: ");
        /* TODO(#521): Evalation error is prepended with `[FAIL]` at the end of the message */
        /* TODO(#486): print_expr_as_sexpr could not be easily integrated with log_fail */
        print_expr_as_sexpr(stderr, eval_result.expr);
        log_fail("\n");
        return -1;
    }

    gc_collect(script->gc, script->scope.expr);

    return 0;
}

bool script_has_scope_value(const Script *script, const char *name)
{
    return !nil_p(
        get_scope_value(
            &script->scope,
            SYMBOL(script->gc, name)));
}
