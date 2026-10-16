#ifndef SIMGOLF_ROSTER_H
#define SIMGOLF_ROSTER_H

// Original slots 0..83 retain their meaning. New identities occupy 84..128.
#define ROSTER_FIRST 84
#define ROSTER_ADDITIONS 45
#define ROSTER_CAPACITY 129
unsigned roster_address(unsigned address);
int roster_enabled(void);
int roster_profile_valid(int profile);
int roster_character(int profile); // painted portrait, independent of gameplay identity
int roster_identity(int profile);
unsigned roster_pick_regular(unsigned ordinal);
unsigned roster_regular_count(void);
unsigned roster_pro_count(void);
unsigned roster_next_regular(unsigned profile);
unsigned roster_member_next(unsigned address);
unsigned roster_person_next(unsigned address);
unsigned roster_member_end(unsigned field);
void roster_seed(int reset);
void roster_append_tables(void);
void roster_bind_slot(int slot);
void roster_spawned(unsigned slot);
int roster_face_count(int male);
int roster_face_character(int male,int ordinal);
const char *roster_face_name(int character);
int roster_face_apply(int profile,int male,int head,int character);
int roster_character_rows(int existing);
int roster_choose_character(int count);
int roster_pro_rows(int existing);
int roster_import_allowed(const char *name);
void roster_save_tail(void);
int roster_preflight_file(void *file, long body_end, long file_end, int course);
int roster_pro_index(int character);
int roster_celebrity_index(int character);

#endif
