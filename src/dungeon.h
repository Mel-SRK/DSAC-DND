#ifndef DND_DUNGEON_H
#define DND_DUNGEON_H

#include <stdbool.h>

#include "combat.h"
#include "loot.h"

typedef enum {
    ROOM_ENTRY,
    ROOM_GUARD,
    ROOM_STORAGE,
    ROOM_UPPER_STAIRS,
    ROOM_LOWER_STAIRS,
    ROOM_HALL,
    ROOM_BOSS,
    ROOM_COUNT
} DungeonRoomId;

typedef enum {
    AREA_SCENERY,
    AREA_ENCOUNTER,
    AREA_CHEST,
    AREA_STAIRS
} DungeonAreaKind;

typedef struct {
    const char *name;
    DungeonAreaKind kind;
    Encounter encounter;
    const LootItem *loot;
    size_t loot_count;
} DungeonArea;

typedef struct {
    DungeonRoomId id;
    const char *name;
    int floor_number;
    const DungeonArea *areas;
    size_t area_count;
} DungeonRoom;

typedef struct {
    const DungeonRoom *rooms;
    size_t room_count;
} DungeonFloor;

typedef struct {
    const DungeonFloor *floors;
    size_t floor_count;
} Dungeon;

typedef struct {
    size_t count;
    const DungeonRoom *rooms[3];
} DungeonLinks;

const Dungeon *dungeon_map(void);
const DungeonRoom *dungeon_room(size_t room_id);
const DungeonLinks *dungeon_links(const DungeonRoom *room);
const LootItem *dungeon_find_loot(const char *name);
const Character *dungeon_find_monster(const char *name);
size_t dungeon_loot_capacity(const Dungeon *dungeon);
bool dungeon_validate(const Dungeon *dungeon);

#endif
