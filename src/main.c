#include "dungeon.h"
#include "save.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

typedef enum {
    GAME_QUIT,
    GAME_VICTORY,
    GAME_DEFEAT,
    GAME_ERROR
} GameResult;

/*
 * Windows 控制台默认使用本地代码页（简体中文是 936），而源码里的中文是 UTF-8。
 * 这里主动把控制台切成 UTF-8，避免直接双击或从 cmd 启动时显示乱码。
 */
static void console_init(void)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

static size_t console_width(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int width = info.srWindow.Right - info.srWindow.Left + 1;
        if (width > 0) {
            return (size_t)width;
        }
    }
#endif
    return 80;
}

/* 中文在终端里占两列，这里按 UTF-8 字节粗略估算显示宽度。 */
static size_t display_width(const char *text)
{
    size_t width = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
        unsigned char byte = *cursor;
        if (byte < 0x80) {
            width += 1;
            cursor += 1;
        } else if ((byte >> 5) == 0x6) {
            width += 1;
            cursor += 2;
        } else if ((byte >> 4) == 0xE) {
            width += 2;
            cursor += 3;
        } else {
            width += 2;
            cursor += 4;
        }
    }
    return width;
}

static void print_centered(const char *text, size_t width)
{
    size_t text_width = display_width(text);
    size_t padding = text_width < width ? (width - text_width) / 2 : 0;
    for (size_t index = 0; index < padding; ++index) {
        putchar(' ');
    }
    puts(text);
}

static void print_right(const char *text, size_t width)
{
    size_t text_width = display_width(text);
    size_t padding = text_width < width ? width - text_width : 0;
    for (size_t index = 0; index < padding; ++index) {
        putchar(' ');
    }
    puts(text);
}

static int read_choice(const char *prompt, int minimum, int maximum, int default_choice)
{
    char line[128];
    for (;;) {
        fputs(prompt, stdout);
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            return -1;
        }
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int character;
            do {
                character = getchar();
            } while (character != '\n' && character != EOF);
            puts("输入过长，请重新输入编号。");
            continue;
        }

        char *start = line;
        while (isspace((unsigned char)*start)) {
            ++start;
        }
        if (*start == '\0' && default_choice >= minimum) {
            return default_choice;
        }
        errno = 0;
        char *end;
        long choice = strtol(start, &end, 10);
        if (end != start && errno == 0 && choice >= minimum && choice <= maximum) {
            while (isspace((unsigned char)*end)) {
                ++end;
            }
            if (*end == '\0') {
                return (int)choice;
            }
        }
        puts("请输入有效编号。");
    }
}

static void bestiary_record(Bestiary *bestiary, const Character *monster)
{
    if (bestiary->count >= BESTIARY_CAPACITY) {
        return;
    }
    for (size_t index = 0; index < bestiary->count; ++index) {
        if (strcmp(bestiary->entries[index]->name, monster->name) == 0) {
            return;
        }
    }
    bestiary->entries[bestiary->count++] = monster;
}

static void print_character_stats(const Character *character)
{
    printf("  %s｜普通攻击 %s\n", character->name, damage_type_text(character->damage_type));
    printf("    生命 %d  SP %d  防御 %d  魔抗 %d\n",
           character->hp, character->sp, character->defense, character->magic_resist);
    printf("    力量 %d  速度 %d  魔力 %d  幸运 %d  声望 %d\n",
           character->strength, character->speed, character->magic,
           character->luck, character->reputation);
}

static void show_party_stats(const Party *party)
{
    puts("[人物属性]");
    for (size_t index = 0; index < PARTY_SIZE; ++index) {
        print_character_stats(&party->members[index]);
    }
}

static void show_bestiary(const Bestiary *bestiary)
{
    if (bestiary == NULL || bestiary->count == 0) {
        puts("[怪物图鉴] 还没有遇到过任何怪物。");
        return;
    }
    printf("[怪物图鉴] 已记录 %zu 种\n", bestiary->count);
    for (size_t index = 0; index < bestiary->count; ++index) {
        print_character_stats(bestiary->entries[index]);
    }
}

static void print_loot_entry(const char *label, const LootItem *item)
{
    printf("[%s] %s（价值 %d）\n", label, item->name, item->value);
}

static bool collect_reward(LootLedger *ledger, const DungeonArea *area)
{
    size_t first = ledger->count;
    if (!loot_collect(ledger, area->loot, area->loot_count)) {
        fputs("战利品记录容量不足。\n", stderr);
        return false;
    }
    for (size_t index = first; index < ledger->count; ++index) {
        print_loot_entry("获得", &ledger->items[index]);
    }
    return true;
}

static void show_room(const DungeonRoom *room, const Party *party)
{
    printf("\n[地点] 第 %d 层 · %s\n", room->floor_number, room->name);
    fputs("小队生命", stdout);
    for (size_t index = 0; index < PARTY_SIZE; ++index) {
        printf("  %s %d", party->members[index].name, party->members[index].hp);
    }
    putchar('\n');
    for (size_t index = 0; index < room->area_count; ++index) {
        printf("  区域：%s\n", room->areas[index].name);
    }
}

static int choose_attack_target(const Combat *combat)
{
    puts("可攻击的目标");
    for (size_t index = 0; index < combat->monster_count; ++index) {
        const Character *monster = &combat->monsters[index];
        if (monster->hp > 0) {
            printf("  %zu. %s（生命 %d）\n", index + 1, monster->name, monster->hp);
        }
    }
    for (;;) {
        int choice = read_choice("攻击目标 > ", 1, (int)combat->monster_count, -1);
        if (choice < 0 || combat->monsters[choice - 1].hp > 0) {
            return choice;
        }
        puts("该目标已经倒地，请重新选择。");
    }
}

static CombatResult run_battle(Party *party, const DungeonArea *area, uint32_t *random_state,
                               Bestiary *bestiary)
{
    Combat combat;
    for (size_t index = 0; index < area->encounter.count; ++index) {
        bestiary_record(bestiary, &area->encounter.monsters[index]);
    }
    combat_start(&combat, party, &area->encounter, random_state);
    printf("[开战] %s\n", area->name);
    fputs("[先攻]", stdout);
    for (size_t index = 0; index < combat.turn_count; ++index) {
        const Character *actor = combat_actor(&combat, combat.turn_order[index]);
        printf(" %s(速度 %d)", actor->name, actor->speed);
    }
    putchar('\n');

    CombatResult result;
    while ((result = combat_result(&combat)) == COMBAT_ACTIVE) {
        size_t actor_id = combat.turn_order[combat.turn_index];
        const Character *actor = combat_actor(&combat, actor_id);
        size_t target_id;
        if (actor_id < PARTY_SIZE) {
            printf("轮到%s行动。\n", actor->name);
            for (;;) {
                puts("  1. 普通攻击");
                puts("  2. 法术");
                int action = read_choice("行动方式 > ", 1, 2, -1);
                if (action < 0) {
                    return COMBAT_ACTIVE;
                }
                if (action == 1) {
                    break;
                }
                puts("暂时没有可用的技能。");
            }
            int choice = choose_attack_target(&combat);
            if (choice < 0) {
                return COMBAT_ACTIVE;
            }
            target_id = PARTY_SIZE + (size_t)(choice - 1);
        } else {
            target_id = combat_monster_target(&combat);
        }
        Character *target = combat_actor(&combat, target_id);
        AttackResult attack = combat_attack(&combat, target_id);
        printf("\n\n%s对%s造成了%d点伤害（%s），%s当前剩余血量：%d。\n",
               actor->name, target->name, attack.damage,
               damage_type_text(attack.type), target->name, target->hp);
        if (attack.recoil > 0) {
            printf("%s被%s的防御反噬，受到%d点伤害，%s当前剩余血量：%d。\n",
                   actor->name, target->name, attack.recoil, actor->name, actor->hp);
        }
        if (attack.reflected > 0) {
            printf("%s被%s的魔抗反弹，受到%d点法术伤害，%s当前剩余血量：%d。\n",
                   actor->name, target->name, attack.reflected, actor->name, actor->hp);
        }
        putchar('\n');
    }
    return result;
}

/* 设置菜单：属性、图鉴、退出探险都收在这里。返回 true 表示要退出本次探险。 */
static bool run_settings(GameState *game)
{
    for (;;) {
        puts("\n[设置]");
        puts("  1. 人物属性");
        puts("  2. 怪物图鉴");
        puts("  3. 退出探险");
        puts("  0. 返回");
        int choice = read_choice("设置 > ", 0, 3, -1);
        if (choice < 0 || choice == 3) {
            return true;
        }
        if (choice == 0) {
            return false;
        }
        if (choice == 1) {
            show_party_stats(&game->party);
        } else {
            show_bestiary(&game->bestiary);
        }
    }
}

static GameResult explore(GameState *game, uint32_t *random_state)
{
    for (;;) {
        const DungeonRoom *room = dungeon_room(game->room_id);
        RoomState *state = &game->rooms[room->id];
        game->floor_number = room->floor_number;
        if (room->floor_number > game->max_floor) {
            game->max_floor = room->floor_number;
        }

        const DungeonArea *chest = NULL;
        show_room(room, &game->party);
        for (size_t index = 0; index < room->area_count; ++index) {
            const DungeonArea *area = &room->areas[index];
            if (area->kind == AREA_ENCOUNTER && !state->cleared) {
                CombatResult result = run_battle(&game->party, area, random_state,
                                                 &game->bestiary);
                if (result == COMBAT_ACTIVE) {
                    save_write(game);
                    return GAME_QUIT;
                }
                if (result == COMBAT_DEFEAT) {
                    save_clear();
                    return GAME_DEFEAT;
                }
                state->cleared = true;
                printf("[清理] %s\n", room->name);
                if (!collect_reward(&game->ledger, area)) {
                    return GAME_ERROR;
                }
                if (room->id == ROOM_BOSS) {
                    save_clear();
                    return GAME_VICTORY;
                }
            }
            if (area->kind == AREA_CHEST && !state->chest_opened) {
                chest = area;
            }
        }

        const DungeonLinks *exits = dungeon_links(room);
        for (size_t index = 0; index < exits->count; ++index) {
            const DungeonRoom *destination = exits->rooms[index];
            printf("  %zu. 前往第 %d 层 · %s\n",
                   index + 1, destination->floor_number, destination->name);
        }
        int choice_count = (int)exits->count;
        int chest_choice = 0;
        if (chest != NULL) {
            chest_choice = ++choice_count;
            printf("  %d. 打开宝箱\n", chest_choice);
        }
        int settings_choice = ++choice_count;
        printf("  %d. 设置\n", settings_choice);
        int choice = read_choice("探索操作 > ", 1, choice_count, -1);
        if (choice < 0) {
            save_write(game);
            return GAME_QUIT;
        }
        putchar('\n');
        if (chest != NULL && choice == chest_choice) {
            if (!collect_reward(&game->ledger, chest)) {
                return GAME_ERROR;
            }
            state->chest_opened = true;
        } else if (choice == settings_choice) {
            if (run_settings(game)) {
                save_write(game);
                return GAME_QUIT;
            }
        } else {
            game->room_id = exits->rooms[choice - 1]->id;
        }
        save_write(game);
    }
}

static void show_summary(LootLedger *ledger, GameResult result)
{
    puts(result == GAME_VICTORY ? "\n[结果] 通关" : "\n[结果] 全员倒地");
    puts("[战利品结算]");
    loot_sort_for_summary(ledger);
    for (size_t index = 0; index < ledger->count; ++index) {
        print_loot_entry("结算", &ledger->items[index]);
    }
    printf("[总价值] %d\n", ledger->total);
}

static void new_game(GameState *game)
{
    memset(game, 0, sizeof(*game));
    party_init(&game->party);
    loot_init(&game->ledger, dungeon_loot_capacity(dungeon_map()));
    game->room_id = ROOM_ENTRY;
    game->floor_number = 1;
    game->max_floor = 1;
    save_write(game);
}

static void draw_title(bool has_save, int floor_number, int max_floor)
{
    size_t width = console_width();
    putchar('\n');
    print_centered("DND 地下城探险", width);
    print_centered("（标题占位）", width);
    putchar('\n');
    print_centered("1. 开始", width);
    print_centered("2. 继续", width);
    print_centered("3. 人物属性", width);
    print_centered("4. 怪物图鉴", width);
    print_centered("0. 退出", width);
    putchar('\n');
    char status[128];
    snprintf(status, sizeof(status), "当前层数：%d   最远层数：%d",
             has_save ? floor_number : 0, has_save ? max_floor : 0);
    print_right(status, width);
}

static bool parse_seed(const char *text, uint32_t *seed)
{
    if (!isdigit((unsigned char)text[0])) {
        return false;
    }
    errno = 0;
    char *end;
    uintmax_t value = strtoumax(text, &end, 10);
    if (errno != 0 || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    *seed = (uint32_t)value;
    return true;
}

int main(int argc, char *argv[])
{
    console_init();
    uint32_t seed = (uint32_t)time(NULL);
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("用法：dnd [--seed 0-4294967295]");
        return EXIT_SUCCESS;
    }
    if (argc != 1 &&
        (argc != 3 || strcmp(argv[1], "--seed") != 0 || !parse_seed(argv[2], &seed))) {
        fputs("用法：dnd [--seed 0-4294967295]\n", stderr);
        return EXIT_FAILURE;
    }

    Party party;
    if (!party_init(&party)) {
        fputs("角色属性超出 0-99 的范围。\n", stderr);
        return EXIT_FAILURE;
    }
    if (!dungeon_validate(dungeon_map())) {
        fputs("地下城里的怪物属性超出 0-99 的范围。\n", stderr);
        return EXIT_FAILURE;
    }

    for (;;) {
        GameState saved;
        bool has_save = save_read(&saved);
        draw_title(has_save, has_save ? saved.floor_number : 0,
                   has_save ? saved.max_floor : 0);
        int choice = read_choice("请选择 > ", 0, 4, -1);
        if (choice <= 0) {
            if (has_save) {
                loot_destroy(&saved.ledger);
            }
            break;
        }
        if (choice == 3) {
            show_party_stats(&party);
            if (has_save) {
                loot_destroy(&saved.ledger);
            }
            continue;
        }
        if (choice == 4) {
            show_bestiary(has_save ? &saved.bestiary : NULL);
            if (has_save) {
                loot_destroy(&saved.ledger);
            }
            continue;
        }
        if (choice == 2 && !has_save) {
            puts("没有可以继续的存档。");
            continue;
        }

        GameState game;
        if (choice == 2) {
            game = saved;
        } else {
            if (has_save) {
                loot_destroy(&saved.ledger);
            }
            new_game(&game);
        }
        GameResult result = explore(&game, &seed);
        if (result == GAME_VICTORY || result == GAME_DEFEAT) {
            show_summary(&game.ledger, result);
        } else if (result == GAME_QUIT) {
            puts("\n[结果] 已退出探险");
        }
        loot_destroy(&game.ledger);
        if (result == GAME_ERROR) {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
