#include "loot.h"

#include <stdlib.h>

bool loot_init(LootLedger *ledger, size_t capacity)
{
    *ledger = (LootLedger){.capacity = capacity};
    if (capacity == 0) {
        return true;
    }
    ledger->items = calloc(capacity, sizeof(*ledger->items));
    return ledger->items != NULL;
}

static void sort_by_value(LootItem *items, size_t count)
{
    for (size_t index = 1; index < count; ++index) {
        LootItem item = items[index];
        size_t position = index;
        while (position > 0 && items[position - 1].value < item.value) {
            items[position] = items[position - 1];
            --position;
        }
        items[position] = item;
    }
}

bool loot_collect(LootLedger *ledger, const LootItem *items, size_t count)
{
    if (count == 0) {
        return true;
    }
    if (ledger->count > ledger->capacity || count > ledger->capacity - ledger->count) {
        return false;
    }
    LootItem *batch = ledger->items + ledger->count;
    for (size_t index = 0; index < count; ++index) {
        batch[index] = items[index];
        ledger->total += items[index].value;
    }
    sort_by_value(batch, count);
    ledger->count += count;
    return true;
}

void loot_sort_for_summary(LootLedger *ledger)
{
    sort_by_value(ledger->items, ledger->count);
}

void loot_destroy(LootLedger *ledger)
{
    free(ledger->items);
    ledger->items = NULL;
    ledger->count = 0;
}
