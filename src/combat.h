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
    Party *party;
    Character monsters[MAX_MONSTERS];
    size_t monster_count;
    size_t turn_order[MAX_COMBATANTS];
    size_t turn_count;
    size_t turn_index;
    unsigned normal_attacks[MAX_COMBATANTS]; /* 本场战斗中每个行动者的普攻次数 */
    uint32_t *random_state;
} Combat;

typedef enum {
    COMBAT_ACTIVE,
    COMBAT_VICTORY,
    COMBAT_DEFEAT
} CombatResult;

typedef struct {
    DamageType type;
    int damage;    /* 目标实际失去的生命 */
    int recoil;    /* 出手者被防御反噬失去的生命 */
    int reflected; /* 魔抗 99 时反弹给出手者的法术伤害 */
} AttackResult;

void combat_start(Combat *combat, Party *party, const Encounter *encounter,
                  uint32_t *random_state);
Character *combat_actor(Combat *combat, size_t actor_id);
CombatResult combat_result(const Combat *combat);
size_t combat_monster_target(const Combat *combat);
AttackResult combat_attack(Combat *combat, size_t target_id);

#endif
