#include "party.h"

#include <stdbool.h>

/* HP/SP 的数值仍在调整中，先沿用原版的 28/23/20 与占位 SP。 */
static const Character party_members[PARTY_SIZE] = {
    {
        .name = "战士", .job = JOB_WARRIOR, .damage_type = DAMAGE_BLUNT,
        .hp = 28, .sp = 10, .defense = 6, .magic_resist = 4,
        .strength = 9, .speed = 6, .magic = 2, .luck = 30,
        .reputation = 10, .skills = NULL, .skill_count = 0
    },
    {
        .name = "游侠", .job = JOB_RANGER, .damage_type = DAMAGE_SLASH,
        .hp = 23, .sp = 10, .defense = 4, .magic_resist = 3,
        .strength = 8, .speed = 12, .magic = 3, .luck = 45,
        .reputation = 10, .skills = NULL, .skill_count = 0
    },
    {
        .name = "法师", .job = JOB_MAGE, .damage_type = DAMAGE_SLASH,
        .hp = 20, .sp = 20, .defense = 2, .magic_resist = 6,
        .strength = 3, .speed = 9, .magic = 15, .luck = 60,
        .reputation = 10, .skills = NULL, .skill_count = 0
    }
};

static bool stat_in_range(int value)
{
    return value >= 0 && value <= 99;
}

bool character_stats_valid(const Character *character)
{
    if (character->name == NULL) {
        return false;
    }
    if (character->skill_count > 0 && character->skills == NULL) {
        return false;
    }
    return stat_in_range(character->hp) &&
           stat_in_range(character->sp) &&
           stat_in_range(character->defense) &&
           stat_in_range(character->magic_resist) &&
           stat_in_range(character->strength) &&
           stat_in_range(character->speed) &&
           stat_in_range(character->magic) &&
           stat_in_range(character->luck) &&
           stat_in_range(character->reputation);
}

bool party_init(Party *party)
{
    for (size_t index = 0; index < PARTY_SIZE; ++index) {
        if (!character_stats_valid(&party_members[index])) {
            return false;
        }
        party->members[index] = party_members[index];
    }
    return true;
}

const char *damage_type_text(DamageType type)
{
    switch (type) {
    case DAMAGE_SLASH:
        return "斩击";
    case DAMAGE_BLUNT:
        return "打击";
    case DAMAGE_SPELL:
        return "法术";
    case DAMAGE_DEATH:
        return "死亡";
    }
    return "未知";
}
