#include "combat.h"

#include <stdbool.h>

typedef struct {
    Initiative entries[MAX_COMBATANTS];
    size_t count;
} InitiativeHeap;

static bool acts_before(Initiative first, Initiative second)
{
    if (first.score != second.score) {
        return first.score > second.score;
    }
    if (first.dexterity_modifier != second.dexterity_modifier) {
        return first.dexterity_modifier > second.dexterity_modifier;
    }
    return first.actor_id < second.actor_id;
}

static void heap_push(InitiativeHeap *heap, Initiative entry)
{
    size_t index = heap->count++;
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (!acts_before(entry, heap->entries[parent])) {
            break;
        }
        heap->entries[index] = heap->entries[parent];
        index = parent;
    }
    heap->entries[index] = entry;
}

static Initiative heap_pop(InitiativeHeap *heap)
{
    Initiative first = heap->entries[0];
    Initiative last = heap->entries[--heap->count];
    size_t index = 0;
    while (2 * index + 1 < heap->count) {
        size_t child = 2 * index + 1;
        size_t right = child + 1;
        if (right < heap->count &&
            acts_before(heap->entries[right], heap->entries[child])) {
            child = right;
        }
        if (!acts_before(heap->entries[child], last)) {
            break;
        }
        heap->entries[index] = heap->entries[child];
        index = child;
    }
    heap->entries[index] = last;
    return first;
}

static int roll_d20(uint32_t *state)
{
    // 固定算法让同一个种子在不同平台上得到相同的先攻。
    *state = (uint32_t)((uint64_t)*state * UINT64_C(1664525) + UINT64_C(1013904223));
    return (int)((*state >> 16) % 20u) + 1;
}

Character *combat_actor(Combat *combat, size_t actor_id)
{
    if (actor_id < PARTY_SIZE) {
        return &combat->party->members[actor_id];
    }
    return &combat->monsters[actor_id - PARTY_SIZE];
}

void combat_start(Combat *combat, Party *party, const Encounter *encounter,
                  uint32_t *random_state)
{
    *combat = (Combat){.party = party, .monster_count = encounter->count};
    for (size_t index = 0; index < encounter->count; ++index) {
        combat->monsters[index] = encounter->monsters[index];
    }

    InitiativeHeap heap = {0};
    for (size_t id = 0; id < PARTY_SIZE + combat->monster_count; ++id) {
        const Character *actor = combat_actor(combat, id);
        if (actor->hp > 0) {
            heap_push(&heap, (Initiative){
                .actor_id = id,
                .score = roll_d20(random_state) + actor->dexterity_modifier,
                .dexterity_modifier = actor->dexterity_modifier
            });
        }
    }
    while (heap.count > 0) {
        combat->turn_order[combat->turn_count++] = heap_pop(&heap);
    }
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

void combat_attack(Combat *combat, size_t target_id)
{
    size_t actor_id = combat->turn_order[combat->turn_index].actor_id;
    const Character *actor = combat_actor(combat, actor_id);
    Character *target = combat_actor(combat, target_id);
    target->hp -= actor->attack;
    if (target->hp < 0) {
        target->hp = 0;
    }

    do {
        combat->turn_index = (combat->turn_index + 1) % combat->turn_count;
        actor_id = combat->turn_order[combat->turn_index].actor_id;
    } while (combat_actor(combat, actor_id)->hp == 0);
}
