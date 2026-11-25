import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "build" / "_tests"
SAVE = WORK / "dnd_save.txt"

# 一次完整通关的探索与战斗输入（不含开头的「开始」）。
# 路线：入口 → 守卫室 → 储藏室（开宝箱）→ 守卫室 → 楼梯间 →
# 第 2 层楼梯间 → 前厅 → 首领室。
ROUTE = (
    "1 1 1 1 1 1 1 1 1 1 2 1 2 2 2 1 3"
    " 2 2 1 1 1 1 1 1 1 1 1 1 1 1 1 2 1"
    " 2 1 2 1 2 1 2 1 2 2 1 1 1 1 1 1 1"
    " 1 1 1 1 1 1 1 1 1 1 1 1 1 1 2 1 2"
).split()


class GameTests(unittest.TestCase):
    binary: Path

    def setUp(self):
        WORK.mkdir(parents=True, exist_ok=True)

    def run_game(self, choices, seed=0, keep_save=False):
        if not keep_save and SAVE.exists():
            SAVE.unlink()
        result = subprocess.run(
            [str(self.binary), "--seed", str(seed)],
            input="".join(f"{choice}\n" for choice in choices),
            capture_output=True,
            encoding="utf-8",
            cwd=WORK,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(result.stderr, "")
        return result.stdout

    def summary_items(self, output):
        return {
            name: int(value)
            for name, value in re.findall(
                r"^\[结算\] (.+)（价值 (\d+)）$", output, re.MULTILINE
            )
        }

    def test_title_screen_and_shared_victory(self):
        output = self.run_game(["1"] + ROUTE)
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

        # 战利品一律进入小队仓库，不再有个人分配。
        items = self.summary_items(output)
        self.assertEqual(output.count("[结算]"), 10)
        self.assertEqual(list(items.values()), [40, 27, 23, 18, 16, 14, 12, 9, 8, 4])
        self.assertIn("[总价值] 171", output)
        self.assertNotIn("[个人收益]", output)

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
        bestiary = self.run_game(["1"] + ROUTE[:13] + ["4", "2", "3", "0"])
        self.assertIn("[怪物图鉴] 已记录 2 种", bestiary)
        self.assertIn("哥布林守卫｜普通攻击 打击", bestiary)
        self.assertIn("洞穴蝠｜普通攻击 斩击", bestiary)
        self.assertNotIn("地牢领主", bestiary)

    def test_quit_then_continue_restores_progress(self):
        first = self.run_game(["1", "2", "3", "0"])
        self.assertIn("[结果] 已退出探险", first)
        self.assertTrue(SAVE.exists())

        resumed = self.run_game(["2", "2", "3", "0"], keep_save=True)
        self.assertIn("当前层数：1   最远层数：1", resumed)
        self.assertIn("[地点] 第 1 层 · 入口", resumed)
        self.assertIn("小队生命  战士 28  游侠 23  法师 20", resumed)

    def test_action_menu_and_invalid_input_retries(self):
        output = self.run_game(["1", "1", "2", "1", "99", "x" * 300, "2"])
        self.assertIn("[开战] 守卫哨岗", output)
        self.assertIn("暂时没有可用的技能。", output)
        self.assertIn("请输入有效编号。", output)
        self.assertIn("输入过长，请重新输入编号。", output)
        self.assertIn("游侠对洞穴蝠造成了7点伤害（斩击），洞穴蝠当前剩余血量：0。", output)
        self.assertIn("[结果] 已退出探险", output)

    def test_eof_during_battle_exits(self):
        output = self.run_game(["1", "1"])
        self.assertIn("[开战] 守卫哨岗", output)
        self.assertIn("[结果] 已退出探险", output)
        self.assertNotIn("[战利品结算]", output)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("用法：python3 tests/test_game.py <dnd 可执行文件>")
    GameTests.binary = Path(sys.argv.pop(1)).resolve()
    unittest.main()
