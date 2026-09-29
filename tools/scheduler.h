#ifndef BLOGGEN_SCHEDULER_H
#define BLOGGEN_SCHEDULER_H

#include <stddef.h>

typedef struct blog_catalog_t blog_catalog_t;

typedef struct blog_scheduler_contract {
    size_t (*get_category_count)(const blog_catalog_t *catalog);
    size_t (*get_post_count)(const blog_catalog_t *catalog, size_t category_index);

    void (*build_home)(blog_catalog_t *catalog, const char *out_dir);
    void (*build_category)(blog_catalog_t *catalog, size_t category_index, const char *out_dir);
    void (*build_post)(blog_catalog_t *catalog, size_t category_index, size_t post_index, const char *out_dir);
    void (*build_search_index)(blog_catalog_t *catalog, const char *out_dir);
    void (*build_search_page)(blog_catalog_t *catalog, const char *out_dir);
} blog_scheduler_contract_t;

int blog_scheduler_dispatch(blog_catalog_t *catalog,
                            const char *out_dir,
                            const blog_scheduler_contract_t *contract);

#endif /* BLOGGEN_SCHEDULER_H */
