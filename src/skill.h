#ifndef DND_SKILL_H
#define DND_SKILL_H

#include <stddef.h>

/* 攻击/技能的伤害属性。死亡类型无视防御与魔抗。 */
typedef enum {
    DAMAGE_SLASH,
    DAMAGE_BLUNT,
    DAMAGE_SPELL,
    DAMAGE_DEATH
} DamageType;

typedef enum {
    SKILL_DAMAGE,
    SKILL_HEAL,
    SKILL_BUFF
} SkillKind;

typedef enum {
    SKILL_TARGET_ONE_ENEMY,
    SKILL_TARGET_ALL_ENEMIES,
    SKILL_TARGET_ALLY
} SkillTarget;

typedef struct {
    const char *name;
    int sp_cost;
    SkillKind kind;
    DamageType damage_type;
    int power;
    SkillTarget target;
} Skill;

#endif
