#ifndef DND_PARTY_H
#define DND_PARTY_H

#include <stdbool.h>
#include <stddef.h>

#include "skill.h"

enum { PARTY_SIZE = 3 };

/* 职业决定普通攻击的结算方式：法师的平砍有独立的伤害与保底规则。 */
typedef enum {
    JOB_WARRIOR,
    JOB_RANGER,
    JOB_MAGE,
    JOB_MONSTER
} CharacterJob;

/*
 * 角色数值。除 name/job/damage_type/skills 外，所有数值都必须落在 0-99，
 * 由 character_stats_valid() 在初始化时校验。
 */
typedef struct {
    const char *name;
    CharacterJob job;
    DamageType damage_type; /* 普通攻击的伤害属性 */
    int hp;
    int sp;
    int defense;       /* 防御值：物理伤害的减伤 */
    int magic_resist;  /* 魔抗：法术伤害的除数，99 为免疫并反弹 */
    int strength;      /* 力量：普通攻击伤害 */
    int speed;         /* 速度：决定行动顺序 */
    int magic;         /* 魔力：法术伤害 */
    int luck;          /* 幸运：同速时的先攻判定 */
    int reputation;    /* 声望：暂未参与计算 */
    const Skill *skills;
    size_t skill_count;
} Character;

typedef struct {
    Character members[PARTY_SIZE];
} Party;

bool character_stats_valid(const Character *character);
bool party_init(Party *party);
const char *damage_type_text(DamageType type);

#endif
