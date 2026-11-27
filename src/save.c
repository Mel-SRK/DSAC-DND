#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "save.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
enum { TEMP_PATH_CAPACITY = MAX_PATH };
#else
#include <unistd.h>
enum { TEMP_PATH_CAPACITY = sizeof "dnd_save.txt.tmp.XXXXXX" };
#endif

static const char save_path[] = "dnd_save.txt";

bool save_exists(void)
{
#ifdef _WIN32
    return _access(save_path, 0) == 0 || errno != ENOENT;
#else
    return access(save_path, F_OK) == 0 || errno != ENOENT;
#endif
}

static FILE *open_temp_save(char path[TEMP_PATH_CAPACITY])
{
#ifdef _WIN32
    if (GetTempFileNameA(".", "dnd", 0, path) == 0) {
        return NULL;
    }
    FILE *file = fopen(path, "wb");
#else
    strcpy(path, "dnd_save.txt.tmp.XXXXXX");
    int descriptor = mkstemp(path);
    if (descriptor < 0) {
        return NULL;
    }
    FILE *file = fdopen(descriptor, "wb");
    if (file == NULL) {
        close(descriptor);
    }
#endif
    if (file == NULL) {
        remove(path);
    }
    return file;
}

static bool sync_save(FILE *file)
{
    if (ferror(file) || fflush(file) != 0) {
        return false;
    }
#ifdef _WIN32
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

static bool replace_save(const char *path)
{
#ifdef _WIN32
    return MoveFileExA(path, save_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(path, save_path) == 0;
#endif
}

bool save_write(const GameState *game)
{
    char path[TEMP_PATH_CAPACITY];
    FILE *file = open_temp_save(path);
    if (file == NULL) {
        return false;
    }
    fprintf(file, "DNDSAVE 2\n");
    fprintf(file, "progress %zu %d %d\n",
            game->room_id, game->floor_number, game->max_floor);
    fprintf(file, "random %" PRIu32 "\n", game->random_state);
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
    fputs("end\n", file);

    bool success = sync_save(file);
    if (fclose(file) != 0) {
        success = false;
    }
    if (success && replace_save(path)) {
        return true;
    }
    remove(path);
    return false;
}

enum { SAVE_LINE_CAPACITY = 256 };

typedef enum { LINE_OK, LINE_END, LINE_ERROR } LineStatus;

static LineStatus read_line(FILE *file, char line[SAVE_LINE_CAPACITY])
{
    if (fgets(line, SAVE_LINE_CAPACITY, file) == NULL) {
        return ferror(file) ? LINE_ERROR : LINE_END;
    }
    char *newline = strchr(line, '\n');
    if (newline == NULL) {
        return LINE_ERROR;
    }
    if (newline > line && newline[-1] == '\r') {
        --newline;
    }
    *newline = '\0';
    return LINE_OK;
}

static bool parse_numbers(const char *line, const char *label,
                          uintmax_t *numbers, size_t count)
{
    size_t length = strlen(label);
    if (strncmp(line, label, length) != 0) {
        return false;
    }
    const char *cursor = line + length;
    for (size_t index = 0; index < count; ++index) {
        if (*cursor != ' ' || cursor[1] < '0' || cursor[1] > '9') {
            return false;
        }
        errno = 0;
        char *end;
        numbers[index] = strtoumax(cursor + 1, &end, 10);
        if (errno == ERANGE) {
            return false;
        }
        cursor = end;
    }
    return *cursor == '\0';
}

static bool read_numbers(FILE *file, const char *label,
                         uintmax_t *numbers, size_t count)
{
    char line[SAVE_LINE_CAPACITY];
    return read_line(file, line) == LINE_OK && parse_numbers(line, label, numbers, count);
}

static bool read_progress(FILE *file, GameState *game, bool legacy)
{
    uintmax_t fields[3];
    size_t floor_count = dungeon_map()->floor_count;
    if (!read_numbers(file, "progress", fields, 3) ||
        fields[0] >= ROOM_COUNT || fields[1] == 0 || fields[1] > floor_count ||
        fields[2] == 0 || fields[2] > floor_count) {
        return false;
    }
    game->room_id = (size_t)fields[0];
    game->floor_number = dungeon_room(game->room_id)->floor_number;
    game->max_floor = (int)fields[2];
    if (legacy) {
        /* 旧版在换层时可能把新房间和旧楼层写入同一个存档。 */
        if (game->max_floor < game->floor_number) {
            game->max_floor = game->floor_number;
        }
        return true;
    }
    if (fields[1] != (uintmax_t)game->floor_number || game->max_floor < game->floor_number ||
        !read_numbers(file, "random", fields, 1) || fields[0] > UINT32_MAX) {
        return false;
    }
    game->random_state = (uint32_t)fields[0];
    return true;
}

static bool read_party_and_rooms(FILE *file, GameState *game)
{
    uintmax_t fields[3];
    for (size_t index = 0; index < PARTY_SIZE; ++index) {
        if (!read_numbers(file, "member", fields, 3) || fields[0] != index ||
            fields[1] > 99 || fields[2] > 99) {
            return false;
        }
        game->party.members[index].hp = (int)fields[1];
        game->party.members[index].sp = (int)fields[2];
    }
    for (size_t index = 0; index < ROOM_COUNT; ++index) {
        if (!read_numbers(file, "room", fields, 3) || fields[0] != index ||
            fields[1] > 1 || fields[2] > 1) {
            return false;
        }
        game->rooms[index].cleared = fields[1] != 0;
        game->rooms[index].chest_opened = fields[2] != 0;
    }
    return true;
}

static bool read_loot(char *name, LootLedger *ledger)
{
    char *last_space = strrchr(name, ' ');
    uintmax_t value;
    if (last_space == NULL || !parse_numbers(last_space, "", &value, 1) ||
        value > (uintmax_t)(INT_MAX - ledger->total)) {
        return false;
    }
    *last_space = '\0';
    const LootItem *definition = dungeon_find_loot(name);
    if (definition == NULL) {
        return false;
    }
    LootItem item = {definition->name, (int)value};
    return loot_collect(ledger, &item, 1);
}

static bool read_rewards(FILE *file, GameState *game, bool legacy)
{
    char line[SAVE_LINE_CAPACITY];
    LineStatus status;
    while ((status = read_line(file, line)) == LINE_OK) {
        if (!legacy && strcmp(line, "end") == 0) {
            return fgetc(file) == EOF && !ferror(file);
        }
        if (strncmp(line, "loot ", 5) == 0) {
            if (!read_loot(line + 5, &game->ledger)) {
                return false;
            }
        } else if (strncmp(line, "monster ", 8) == 0) {
            const Character *monster = dungeon_find_monster(line + 8);
            if (monster == NULL || game->bestiary.count == BESTIARY_CAPACITY) {
                return false;
            }
            game->bestiary.entries[game->bestiary.count++] = monster;
        } else {
            return false;
        }
    }
    return legacy && status == LINE_END;
}

bool save_read(GameState *game, uint32_t fallback_random_state)
{
    FILE *file = fopen(save_path, "rb");
    if (file == NULL) {
        return false;
    }
    *game = (GameState){.random_state = fallback_random_state};
    char header[SAVE_LINE_CAPACITY];
    bool has_header = read_line(file, header) == LINE_OK;
    bool legacy = has_header && strcmp(header, "DNDSAVE 1") == 0;
    bool success = (legacy || (has_header && strcmp(header, "DNDSAVE 2") == 0)) &&
                   party_init(&game->party) &&
                   loot_init(&game->ledger, dungeon_loot_capacity(dungeon_map())) &&
                   read_progress(file, game, legacy) &&
                   read_party_and_rooms(file, game) &&
                   read_rewards(file, game, legacy);
    if (fclose(file) != 0) {
        success = false;
    }
    if (!success) {
        loot_destroy(&game->ledger);
    }
    return success;
}

bool save_clear(void)
{
#ifdef _WIN32
    return _unlink(save_path) == 0 || errno == ENOENT;
#else
    return unlink(save_path) == 0 || errno == ENOENT;
#endif
}
