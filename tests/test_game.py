import re
import subprocess
import sys
import unittest
from pathlib import Path


# 一次完整通关的探索与战斗输入。第一项由 run_game 的 choices 提供分配方式，
# 这里从「入口 → 守卫室 → 储藏室（开宝箱）→ 守卫室 → 楼梯间 →
# 第 2 层楼梯间 → 前厅 → 首领室」逐项列出。
ROUTE = (
    "1 1 1 1 1 1 1 1 1 1 2 1 2 2 2 1 3"
    " 2 2 1 1 1 1 1 1 1 1 1 1 1 1 1 2 1"
    " 2 1 2 1 2 1 2 1 2 2 1 1 1 1 1 1 1"
    " 1 1 1 1 1 1 1 1 1 1 1 1 1 1 2 1 2"
).split()


class GameTests(unittest.TestCase):
    binary: Path

    def run_game(self, choices, seed=0):
        result = subprocess.run(
            [str(self.binary), "--seed", str(seed)],
            input="".join(f"{choice}\n" for choice in choices),
            capture_output=True,
            encoding="utf-8",
            timeout=5,
        )
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(result.stderr, "")
        return result.stdout

    def summary_items(self, output):
        return {
            name: (int(value), owner)
            for name, value, owner in re.findall(
                r"^\[结算\] (.+) \| 价值 (\d+) \| (.+)$", output, re.MULTILINE
            )
        }

    def test_shared_victory_and_loot_summary(self):
        output = self.run_game(["1"] + ROUTE)
        self.assertIn("[结果] 通关", output)
        self.assertEqual(output.count("[地点] 第 1 层 · 守卫室"), 2)
        self.assertEqual(output.count("[地点] 第 1 层 · 储藏室"), 2)
        self.assertEqual(output.count("[地点] 第 1 层 · 楼梯间"), 1)
        self.assertEqual(output.count("[地点] 第 2 层 · 楼梯间"), 1)
        self.assertEqual(output.count("2. 打开宝箱"), 1)
        self.assertEqual(output.count("[开战]"), 3)

        # 先攻按速度降序，不再掷 d20。
        self.assertIn(
            "[先攻] 游侠(速度 12) 法师(速度 9) 洞穴蝠(速度 9) "
            "战士(速度 6) 哥布林守卫(速度 4)",
            output,
        )
        # 法师平砍 1 点，被哥布林守卫的 2 点防御完全吃掉。
        self.assertRegex(output, r"\[攻击\] 法师 → 哥布林守卫（斩击|打击），伤害 0")

        items = self.summary_items(output)
        self.assertEqual(output.count("[结算]"), 10)
        self.assertEqual(
            [value for value, _ in items.values()],
            [40, 27, 23, 18, 16, 14, 12, 9, 8, 4],
        )
        self.assertEqual({owner for _, owner in items.values()}, {"小队仓库"})
        self.assertIn("[总价值] 171", output)
        self.assertNotIn("[个人收益]", output)

    def test_balanced_distribution_uses_selected_scope(self):
        expected = {
            2: ("战士", "战士", [97, 62, 12]),
            3: ("法师", "法师", [61, 43, 67]),
        }
        for mode, (gem_owner, ring_owner, totals) in expected.items():
            with self.subTest(mode=mode):
                output = self.run_game([str(mode)] + ROUTE)
                self.assertIn("[结果] 通关", output)
                items = self.summary_items(output)
                self.assertEqual(len(items), 10)
                self.assertEqual(items["蓝晶石"], (23, gem_owner))
                self.assertEqual(items["领主印戒"], (40, ring_owner))
                self.assertEqual(items["旧铜币"], (4, "法师"))
                for member, total in zip(("战士", "游侠", "法师"), totals):
                    distributed = sum(
                        value for value, owner in items.values() if owner == member
                    )
                    self.assertEqual(distributed, total)
                    self.assertIn(f"[个人收益] {member} {total}", output)
                self.assertIn("[总价值] 171", output)

    def test_action_menu_and_invalid_input_retries(self):
        output = self.run_game(["1", "1", "2", "1", "99", "x" * 300, "2"])
        self.assertIn("[开战] 守卫哨岗", output)
        self.assertIn("暂时没有可用的技能。", output)
        self.assertIn("请输入有效编号。", output)
        self.assertIn("输入过长，请重新输入编号。", output)
        self.assertIn("[攻击] 游侠 → 洞穴蝠（斩击），伤害 7，剩余生命 0", output)
        self.assertIn("[结果] 已退出探险", output)

    def test_party_stats_and_bestiary(self):
        stats = self.run_game(["", "2", "3", "0"])
        self.assertIn("[人物属性]", stats)
        self.assertIn("战士｜普通攻击 打击", stats)
        self.assertIn("法师｜普通攻击 斩击", stats)
        self.assertIn("力量 9  速度 6  魔力 2  幸运 30  声望 10", stats)
        self.assertIn("[怪物图鉴] 还没有遇到过任何怪物。", stats)

        # 打完守卫室后，图鉴只记录这一场遇到过的两种怪物。
        bestiary = self.run_game([""] + ROUTE[:13] + ["5", "0"])
        self.assertIn("[怪物图鉴] 已记录 2 种", bestiary)
        self.assertIn("哥布林守卫｜普通攻击 打击", bestiary)
        self.assertIn("洞穴蝠｜普通攻击 斩击", bestiary)
        self.assertNotIn("地牢领主", bestiary)

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
