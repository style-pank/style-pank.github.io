#include "scheduler.h"

#include <cwist/core/sstring/sstring.h>
#include <cwist/sys/err/cwist_err.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOG_SCHEDULER_MIN_ORDER 3
#define BLOG_SCHEDULER_MAX_ORDER 13

typedef enum blog_job_kind {
    BLOG_JOB_HOME = 0,
    BLOG_JOB_CATEGORY,
    BLOG_JOB_POST,
    BLOG_JOB_INDEX,
    BLOG_JOB_KIND_COUNT
} blog_job_kind_t;

typedef struct blog_dispatch_context {
    blog_catalog_t *catalog;
    const blog_scheduler_contract_t *ops;
    cwist_sstring out_dir;
    size_t total_categories;
    size_t total_posts;
    size_t order;
} blog_dispatch_context_t;

typedef struct ttak_worker {
    pthread_t thread;
    size_t row;
    size_t col;
    size_t partition_index;
    blog_job_kind_t kind;
    cwist_sstring label;
    blog_dispatch_context_t *ctx;
} ttak_worker_t;

typedef struct ttak_scheduler {
    blog_dispatch_context_t ctx;
    uint32_t *latin_primary;
    uint32_t *latin_secondary;
    ttak_worker_t *workers;
    size_t worker_count;
    bool initialized;
} ttak_scheduler_t;

typedef enum blog_index_action {
    BLOG_INDEX_BUILD_SEARCH_DATA = 0,
    BLOG_INDEX_BUILD_SEARCH_PAGE = 1,
    BLOG_INDEX_ACTION_COUNT      = 2
} blog_index_action_t;

typedef enum blog_home_action {
    BLOG_HOME_BUILD_PAGE = 0,
    BLOG_HOME_ACTION_COUNT = 1
} blog_home_action_t;

static size_t blog_scheduler_clamp_order(size_t order) {
    if (order < BLOG_SCHEDULER_MIN_ORDER) return BLOG_SCHEDULER_MIN_ORDER;
    if (order > BLOG_SCHEDULER_MAX_ORDER) return BLOG_SCHEDULER_MAX_ORDER;
    return order;
}

static size_t blog_scheduler_order_from_env(size_t fallback) {
    const char *env = getenv("BLOG_SCHEDULER_ORDER");
    if (!env || !*env) return fallback;
    char *endptr = NULL;
    unsigned long parsed = strtoul(env, &endptr, 10);
    if (endptr == env) return fallback;
    return blog_scheduler_clamp_order((size_t)parsed);
}

static bool blog_scheduler_parallel_enabled(void) {
    const char *env = getenv("BLOG_SCHEDULER_DISABLE_PARALLEL");
    return !(env && *env && strcmp(env, "0") != 0);
}

static bool blog_sstring_success(cwist_error_t err) {
    return err.errtype == CWIST_ERR_INT8 && err.error.err_i8 == ERR_SSTRING_OKAY;
}

static bool blog_is_prime(size_t value) {
    if (value < 2) return false;
    if (value % 2 == 0) return value == 2;
    for (size_t i = 3; i * i <= value; i += 2) {
        if (value % i == 0) return false;
    }
    return true;
}

static size_t blog_next_prime(size_t start) {
    size_t candidate = start;
    if (candidate < BLOG_SCHEDULER_MIN_ORDER) candidate = BLOG_SCHEDULER_MIN_ORDER;
    if (candidate == 2) candidate = 3;
    if (candidate == 6) candidate = 7;
    while (!blog_is_prime(candidate)) {
        candidate++;
        if (candidate == 6) candidate++;
        if (candidate > BLOG_SCHEDULER_MAX_ORDER) {
            return BLOG_SCHEDULER_MAX_ORDER;
        }
    }
    return candidate;
}

static bool blog_generate_orthogonal_latin_squares(size_t order,
                                                   uint32_t *latin_primary,
                                                   uint32_t *latin_secondary) {
    if (!latin_primary || !latin_secondary || order < BLOG_SCHEDULER_MIN_ORDER) return false;
    for (size_t row = 0; row < order; ++row) {
        for (size_t col = 0; col < order; ++col) {
            size_t idx = row * order + col;
            latin_primary[idx] = (uint32_t)((row + col) % order);
            latin_secondary[idx] = (uint32_t)((row + (2 * col)) % order);
        }
    }
    return true;
}

static size_t blog_scheduler_select_order(size_t categories, size_t posts) {
    size_t desired = categories;
    size_t post_partitions = posts / BLOG_JOB_KIND_COUNT;
    if (post_partitions > desired) desired = post_partitions;
    if (desired < BLOG_SCHEDULER_MIN_ORDER) desired = BLOG_SCHEDULER_MIN_ORDER;
    if (desired > BLOG_SCHEDULER_MAX_ORDER) desired = BLOG_SCHEDULER_MAX_ORDER;
    size_t prime = blog_next_prime(desired);
    if (prime > BLOG_SCHEDULER_MAX_ORDER) prime = BLOG_SCHEDULER_MAX_ORDER;
    if (prime == 2) prime = 3;
    if (prime == 6) prime = 7;
    return prime;
}

static blog_job_kind_t blog_job_from_symbol(uint32_t symbol) {
    return (blog_job_kind_t)(symbol % BLOG_JOB_KIND_COUNT);
}

static const char *blog_job_name(blog_job_kind_t kind) {
    switch (kind) {
        case BLOG_JOB_HOME:     return "home";
        case BLOG_JOB_CATEGORY: return "category";
        case BLOG_JOB_POST:     return "post";
        case BLOG_JOB_INDEX:    return "index";
        default:                return "unknown";
    }
}

static bool blog_dispatch_context_init(blog_dispatch_context_t *ctx,
                                       blog_catalog_t *catalog,
                                       const char *out_dir,
                                       const blog_scheduler_contract_t *ops) {
    if (!ctx || !catalog || !out_dir || !ops) return false;
    if (!ops->get_category_count || !ops->get_post_count ||
        !ops->build_home || !ops->build_category || !ops->build_post ||
        !ops->build_search_index || !ops->build_search_page) {
        return false;
    }

    ctx->catalog = catalog;
    ctx->ops = ops;
    ctx->total_categories = ops->get_category_count(catalog);
    ctx->total_posts = 0;
    for (size_t i = 0; i < ctx->total_categories; ++i) {
        ctx->total_posts += ops->get_post_count(catalog, i);
    }

    if (!blog_sstring_success(cwist_sstring_init(&ctx->out_dir))) return false;
    if (!blog_sstring_success(cwist_sstring_assign_len(&ctx->out_dir, out_dir, strlen(out_dir)))) {
        cwist_sstring_destroy(&ctx->out_dir);
        return false;
    }

    ctx->order = blog_scheduler_order_from_env(blog_scheduler_select_order(ctx->total_categories, ctx->total_posts));
    return true;
}

static void blog_dispatch_context_destroy(blog_dispatch_context_t *ctx) {
    if (!ctx) return;
    cwist_sstring_destroy(&ctx->out_dir);
}

static void blog_run_home_partition(blog_dispatch_context_t *ctx, size_t partition) {
    if (!ctx || !ctx->ops) return;
    (void)partition;
    for (size_t action = partition; action < BLOG_HOME_ACTION_COUNT; action += ctx->order) {
        switch ((blog_home_action_t)action) {
            case BLOG_HOME_BUILD_PAGE:
                ctx->ops->build_home(ctx->catalog, ctx->out_dir.data);
                break;
            default:
                break;
        }
    }
}

static void blog_run_category_partition(blog_dispatch_context_t *ctx, size_t partition) {
    if (!ctx || !ctx->ops || ctx->order == 0) return;
    for (size_t category = partition; category < ctx->total_categories; category += ctx->order) {
        ctx->ops->build_category(ctx->catalog, category, ctx->out_dir.data);
    }
}

static void blog_run_post_partition(blog_dispatch_context_t *ctx, size_t partition) {
    if (!ctx || !ctx->ops || ctx->order == 0) return;
    size_t linear_index = 0;
    for (size_t category = 0; category < ctx->total_categories; ++category) {
        size_t post_count = ctx->ops->get_post_count(ctx->catalog, category);
        for (size_t post = 0; post < post_count; ++post) {
            if ((linear_index % ctx->order) == partition) {
                ctx->ops->build_post(ctx->catalog, category, post, ctx->out_dir.data);
            }
            linear_index++;
        }
    }
}

static void blog_run_index_partition(blog_dispatch_context_t *ctx, size_t partition) {
    if (!ctx || !ctx->ops) return;
    for (size_t action = partition; action < BLOG_INDEX_ACTION_COUNT; action += ctx->order) {
        switch ((blog_index_action_t)action) {
            case BLOG_INDEX_BUILD_SEARCH_DATA:
                ctx->ops->build_search_index(ctx->catalog, ctx->out_dir.data);
                break;
            case BLOG_INDEX_BUILD_SEARCH_PAGE:
                ctx->ops->build_search_page(ctx->catalog, ctx->out_dir.data);
                break;
            default:
                break;
        }
    }
}

static void *blog_worker_entry(void *arg) {
    ttak_worker_t *worker = (ttak_worker_t *)arg;
    if (!worker || !worker->ctx) return NULL;
    switch (worker->kind) {
        case BLOG_JOB_HOME:
            blog_run_home_partition(worker->ctx, worker->partition_index);
            break;
        case BLOG_JOB_CATEGORY:
            blog_run_category_partition(worker->ctx, worker->partition_index);
            break;
        case BLOG_JOB_POST:
            blog_run_post_partition(worker->ctx, worker->partition_index);
            break;
        case BLOG_JOB_INDEX:
            blog_run_index_partition(worker->ctx, worker->partition_index);
            break;
        default:
            break;
    }
    return NULL;
}

static bool blog_worker_init(ttak_worker_t *worker,
                             blog_dispatch_context_t *ctx,
                             blog_job_kind_t kind,
                             size_t row,
                             size_t col,
                             size_t partition) {
    if (!worker || !ctx) return false;
    worker->ctx = ctx;
    worker->kind = kind;
    worker->row = row;
    worker->col = col;
    worker->partition_index = partition;
    if (!blog_sstring_success(cwist_sstring_init(&worker->label))) return false;

    const char *name = blog_job_name(kind);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%s[%zu,%zu]/%zu", name, row, col, partition);
    if (!blog_sstring_success(cwist_sstring_assign_len(&worker->label, buffer, strlen(buffer)))) {
        cwist_sstring_destroy(&worker->label);
        return false;
    }
    return true;
}

static void blog_worker_destroy(ttak_worker_t *worker) {
    if (!worker) return;
    cwist_sstring_destroy(&worker->label);
}

static bool blog_scheduler_init(ttak_scheduler_t *sched,
                                blog_catalog_t *catalog,
                                const char *out_dir,
                                const blog_scheduler_contract_t *ops) {
    if (!sched) return false;
    memset(sched, 0, sizeof(*sched));

    if (!blog_dispatch_context_init(&sched->ctx, catalog, out_dir, ops)) return false;

    size_t order = sched->ctx.order;
    size_t total_cells = order * order;
    sched->latin_primary = calloc(total_cells, sizeof(uint32_t));
    sched->latin_secondary = calloc(total_cells, sizeof(uint32_t));
    if (!sched->latin_primary || !sched->latin_secondary) {
        free(sched->latin_primary);
        free(sched->latin_secondary);
        blog_dispatch_context_destroy(&sched->ctx);
        return false;
    }

    if (!blog_generate_orthogonal_latin_squares(order, sched->latin_primary, sched->latin_secondary)) {
        free(sched->latin_primary);
        free(sched->latin_secondary);
        blog_dispatch_context_destroy(&sched->ctx);
        return false;
    }

    sched->worker_count = total_cells;
    sched->workers = calloc(total_cells, sizeof(ttak_worker_t));
    if (!sched->workers) {
        free(sched->latin_primary);
        free(sched->latin_secondary);
        blog_dispatch_context_destroy(&sched->ctx);
        return false;
    }

    for (size_t row = 0; row < order; ++row) {
        for (size_t col = 0; col < order; ++col) {
            size_t idx = row * order + col;
            blog_job_kind_t kind = blog_job_from_symbol(sched->latin_primary[idx]);
            size_t partition = sched->latin_secondary[idx] % order;
            if (!blog_worker_init(&sched->workers[idx], &sched->ctx, kind, row, col, partition)) {
                for (size_t i = 0; i < idx; ++i) blog_worker_destroy(&sched->workers[i]);
                free(sched->workers);
                free(sched->latin_primary);
                free(sched->latin_secondary);
                blog_dispatch_context_destroy(&sched->ctx);
                return false;
            }
        }
    }

    sched->initialized = true;
    return true;
}

static bool blog_scheduler_run(ttak_scheduler_t *sched) {
    if (!sched || !sched->initialized) return false;
    size_t launched = 0;
    for (size_t i = 0; i < sched->worker_count; ++i) {
        if (pthread_create(&sched->workers[i].thread, NULL, blog_worker_entry, &sched->workers[i]) != 0) {
            fprintf(stderr, "[bloggen] failed to spawn worker %s\n",
                    sched->workers[i].label.data ? sched->workers[i].label.data : "unknown");
            for (size_t j = 0; j < i; ++j) pthread_join(sched->workers[j].thread, NULL);
            return false;
        }
        launched = i + 1;
    }

    for (size_t i = 0; i < launched; ++i) {
        pthread_join(sched->workers[i].thread, NULL);
    }

    return true;
}

static void blog_scheduler_destroy(ttak_scheduler_t *sched) {
    if (!sched) return;
    if (sched->workers) {
        for (size_t i = 0; i < sched->worker_count; ++i) {
            blog_worker_destroy(&sched->workers[i]);
        }
    }
    free(sched->workers);
    free(sched->latin_primary);
    free(sched->latin_secondary);
    blog_dispatch_context_destroy(&sched->ctx);
    sched->initialized = false;
}

int blog_scheduler_dispatch(blog_catalog_t *catalog,
                            const char *out_dir,
                            const blog_scheduler_contract_t *contract) {
    if (!catalog || !out_dir || !contract) return -1;
    ttak_scheduler_t scheduler;
    if (!blog_scheduler_init(&scheduler, catalog, out_dir, contract)) return -1;

    if (!blog_scheduler_parallel_enabled()) {
        scheduler.ctx.order = 1;
    }
    bool ok = blog_scheduler_run(&scheduler);
    blog_scheduler_destroy(&scheduler);
    return ok ? 0 : -1;
}
