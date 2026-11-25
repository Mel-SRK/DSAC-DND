#include "dungeon.h"

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

typedef struct {
    bool cleared;
    bool chest_opened;
} RoomState;

typedef enum {
    GAME_QUIT,
    GAME_VICTORY,
    GAME_DEFEAT,
    GAME_ERROR
} GameResult;

enum { BESTIARY_CAPACITY = 32 };

/* 只记录战斗里真正遇到过的怪物，按首次出现的顺序排列。 */
typedef struct {
    const Character *entries[BESTIARY_CAPACITY];
    size_t count;
} Bestiary;

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
    if (bestiary->count == 0) {
        puts("[怪物图鉴] 还没有遇到过任何怪物。");
        return;
    }
    printf("[怪物图鉴] 已记录 %zu 种\n", bestiary->count);
    for (size_t index = 0; index < bestiary->count; ++index) {
        print_character_stats(bestiary->entries[index]);
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
        printf("\n[攻击] %s → %s（%s），伤害 %d，剩余生命 %d\n",
               actor->name, target->name, damage_type_text(attack.type),
               attack.damage, target->hp);
        if (attack.recoil > 0) {
            printf("[反噬] %s 受到 %d 点伤害，剩余生命 %d\n",
                   actor->name, attack.recoil, actor->hp);
        }
        if (attack.reflected > 0) {
            printf("[反弹] %s 受到 %d 点法术伤害，剩余生命 %d\n",
                   actor->name, attack.reflected, actor->hp);
        }
    }
    return result;
}

static void print_loot_entry(const char *label, const LootEntry *entry, const Party *party)
{
    const char *owner = "小队仓库";
    if (entry->owner < PARTY_SIZE) {
        owner = party->members[entry->owner].name;
    }
    printf("[%s] %s | 价值 %d | %s\n", label, entry->item.name, entry->item.value, owner);
}

static bool collect_reward(LootLedger *ledger, const DungeonArea *area, const Party *party)
{
    size_t first = ledger->count;
    if (!loot_collect(ledger, area->loot, area->loot_count)) {
        fputs("战利品记录容量不足。\n", stderr);
        return false;
    }
    for (size_t index = first; index < ledger->count; ++index) {
        print_loot_entry("获得", &ledger->entries[index], party);
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

static GameResult explore(const Dungeon *map, Party *party, LootLedger *ledger,
                          uint32_t *random_state, Bestiary *bestiary)
{
    RoomState rooms[ROOM_COUNT] = {0};
    const DungeonRoom *room = &map->floors[0].rooms[0];
    for (;;) {
        RoomState *state = &rooms[room->id];
        const DungeonArea *chest = NULL;
        show_room(room, party);
        for (size_t index = 0; index < room->area_count; ++index) {
            const DungeonArea *area = &room->areas[index];
            if (area->kind == AREA_ENCOUNTER && !state->cleared) {
                CombatResult result = run_battle(party, area, random_state, bestiary);
                if (result == COMBAT_ACTIVE) {
                    return GAME_QUIT;
                }
                if (result == COMBAT_DEFEAT) {
                    return GAME_DEFEAT;
                }
                state->cleared = true;
                printf("[清理] %s\n", room->name);
                if (!collect_reward(ledger, area, party)) {
                    return GAME_ERROR;
                }
                if (room->id == ROOM_BOSS) {
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
        int party_choice = ++choice_count;
        printf("  %d. 查看人物属性\n", party_choice);
        int bestiary_choice = ++choice_count;
        printf("  %d. 怪物图鉴\n", bestiary_choice);
        puts("  0. 退出探险");
        int choice = read_choice("探索操作 > ", 0, choice_count, -1);
        if (choice <= 0) {
            return GAME_QUIT;
        }
        putchar('\n');
        if (chest != NULL && choice == chest_choice) {
            if (!collect_reward(ledger, chest, party)) {
                return GAME_ERROR;
            }
            state->chest_opened = true;
        } else if (choice == party_choice) {
            putchar('\n');
            show_party_stats(party);
        } else if (choice == bestiary_choice) {
            putchar('\n');
            show_bestiary(bestiary);
        } else {
            room = exits->rooms[choice - 1];
        }
    }
}

static void show_summary(LootLedger *ledger, const Party *party, GameResult result)
{
    puts(result == GAME_VICTORY ? "\n[结果] 通关" : "\n[结果] 全员倒地");
    puts("[战利品结算]");
    loot_sort_for_summary(ledger);
    for (size_t index = 0; index < ledger->count; ++index) {
        print_loot_entry("结算", &ledger->entries[index], party);
    }
    printf("[总价值] %d\n", ledger->total);
    if (ledger->mode != LOOT_SHARED) {
        for (size_t index = 0; index < PARTY_SIZE; ++index) {
            printf("[个人收益] %s %d\n", party->members[index].name, ledger->member_totals[index]);
        }
    }
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

    printf("DND 地下城探险游戏\n[随机种子] %" PRIu32 "\n", seed);
    puts("战利品分配方式\n  1. 队伍共有（默认）\n  2. 单场均衡\n  3. 累计均衡");
    int mode = read_choice("选择分配方式 > ", LOOT_SHARED, LOOT_BALANCED_RUN, LOOT_SHARED);
    if (mode < 0) {
        return EXIT_SUCCESS;
    }

    Party party;
    if (!party_init(&party)) {
        fputs("角色属性超出 0-99 的范围。\n", stderr);
        return EXIT_FAILURE;
    }
    const Dungeon *map = dungeon_map();
    if (!dungeon_validate(map)) {
        fputs("地下城里的怪物属性超出 0-99 的范围。\n", stderr);
        return EXIT_FAILURE;
    }
    LootLedger ledger;
    if (!loot_init(&ledger, (LootMode)mode, dungeon_loot_capacity(map))) {
        fputs("无法分配战利品记录所需的内存。\n", stderr);
        return EXIT_FAILURE;
    }

    Bestiary bestiary = {0};
    GameResult result = explore(map, &party, &ledger, &seed, &bestiary);
    if (result == GAME_VICTORY || result == GAME_DEFEAT) {
        show_summary(&ledger, &party, result);
    } else if (result == GAME_QUIT) {
        puts("\n[结果] 已退出探险");
    }
    loot_destroy(&ledger);
    return result == GAME_ERROR ? EXIT_FAILURE : EXIT_SUCCESS;
}
