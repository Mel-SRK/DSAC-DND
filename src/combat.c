#include "combat.h"

#include <stdbool.h>

enum {
    MAGE_DEATH_DAMAGE = 99,   /* 法师普攻触发死亡类型时的伤害 */
    MAGE_PITY_ATTACKS = 100   /* 一场战斗内第几次普攻必定触发死亡 */
};

/* 固定算法的伪随机数，保证同一个 --seed 在不同平台上得到相同结果。 */
static uint32_t next_random(uint32_t *state)
{
    *state = (uint32_t)((uint64_t)*state * UINT64_C(1664525) + UINT64_C(1013904223));
    return *state;
}

static int percent_roll(uint32_t *state)
{
    return (int)((next_random(state) >> 16) % 100u) + 1;
}

static bool is_party(size_t actor_id)
{
    return actor_id < PARTY_SIZE;
}

Character *combat_actor(Combat *combat, size_t actor_id)
{
    if (is_party(actor_id)) {
        return &combat->party->members[actor_id];
    }
    return &combat->monsters[actor_id - PARTY_SIZE];
}

static void apply_damage(Character *target, int amount)
{
    if (amount <= 0) {
        return;
    }
    target->hp -= amount;
    if (target->hp < 0) {
        target->hp = 0;
    }
}

/*
 * 物理伤害：攻击高于防御时按差值扣血；防御高出 51~70 时反噬 50%，
 * 高出 71 以上时反噬 70%。反噬直接扣血，不再经过防御。
 */
static int physical_damage(int damage, int defense, int *recoil)
{
    if (damage > defense) {
        return damage - defense;
    }
    int gap = defense - damage;
    if (gap >= 71) {
        *recoil = gap * 7 / 10;
    } else if (gap >= 51) {
        *recoil = gap / 2;
    }
    return 0;
}

/* 法术伤害：魔抗 0 视为全额，1~98 作除数向下取整，99 免疫。 */
static int spell_damage(int damage, int magic_resist)
{
    if (magic_resist >= 99) {
        return 0;
    }
    if (magic_resist <= 0) {
        return damage;
    }
    return damage / magic_resist;
}

/* 魔抗 99 的反弹伤害，只反弹一次；施法者也是 99 时固定 1 点。 */
static int reflected_damage(int damage, int magic_resist)
{
    int reflected = damage * 3 / 10;
    if (magic_resist >= 99) {
        return 1;
    }
    if (magic_resist <= 0) {
        return reflected;
    }
    return reflected / magic_resist;
}

/*
 * 法师普攻的真随机：1% 死亡（99 点），其余斩击/打击各 49.5%。
 * 不在概率上做保底，只额外记录本场普攻次数，第 100 次强制死亡。
 */
static DamageType roll_mage_attack(Combat *combat, size_t actor_id, int *damage)
{
    ++combat->normal_attacks[actor_id];
    uint32_t roll = next_random(combat->random_state) % 200u;
    if (roll < 2u || combat->normal_attacks[actor_id] >= MAGE_PITY_ATTACKS) {
        combat->normal_attacks[actor_id] = 0;
        *damage = MAGE_DEATH_DAMAGE;
        return DAMAGE_DEATH;
    }
    *damage = 1;
    return roll < 101u ? DAMAGE_SLASH : DAMAGE_BLUNT;
}

/* 幸运值 n 即 n% 概率先手，99 视为必定先手。 */
static bool luck_wins(Combat *combat, size_t actor_id)
{
    int luck = combat_actor(combat, actor_id)->luck;
    if (luck >= 99) {
        return true;
    }
    return percent_roll(combat->random_state) <= luck;
}

/*
 * 同速同幸运的角色已按编号排列，队员在怪物前。
 * 跨队时按编号为队员掷骰，将未掷中的队员依次移到怪物后。
 */
static void order_equal_luck(Combat *combat, size_t *ids, size_t count)
{
    if (!is_party(ids[0]) || is_party(ids[count - 1])) {
        return;
    }

    size_t losers[PARTY_SIZE];
    size_t loser_count = 0;
    size_t read_index = 0;
    size_t write_index = 0;
    while (read_index < count && is_party(ids[read_index])) {
        size_t actor_id = ids[read_index++];
        if (luck_wins(combat, actor_id)) {
            ids[write_index++] = actor_id;
        } else {
            losers[loser_count++] = actor_id;
        }
    }
    while (read_index < count) {
        ids[write_index++] = ids[read_index++];
    }
    for (size_t index = 0; index < loser_count; ++index) {
        ids[write_index++] = losers[index];
    }
}

typedef struct {
    size_t ids[MAX_COMBATANTS];
    size_t count;
} OrderHeap;

/* 大顶堆的比较规则：速度高的先，同速幸运高的先，再按编号稳定。 */
static bool acts_before(Combat *combat, size_t first, size_t second)
{
    const Character *left = combat_actor(combat, first);
    const Character *right = combat_actor(combat, second);
    if (left->speed != right->speed) {
        return left->speed > right->speed;
    }
    if (left->luck != right->luck) {
        return left->luck > right->luck;
    }
    return first < second;
}

static void heap_push(Combat *combat, OrderHeap *heap, size_t actor_id)
{
    size_t index = heap->count++;
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (!acts_before(combat, actor_id, heap->ids[parent])) {
            break;
        }
        heap->ids[index] = heap->ids[parent];
        index = parent;
    }
    heap->ids[index] = actor_id;
}

static size_t heap_pop(Combat *combat, OrderHeap *heap)
{
    size_t first = heap->ids[0];
    size_t last = heap->ids[--heap->count];
    size_t index = 0;
    while (2 * index + 1 < heap->count) {
        size_t child = 2 * index + 1;
        size_t right = child + 1;
        if (right < heap->count &&
            acts_before(combat, heap->ids[right], heap->ids[child])) {
            child = right;
        }
        if (!acts_before(combat, heap->ids[child], last)) {
            break;
        }
        heap->ids[index] = heap->ids[child];
        index = child;
    }
    heap->ids[index] = last;
    return first;
}

static void build_turn_order(Combat *combat)
{
    OrderHeap heap = {0};
    size_t actor_total = PARTY_SIZE + combat->monster_count;
    for (size_t actor_id = 0; actor_id < actor_total; ++actor_id) {
        if (combat_actor(combat, actor_id)->hp > 0) {
            heap_push(combat, &heap, actor_id);
        }
    }

    size_t *ids = combat->turn_order;
    size_t count = 0;
    while (heap.count > 0) {
        ids[count++] = heap_pop(combat, &heap);
    }

    size_t start = 0;
    while (start < count) {
        size_t end = start + 1;
        const Character *first = combat_actor(combat, ids[start]);
        while (end < count) {
            const Character *next = combat_actor(combat, ids[end]);
            if (next->speed != first->speed || next->luck != first->luck) {
                break;
            }
            ++end;
        }
        order_equal_luck(combat, ids + start, end - start);
        start = end;
    }

    combat->turn_count = count;
    combat->turn_index = 0;
}

void combat_start(Combat *combat, Party *party, const Encounter *encounter,
                  uint32_t *random_state)
{
    *combat = (Combat){
        .party = party,
        .monster_count = encounter->count,
        .random_state = random_state
    };
    for (size_t index = 0; index < encounter->count; ++index) {
        combat->monsters[index] = encounter->monsters[index];
    }
    build_turn_order(combat);
}

size_t combat_monster_target(const Combat *combat)
{
    size_t target = PARTY_SIZE;
    for (size_t index = 0; index < PARTY_SIZE; ++index) {
        int hp = combat->party->members[index].hp;
        if (hp > 0 && (target == PARTY_SIZE || hp < combat->party->members[target].hp)) {
            target = index;
        }
    }
    return target;
}

CombatResult combat_result(const Combat *combat)
{
    if (combat_monster_target(combat) == PARTY_SIZE) {
        return COMBAT_DEFEAT;
    }
    for (size_t index = 0; index < combat->monster_count; ++index) {
        if (combat->monsters[index].hp > 0) {
            return COMBAT_ACTIVE;
        }
    }
    return COMBAT_VICTORY;
}

static void advance_turn(Combat *combat)
{
    for (size_t step = 0; step < combat->turn_count; ++step) {
        combat->turn_index = (combat->turn_index + 1) % combat->turn_count;
        if (combat_actor(combat, combat->turn_order[combat->turn_index])->hp > 0) {
            return;
        }
    }
}

AttackResult combat_attack(Combat *combat, size_t target_id)
{
    size_t actor_id = combat->turn_order[combat->turn_index];
    Character *actor = combat_actor(combat, actor_id);
    Character *target = combat_actor(combat, target_id);
    AttackResult result = {.type = actor->damage_type};
    int power = result.type == DAMAGE_SPELL ? actor->magic : actor->strength;

    if (actor->job == JOB_MAGE) {
        result.type = roll_mage_attack(combat, actor_id, &power);
    }

    if (result.type == DAMAGE_DEATH) {
        result.damage = power;
    } else if (result.type == DAMAGE_SPELL) {
        result.damage = spell_damage(power, target->magic_resist);
        if (target->magic_resist >= 99) {
            result.reflected = reflected_damage(power, actor->magic_resist);
        }
    } else {
        result.damage = physical_damage(power, target->defense, &result.recoil);
    }

    apply_damage(target, result.damage);
    apply_damage(actor, result.recoil);
    apply_damage(actor, result.reflected);
    advance_turn(combat);
    return result;
}
