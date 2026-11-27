#ifndef DND_LOOT_H
#define DND_LOOT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *name;
    int value;
} LootItem;

/* 战利品一律进入小队仓库，只记录物品本身。 */
typedef struct {
    LootItem *items;
    size_t count;
    size_t capacity;
    int total;
} LootLedger;

bool loot_init(LootLedger *ledger, size_t capacity);
bool loot_collect(LootLedger *ledger, const LootItem *items, size_t count);
void loot_sort_for_summary(LootLedger *ledger);
void loot_destroy(LootLedger *ledger);

#endif
