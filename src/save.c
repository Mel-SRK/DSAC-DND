#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char save_path[] = "dnd_save.txt";

bool save_exists(void)
{
    FILE *file = fopen(save_path, "r");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

bool save_write(const GameState *game)
{
    FILE *file = fopen(save_path, "w");
    if (file == NULL) {
        return false;
    }
    fprintf(file, "DNDSAVE 1\n");
    fprintf(file, "progress %zu %d %d\n",
            game->room_id, game->floor_number, game->max_floor);
    for (size_t index = 0; index < PARTY_SIZE; ++index) {
        fprintf(file, "member %zu %d %d\n", index,
                game->party.members[index].hp, game->party.members[index].sp);
    }
    for (size_t index = 0; index < ROOM_COUNT; ++index) {
        fprintf(file, "room %zu %d %d\n", index,
                game->rooms[index].cleared ? 1 : 0,
                game->rooms[index].chest_opened ? 1 : 0);
    }
    for (size_t index = 0; index < game->ledger.count; ++index) {
        fprintf(file, "loot %s %d\n",
                game->ledger.items[index].name, game->ledger.items[index].value);
    }
    for (size_t index = 0; index < game->bestiary.count; ++index) {
        fprintf(file, "monster %s\n", game->bestiary.entries[index]->name);
    }
    return fclose(file) == 0;
}

bool save_read(GameState *game)
{
    FILE *file = fopen(save_path, "r");
    if (file == NULL) {
        return false;
    }

    memset(game, 0, sizeof(*game));
    if (!party_init(&game->party) ||
        !loot_init(&game->ledger, dungeon_loot_capacity(dungeon_map()))) {
        fclose(file);
        return false;
    }

    char line[256];
    if (fgets(line, sizeof line, file) == NULL || strncmp(line, "DNDSAVE", 7) != 0) {
        loot_destroy(&game->ledger);
        fclose(file);
        return false;
    }

    while (fgets(line, sizeof line, file) != NULL) {
        size_t index;
        int first;
        int second;
        char name[128];
        if (sscanf(line, "progress %zu %d %d", &index, &first, &second) == 3) {
            game->room_id = index;
            game->floor_number = first;
            game->max_floor = second;
        } else if (sscanf(line, "member %zu %d %d", &index, &first, &second) == 3) {
            if (index < PARTY_SIZE) {
                game->party.members[index].hp = first;
                game->party.members[index].sp = second;
            }
        } else if (sscanf(line, "room %zu %d %d", &index, &first, &second) == 3) {
            if (index < ROOM_COUNT) {
                game->rooms[index].cleared = first != 0;
                game->rooms[index].chest_opened = second != 0;
            }
        } else if (sscanf(line, "loot %127s %d", name, &first) == 2) {
            const LootItem *item = dungeon_find_loot(name);
            if (item != NULL) {
                loot_collect(&game->ledger, item, 1);
            }
        } else if (sscanf(line, "monster %127s", name) == 1) {
            const Character *monster = dungeon_find_monster(name);
            if (monster != NULL && game->bestiary.count < BESTIARY_CAPACITY) {
                game->bestiary.entries[game->bestiary.count++] = monster;
            }
        }
    }
    fclose(file);
    return true;
}

void save_clear(void)
{
    remove(save_path);
}
