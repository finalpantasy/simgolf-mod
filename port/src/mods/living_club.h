// Persistent golfer narrative. Portable logic, no game memory or presentation.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "mods/json.h"

enum { LC_PERSONAS=25, LC_PROFILES=512, LC_EVENTS=66, LC_HOLES=37,
       LC_BONDS=8, LC_JOURNAL=5, LC_TEXT=120, LC_ARCS=16 };
typedef struct {
  int partner, rounds, warmth, shared_good, shared_bad;
} LcBond;
typedef struct {
  int used, id, persona, traits, visits, rounds, completed, confidence, resolve;
  int good, bad, visit_good, visit_bad, best_round, nemesis, favorite;
  int troubles[LC_HOLES], triumphs[LC_HOLES], seen[LC_EVENTS];
  int arc, stage, arc_visit, arc_good, arc_bad, arc_partner, arc_hole, arc_setbacks, ending_kind, endings, setbacks, last_partner;
  int arc_progress_visit, arc_setback_visit;
  uint32_t serial, last_line_hash, last_hash[LC_EVENTS];
  LcBond bonds[LC_BONDS];
  char journal[LC_JOURNAL][LC_TEXT];
} LcPerson;
typedef struct {
  JsonVal *root;
  const JsonVal *personas, *stories, *tuning;
  uint32_t seed;
} LcContent;
typedef struct { LcPerson people[LC_PROFILES]; uint32_t seed; } LcWorld;
typedef struct {
  int profile, partner, traits, hole, event;
  const char *name, *partner_name;
} LcContext;

int lc_content_load(LcContent *c, const char *path, char *err, size_t n);
void lc_content_free(LcContent *c);
void lc_reset(LcWorld *w, uint32_t seed);
LcPerson *lc_person(LcWorld *w, const LcContent *c, int profile, int traits);
void lc_visit(LcWorld *w, const LcContent *c, const LcContext *ctx, char *out, size_t n);
void lc_event(LcWorld *w, const LcContent *c, const LcContext *ctx, char *out, size_t n);
void lc_leave(LcWorld *w, const LcContent *c, int profile, int score, int completed);
int lc_reaction(const LcContent *c, const LcPerson *p, int event, int original);
const char *lc_persona_name(const LcContent *c, const LcPerson *p);
void lc_summary(const LcContent *c, const LcPerson *p, char *out, size_t n);
int lc_save(const LcWorld *w, const LcContent *c, const char *save, char *err, size_t n);
int lc_load(LcWorld *w, const LcContent *c, const char *save, char *err, size_t n);
JsonVal *lc_snapshot(const LcWorld *w, const LcContent *c);

// Native adapter. Browser translator uses no-op macros for these entry points.
void living_init(const char *tables_dir);
int living_enabled(void);
void living_reset(void);
void living_visit(int slot);
void living_event(int slot, int event, int arg);
void living_frame(void);
int living_thought(int slot, int event, int arg, int stamp, char *out, size_t n);
void living_save_field(int fd, int reading);
void living_save_end(void);
int living_written(int fd, const char *path);
void living_closed(int fd);
int living_reaction(int slot, int event, int original);
void living_card(int slot, char *identity, size_t ni, char *story, size_t ns);
int living_diary(int slot, char lines[5][100]);
char *living_snapshot(void);
