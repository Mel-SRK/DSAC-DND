#include "dungeon.h"

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

static const Character guard_monsters[] = {
    {"哥布林守卫", 14, 4, 2},
    {"洞穴蝠", 7, 3, 4}
};
static const Character hall_monsters[] = {
    {"骷髅哨兵", 18, 5, 1},
    {"石像鬼", 11, 4, 2}
};
static const Character boss_monsters[] = {
    {"地牢领主", 38, 9, 2},
    {"影侍", 11, 6, 4}
};

_Static_assert(ARRAY_LENGTH(guard_monsters) <= MAX_MONSTERS, "too many guard monsters");
_Static_assert(ARRAY_LENGTH(hall_monsters) <= MAX_MONSTERS, "too many hall monsters");
_Static_assert(ARRAY_LENGTH(boss_monsters) <= MAX_MONSTERS, "too many boss monsters");

static const LootItem guard_loot[] = {
    {"守卫徽章", 9},
    {"秘银碎片", 18},
    {"旧铜币", 4}
};
static const LootItem storage_loot[] = {
    {"雕花银杯", 14},
    {"蓝晶石", 23}
};
static const LootItem hall_loot[] = {
    {"石像之心", 12},
    {"古银币", 16}
};
static const LootItem boss_loot[] = {
    {"领主印戒", 40},
    {"黑曜石", 27},
    {"纹章残片", 8}
};

static const DungeonArea entry_areas[] = {
    {.name = "入口营地", .kind = AREA_SCENERY}
};
static const DungeonArea guard_areas[] = {
    {
        .name = "守卫哨岗", .kind = AREA_ENCOUNTER,
        .encounter = {guard_monsters, ARRAY_LENGTH(guard_monsters)},
        .loot = guard_loot, .loot_count = ARRAY_LENGTH(guard_loot)
    }
};
static const DungeonArea storage_areas[] = {
    {.name = "堆放区", .kind = AREA_SCENERY},
    {
        .name = "宝箱区", .kind = AREA_CHEST,
        .loot = storage_loot, .loot_count = ARRAY_LENGTH(storage_loot)
    }
};
static const DungeonArea upper_stair_areas[] = {
    {.name = "下行楼梯", .kind = AREA_STAIRS}
};
static const DungeonArea lower_stair_areas[] = {
    {.name = "上行楼梯", .kind = AREA_STAIRS}
};
static const DungeonArea hall_areas[] = {
    {
        .name = "石像长廊", .kind = AREA_ENCOUNTER,
        .encounter = {hall_monsters, ARRAY_LENGTH(hall_monsters)},
        .loot = hall_loot, .loot_count = ARRAY_LENGTH(hall_loot)
    }
};
static const DungeonArea boss_areas[] = {
    {
        .name = "领主王座", .kind = AREA_ENCOUNTER,
        .encounter = {boss_monsters, ARRAY_LENGTH(boss_monsters)},
        .loot = boss_loot, .loot_count = ARRAY_LENGTH(boss_loot)
    }
};

static const DungeonRoom rooms[ROOM_COUNT] = {
    [ROOM_ENTRY] = {ROOM_ENTRY, "入口", 1, entry_areas, ARRAY_LENGTH(entry_areas)},
    [ROOM_GUARD] = {ROOM_GUARD, "守卫室", 1, guard_areas, ARRAY_LENGTH(guard_areas)},
    [ROOM_STORAGE] = {ROOM_STORAGE, "储藏室", 1, storage_areas, ARRAY_LENGTH(storage_areas)},
    [ROOM_UPPER_STAIRS] = {
        ROOM_UPPER_STAIRS, "楼梯间", 1, upper_stair_areas, ARRAY_LENGTH(upper_stair_areas)
    },
    [ROOM_LOWER_STAIRS] = {
        ROOM_LOWER_STAIRS, "楼梯间", 2, lower_stair_areas, ARRAY_LENGTH(lower_stair_areas)
    },
    [ROOM_HALL] = {ROOM_HALL, "前厅", 2, hall_areas, ARRAY_LENGTH(hall_areas)},
    [ROOM_BOSS] = {ROOM_BOSS, "首领室", 2, boss_areas, ARRAY_LENGTH(boss_areas)}
};

static const DungeonFloor floors[] = {
    {&rooms[ROOM_ENTRY], ROOM_LOWER_STAIRS},
    {&rooms[ROOM_LOWER_STAIRS], ROOM_COUNT - ROOM_LOWER_STAIRS}
};
static const Dungeon dungeon = {floors, ARRAY_LENGTH(floors)};

// 邻接表引用树里的同一个房间对象。楼梯连接也用双向边表示。
static const DungeonLinks links[ROOM_COUNT] = {
    [ROOM_ENTRY] = {1, {&rooms[ROOM_GUARD]}},
    [ROOM_GUARD] = {3, {&rooms[ROOM_ENTRY], &rooms[ROOM_STORAGE], &rooms[ROOM_UPPER_STAIRS]}},
    [ROOM_STORAGE] = {1, {&rooms[ROOM_GUARD]}},
    [ROOM_UPPER_STAIRS] = {2, {&rooms[ROOM_GUARD], &rooms[ROOM_LOWER_STAIRS]}},
    [ROOM_LOWER_STAIRS] = {2, {&rooms[ROOM_UPPER_STAIRS], &rooms[ROOM_HALL]}},
    [ROOM_HALL] = {2, {&rooms[ROOM_LOWER_STAIRS], &rooms[ROOM_BOSS]}},
    [ROOM_BOSS] = {1, {&rooms[ROOM_HALL]}}
};

const Dungeon *dungeon_map(void)
{
    return &dungeon;
}

const DungeonLinks *dungeon_links(const DungeonRoom *room)
{
    return &links[room->id];
}

size_t dungeon_loot_capacity(const Dungeon *map)
{
    size_t capacity = 0;
    for (size_t floor_index = 0; floor_index < map->floor_count; ++floor_index) {
        const DungeonFloor *floor = &map->floors[floor_index];
        for (size_t room_index = 0; room_index < floor->room_count; ++room_index) {
            const DungeonRoom *room = &floor->rooms[room_index];
            for (size_t area_index = 0; area_index < room->area_count; ++area_index) {
                capacity += room->areas[area_index].loot_count;
            }
        }
    }
    return capacity;
}
