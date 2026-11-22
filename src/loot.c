#include "loot.h"

#include <stdlib.h>

bool loot_init(LootLedger *ledger, LootMode mode, size_t capacity)
{
    *ledger = (LootLedger){.mode = mode, .capacity = capacity};
    if (capacity == 0) {
        return true;
    }
    ledger->entries = calloc(capacity, sizeof(*ledger->entries));
    return ledger->entries != NULL;
}

static void sort_by_value(LootEntry *entries, size_t count)
{
    for (size_t index = 1; index < count; ++index) {
        LootEntry entry = entries[index];
        size_t position = index;
        while (position > 0 && entries[position - 1].item.value < entry.item.value) {
            entries[position] = entries[position - 1];
            --position;
        }
        entries[position] = entry;
    }
}

static size_t lowest_total(const int totals[PARTY_SIZE])
{
    size_t lowest = 0;
    for (size_t index = 1; index < PARTY_SIZE; ++index) {
        if (totals[index] < totals[lowest]) {
            lowest = index;
        }
    }
    return lowest;
}

bool loot_collect(LootLedger *ledger, const LootItem *items, size_t count)
{
    if (count == 0) {
        return true;
    }
    if (count > ledger->capacity - ledger->count) {
        return false;
    }
    LootEntry *batch = ledger->entries + ledger->count;
    for (size_t index = 0; index < count; ++index) {
        batch[index] = (LootEntry){.item = items[index], .owner = PARTY_SIZE};
    }
    sort_by_value(batch, count);

    int batch_totals[PARTY_SIZE] = {0};
    int *totals = ledger->mode == LOOT_BALANCED_RUN ? ledger->member_totals : batch_totals;
    for (size_t index = 0; index < count; ++index) {
        LootEntry *entry = &batch[index];
        if (ledger->mode != LOOT_SHARED) {
            entry->owner = lowest_total(totals);
            ledger->member_totals[entry->owner] += entry->item.value;
            batch_totals[entry->owner] += entry->item.value;
        }
        ledger->total += entry->item.value;
    }
    ledger->count += count;
    return true;
}

void loot_sort_for_summary(LootLedger *ledger)
{
    sort_by_value(ledger->entries, ledger->count);
}

void loot_destroy(LootLedger *ledger)
{
    free(ledger->entries);
}
