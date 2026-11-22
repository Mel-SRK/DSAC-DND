#ifndef DND_LOOT_H
#define DND_LOOT_H

#include <stdbool.h>
#include <stddef.h>

#include "party.h"

typedef enum {
    LOOT_SHARED = 1,
    LOOT_BALANCED_BATCH,
    LOOT_BALANCED_RUN
} LootMode;

typedef struct {
    const char *name;
    int value;
} LootItem;

typedef struct {
    LootItem item;
    size_t owner;
} LootEntry;

typedef struct {
    LootMode mode;
    LootEntry *entries;
    size_t count;
    size_t capacity;
    int member_totals[PARTY_SIZE];
    int total;
} LootLedger;

bool loot_init(LootLedger *ledger, LootMode mode, size_t capacity);
bool loot_collect(LootLedger *ledger, const LootItem *items, size_t count);
void loot_sort_for_summary(LootLedger *ledger);
void loot_destroy(LootLedger *ledger);

#endif
