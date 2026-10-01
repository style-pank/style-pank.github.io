/*
 * Style and Grace render kernel, shared by the CWIST guest component.
 *
 * Every function returns a heap string owned by the caller (cwist_free), or
 * NULL on failure. The guest (blog_guest.c) exposes them as routes of an
 * in-memory cwist_app dispatched through the cwist-guest WIT world.
 */
#ifndef STYLE_GRACE_BLOG_H
#define STYLE_GRACE_BLOG_H

#include <stdbool.h>
#include <stddef.h>

/* Markdown -> HTML (md4c, GitHub dialect). allow_html=false drops raw HTML
 * blocks, which is what untrusted input such as comments needs. */
char *blog_render_markdown(const char *markdown, size_t len, bool allow_html);

/* One comment card; the body is markdown rendered without raw HTML. */
char *blog_render_comment(const char *author, const char *date, const char *body_md);

/* Ranked search over a JSON index (array of {title,url,summary,tags,body}),
 * top 10 rendered as .search-result cards. Empty string when nothing hits. */
char *blog_search_and_render(const void *index /* cJSON* */, const char *query);

/* Relevance of one post against a query: title 3, tags 2, summary 1, body 1. */
float blog_score(const char *query, const char *title, const char *tags,
                 const char *summary, const char *body);

/* Tag affinity between two space-separated tag lists. */
float blog_pair_score(const char *tags_a, const char *tags_b);

/* {"edges":[{"a":i,"b":j,"w":score},...]} over every post pair of the index. */
char *blog_relations_json(const void *index /* cJSON* */);

/* Home overview card (title, description, comma-separated chips). */
char *blog_render_home(const char *title, const char *description, const char *chips_csv);

/* {"section":..,"accent":..,"hover":..,"rgb":[r,g,b]} for a page path, or
 * {"section":""} when the path belongs to no section. */
char *blog_theme_json(const char *path);

#endif
