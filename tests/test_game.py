import re
import subprocess
import sys
import time
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

# 战斗输入按目标编号组织。两次开战之间为探索操作。
GUARD_TARGETS = "1 1 1 1 2 2".split()
HALL_TARGETS = "1 1 1 1 1 1 2 2 2 2 2 2".split()
WINNING_BOSS_TARGETS = "1 1 1 1 1 1 1 1 1 1 2 2".split()
LOSING_BOSS_TARGETS = "1 1 1 1 1 1 1 1 1 2 2".split()
START_AND_CLEAR_GUARD = ["1", "1"] + GUARD_TARGETS
# 守卫室 → 储藏室（开宝箱）→ 守卫室 → 两层楼梯间 → 前厅 → 首领室。
FROM_GUARD_TO_BOSS = "2 2 1 3 2 2".split() + HALL_TARGETS + ["2"]


class GameTests(unittest.TestCase):
    binary: Path

    def setUp(self):
        temp = TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.work = Path(temp.name)
        self.save = self.work / "dnd_save.txt"

    def run_game(self, choices, seed=0, error_pattern=None):
        result = subprocess.run(
            [str(self.binary), "--seed", str(seed)],
            input="".join(f"{choice}\n" for choice in choices),
            capture_output=True,
            encoding="utf-8",
            cwd=self.work,
            timeout=10,
        )
        if error_pattern is None:
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertEqual(result.stderr, "")
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertRegex(result.stderr, error_pattern)
        return result.stdout

    def interrupt_at_exploration_prompt(self, choices, marker):
        log_path = self.work / "game.log"
        with log_path.open("wb") as log, subprocess.Popen(
            [str(self.binary), "--seed", "0"],
            stdin=subprocess.PIPE,
            stdout=log,
            stderr=subprocess.STDOUT,
            cwd=self.work,
            encoding="utf-8",
        ) as process:
            try:
                process.stdin.write("".join(f"{choice}\n" for choice in choices))
                process.stdin.flush()
                deadline = time.monotonic() + 10
                while True:
                    output = log_path.read_text(encoding="utf-8", errors="replace")
                    if marker in output and output.endswith("探索操作 > "):
                        break
                    if process.poll() is not None or time.monotonic() >= deadline:
                        self.fail(f"未到达探索菜单 {marker!r}\n{output}")
                    time.sleep(0.01)
            finally:
                process.kill()
                process.wait(timeout=10)

    def summary_items(self, output):
        return {
            name: int(value)
            for name, value in re.findall(
                r"^\[结算\] (.+)（价值 (\d+)）$", output, re.MULTILINE
            )
        }

    def test_title_screen_and_shared_victory(self):
        output = self.run_game(START_AND_CLEAR_GUARD + FROM_GUARD_TO_BOSS + WINNING_BOSS_TARGETS)
        self.assertIn("[结果] 通关", output)
        self.assertEqual(output.count("[地点] 第 1 层 · 守卫室"), 2)
        self.assertEqual(output.count("[地点] 第 1 层 · 储藏室"), 2)
        self.assertEqual(output.count("[地点] 第 1 层 · 楼梯间"), 1)
        self.assertEqual(output.count("[地点] 第 2 层 · 楼梯间"), 1)
        self.assertEqual(output.count("2. 打开宝箱"), 1)
        self.assertEqual(output.count("[开战]"), 3)

        self.assertIn(
            "[先攻] 游侠(速度 12) 法师(速度 9) 洞穴蝠(速度 9) "
            "战士(速度 6) 哥布林守卫(速度 4)",
            output,
        )
        self.assertRegex(output, r"法师对哥布林守卫造成了0点伤害（斩击|打击）")

        items = self.summary_items(output)
        self.assertEqual(output.count("[结算]"), 10)
        self.assertEqual(list(items.values()), [40, 27, 23, 18, 16, 14, 12, 9, 8, 4])
        self.assertIn("[总价值] 171", output)
        self.assertNotIn("[个人收益]", output)
        self.assertFalse(self.save.exists())

    def test_defeat_keeps_prior_rewards(self):
        output = self.run_game(START_AND_CLEAR_GUARD + FROM_GUARD_TO_BOSS + LOSING_BOSS_TARGETS)
        self.assertIn("[结果] 全员倒地", output)
        self.assertIn("[结算] 秘银碎片（价值 18）", output)
        self.assertIn("[总价值] 96", output)
        self.assertNotIn("[结算] 领主印戒", output)
        self.assertFalse(self.save.exists())

    def test_title_screen_layout(self):
        output = self.run_game(["0"])
        self.assertIn("DND 地下城探险", output)
        self.assertIn("1. 开始", output)
        self.assertIn("2. 继续", output)
        self.assertIn("3. 人物属性", output)
        self.assertIn("4. 怪物图鉴", output)
        self.assertIn("当前层数：0   最远层数：0", output)
        self.assertIn("没有可以继续的存档。", self.run_game(["2", "0"]))

    def test_party_stats_and_bestiary(self):
        stats = self.run_game(["3", "4", "0"])
        self.assertIn("[人物属性]", stats)
        self.assertIn("战士｜普通攻击 打击", stats)
        self.assertIn("法师｜普通攻击 斩击", stats)
        self.assertIn("力量 9  速度 6  魔力 2  幸运 30  声望 10", stats)
        self.assertIn("[怪物图鉴] 还没有遇到过任何怪物。", stats)

        # 打完守卫室后，从「设置」进入图鉴，只记录这一场遇到过的两种怪物。
        bestiary = self.run_game(START_AND_CLEAR_GUARD + ["4", "2", "3", "0"])
        self.assertIn("[怪物图鉴] 已记录 2 种", bestiary)
        self.assertIn("哥布林守卫｜普通攻击 打击", bestiary)
        self.assertIn("洞穴蝠｜普通攻击 斩击", bestiary)
        self.assertNotIn("地牢领主", bestiary)

    def test_quit_then_continue_restores_progress(self):
        first = self.run_game(["1", "2", "3", "0"])
        self.assertIn("[结果] 已退出探险", first)
        self.assertTrue(self.save.exists())

        resumed = self.run_game(["2", "2", "3", "0"])
        self.assertIn("当前层数：1   最远层数：1", resumed)
        self.assertIn("[地点] 第 1 层 · 入口", resumed)
        self.assertIn("小队生命  战士 28  游侠 23  法师 20", resumed)

    def test_attack_target_invalid_input_retries(self):
        output = self.run_game(["1", "1", "99", "x" * 300, "2", "2"])
        self.assertIn("[开战] 守卫哨岗", output)
        self.assertNotIn("行动方式 >", output)
        self.assertIn("请输入有效编号。", output)
        self.assertIn("输入过长，请重新输入编号。", output)
        self.assertIn("该目标已经倒地，请重新选择。", output)
        self.assertIn("游侠对洞穴蝠造成了7点伤害（斩击），洞穴蝠当前剩余血量：0。", output)
        self.assertIn("[结果] 已退出探险", output)

    def test_eof_during_battle_restores_prebattle_checkpoint(self):
        output = self.run_game(["1", "1", "2", "1", "1"])
        self.assertIn("[开战] 守卫哨岗", output)
        self.assertIn("洞穴蝠当前剩余血量：0", output)
        self.assertIn("法师当前剩余血量：18", output)
        self.assertIn("[结果] 已退出探险", output)
        self.assertNotIn("[战利品结算]", output)

        resumed = self.run_game(["2"])
        self.assertIn("[开战] 守卫哨岗", resumed)
        self.assertIn("小队生命  战士 28  游侠 23  法师 20", resumed)
        self.assertIn("洞穴蝠（生命 7）", resumed)
        self.assertIn("哥布林守卫（生命 14）", resumed)

    def test_continue_restores_random_sequence(self):
        # 种子 136 在守卫战消耗两次法师掷骰，下一次触发死亡伤害。
        self.run_game(START_AND_CLEAR_GUARD + ["4", "3", "0"], seed=136)

        resumed = self.run_game(["2", "3", "2", "2", "1", "1"], seed=0)
        self.assertIn("法师对骷髅哨兵造成了99点伤害（死亡）", resumed)

    def test_battle_victory_is_saved_before_next_input(self):
        self.interrupt_at_exploration_prompt(START_AND_CLEAR_GUARD, "[清理] 守卫室")

        resumed = self.run_game(["2"] + FROM_GUARD_TO_BOSS + WINNING_BOSS_TARGETS)
        self.assertIn("[地点] 第 1 层 · 守卫室", resumed)
        self.assertNotIn("[开战] 守卫哨岗", resumed)
        self.assertIn("[结果] 通关", resumed)
        self.assertIn("[结算] 秘银碎片（价值 18）", resumed)
        self.assertIn("[总价值] 171", resumed)

    def test_downstairs_progress_is_saved_before_next_input(self):
        self.interrupt_at_exploration_prompt(
            START_AND_CLEAR_GUARD + ["3", "2"], "[地点] 第 2 层 · 楼梯间"
        )

        resumed = self.run_game(["2"])
        title, _, _ = resumed.partition("[地点]")
        self.assertIn("当前层数：2   最远层数：2", title)
        self.assertIn("[地点] 第 2 层 · 楼梯间", resumed)

    def test_unwritable_save_reports_error(self):
        self.save.mkdir()
        error = r"(保存|存档).*dnd_save\.txt|dnd_save\.txt.*(保存|存档)"
        self.run_game(["1"], error_pattern=error)
        self.assertTrue(self.save.is_dir())

    def test_legacy_crlf_save_restores_progress(self):
        content = (
            "DNDSAVE 1\n"
            "progress 4 1 1\n"
            "member 0 28 10\nmember 1 23 10\nmember 2 16 20\n"
            "room 0 0 0\nroom 1 1 0\nroom 2 0 0\nroom 3 0 0\n"
            "room 4 0 0\nroom 5 0 0\nroom 6 0 0\n"
            "loot 秘银碎片 18\nloot 守卫徽章 9\nloot 旧铜币 4\n"
            "monster 哥布林守卫\nmonster 洞穴蝠\n"
        )
        self.save.write_bytes(content.replace("\n", "\r\n").encode("utf-8"))

        resumed = self.run_game(["2"])
        title, _, _ = resumed.partition("[地点]")
        self.assertIn("当前层数：2   最远层数：2", title)
        self.assertIn("[地点] 第 2 层 · 楼梯间", resumed)
        self.assertIn("小队生命  战士 28  游侠 23  法师 16", resumed)

    def test_current_crlf_save_can_be_resumed(self):
        self.run_game(["1"])
        lines = self.save.read_bytes().splitlines()
        self.save.write_bytes(b"\r\n".join(lines) + b"\r\n")

        resumed = self.run_game(["2"])
        self.assertIn("[地点] 第 1 层 · 入口", resumed)

    def test_truncated_save_cannot_be_resumed(self):
        self.run_game(["1"])
        lines = self.save.read_bytes().splitlines(keepends=True)
        self.save.write_bytes(b"".join(lines[:-1]))

        resumed = self.run_game(["2", "0"])
        self.assertIn("无法读取存档 dnd_save.txt。", resumed)
        self.assertNotIn("[地点]", resumed)

    def test_trailing_dos_eof_cannot_hide_save_data(self):
        self.run_game(["1"])
        with self.save.open("ab") as save:
            save.write(b"\x1aextra data\n")

        resumed = self.run_game(["2", "0"])
        self.assertIn("无法读取存档 dnd_save.txt。", resumed)
        self.assertNotIn("[地点]", resumed)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("用法：python3 tests/test_game.py <dnd 可执行文件>")
    GameTests.binary = Path(sys.argv.pop(1)).resolve()
    unittest.main()
