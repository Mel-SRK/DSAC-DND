import re
import subprocess
import sys
import unittest
from pathlib import Path


ENTER_AND_CLEAR_GUARD = [1, 2, 1, 1]
OPEN_AND_REVISIT_STORAGE = [2, 2, 1, 2, 1]
USE_STAIRS_BOTH_WAYS_AND_ENTER_HALL = [3, 2, 1, 2, 2]
CLEAR_HALL_AND_ENTER_BOSS = [2, 2, 1, 1, 2]
ROUTE_TO_BOSS = (
    ENTER_AND_CLEAR_GUARD
    + OPEN_AND_REVISIT_STORAGE
    + USE_STAIRS_BOTH_WAYS_AND_ENTER_HALL
    + CLEAR_HALL_AND_ENTER_BOSS
)
BOSS_VICTORY_ATTACKS = [2, 2, 1, 1, 1, 1, 1]
BOSS_DEFEAT_ATTACKS = [2, 2, 1, 1, 1, 1]


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

    def test_default_shared_victory_and_room_revisits(self):
        output = self.run_game([""] + ROUTE_TO_BOSS + BOSS_VICTORY_ATTACKS)
        self.assertIn("[结果] 通关", output)
        self.assertEqual(output.count("[地点] 第 1 层 · 守卫室"), 3)
        self.assertEqual(output.count("[地点] 第 2 层 · 楼梯间"), 2)
        self.assertEqual(output.count("[开战] 守卫哨岗"), 1)
        self.assertEqual(output.count("2. 打开宝箱"), 1)

        items = self.summary_items(output)
        self.assertEqual(output.count("[结算]"), 10)
        self.assertEqual([value for value, _ in items.values()],
                         [40, 27, 23, 18, 16, 14, 12, 9, 8, 4])
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
                output = self.run_game([mode] + ROUTE_TO_BOSS + BOSS_VICTORY_ATTACKS)
                self.assertIn("[结果] 通关", output)
                items = self.summary_items(output)
                self.assertEqual(len(items), 10)
                self.assertEqual(items["蓝晶石"], (23, gem_owner))
                self.assertEqual(items["领主印戒"], (40, ring_owner))
                self.assertEqual(items["旧铜币"], (4, "法师"))
                for member, total in zip(("战士", "游侠", "法师"), totals):
                    distributed = sum(value for value, owner in items.values()
                                      if owner == member)
                    self.assertEqual(distributed, total)
                    self.assertIn(f"[个人收益] {member} {total}", output)
                self.assertIn("[总价值] 171", output)

    def test_initiative_and_invalid_target_retries(self):
        output = self.run_game([1, 1, 99, "x" * 300, 2, 2, 1, 1, 0])
        self.assertIn("[先攻] 游侠(20) 洞穴蝠(15) 哥布林守卫(12) 战士(12) 法师(11)", output)
        self.assertIn("请输入有效编号。", output)
        self.assertIn("输入过长，请重新输入编号。", output)
        self.assertIn("该目标已经倒地，请重新选择。", output)
        self.assertEqual(
            re.findall(r"^\[攻击\] (.+)$", output, re.MULTILINE),
            [
                "游侠 → 洞穴蝠，伤害 8，剩余生命 0",
                "哥布林守卫 → 法师，伤害 4，剩余生命 16",
                "战士 → 哥布林守卫，伤害 9，剩余生命 5",
                "法师 → 哥布林守卫，伤害 10，剩余生命 0",
            ],
        )
        self.assertIn("[结果] 已退出探险", output)

    def test_defeat_keeps_prior_rewards(self):
        output = self.run_game([1] + ROUTE_TO_BOSS + BOSS_DEFEAT_ATTACKS, seed=12)
        self.assertIn("[攻击] 地牢领主 → 战士，伤害 9，剩余生命 0", output)
        self.assertIn("[结果] 全员倒地", output)
        self.assertIn("[总价值] 96", output)
        self.assertEqual(len(self.summary_items(output)), 7)
        self.assertNotIn("领主印戒", output)

    def test_eof_during_battle_exits(self):
        output = self.run_game([1, 1])
        self.assertIn("[开战] 守卫哨岗", output)
        self.assertIn("[结果] 已退出探险", output)
        self.assertNotIn("[战利品结算]", output)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("用法：python3 tests/test_game.py <dnd 可执行文件>")
    GameTests.binary = Path(sys.argv.pop(1)).resolve()
    unittest.main()
