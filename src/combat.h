#ifndef DND_COMBAT_H
#define DND_COMBAT_H

#include <stddef.h>
#include <stdint.h>

#include "party.h"

enum { MAX_MONSTERS = 3, MAX_COMBATANTS = PARTY_SIZE + MAX_MONSTERS };

typedef struct {
    const Character *monsters;
    size_t count;
} Encounter;

typedef struct {
    size_t actor_id;
    int score;
    int dexterity_modifier;
} Initiative;

typedef struct {
    Party *party;
    Character monsters[MAX_MONSTERS];
    size_t monster_count;
    Initiative turn_order[MAX_COMBATANTS];
    size_t turn_count;
    size_t turn_index;
} Combat;

typedef enum {
    COMBAT_ACTIVE,
    COMBAT_VICTORY,
    COMBAT_DEFEAT
} CombatResult;

void combat_start(Combat *combat, Party *party, const Encounter *encounter,
                  uint32_t *random_state);
Character *combat_actor(Combat *combat, size_t actor_id);
CombatResult combat_result(const Combat *combat);
size_t combat_monster_target(const Combat *combat);
void combat_attack(Combat *combat, size_t target_id);

#endif
