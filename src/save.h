#ifndef DND_SAVE_H
#define DND_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dungeon.h"

enum { BESTIARY_CAPACITY = 32 };

/* 图鉴按首次遇到顺序记录怪物。 */
typedef struct {
    const Character *entries[BESTIARY_CAPACITY];
    size_t count;
} Bestiary;

typedef struct {
    bool cleared;
    bool chest_opened;
} RoomState;

/* 一局探险的完整状态，也是存档单位。 */
typedef struct {
    Party party;
    RoomState rooms[ROOM_COUNT];
    size_t room_id;
    int floor_number;
    int max_floor;
    uint32_t random_state;
    LootLedger ledger;
    Bestiary bestiary;
} GameState;

bool save_exists(void);
bool save_write(const GameState *game);
bool save_read(GameState *game, uint32_t fallback_random_state);
bool save_clear(void);

#endif
