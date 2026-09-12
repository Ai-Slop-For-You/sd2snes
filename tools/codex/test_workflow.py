#!/usr/bin/env python3
"""Fast offline preservation tests; no firmware build or user-repository mutation."""
import subprocess
import tempfile
import unittest
from pathlib import Path

from install_task_bridge import install


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], stderr=subprocess.STDOUT)


class WorkflowTests(unittest.TestCase):
    def fixture(self, directory):
        task = Path(directory)
        repo = task / "work/sd2snes"
        for path, content in {
            "tools/codex/task-AGENTS.md": "bridge",
            ".agents/skills/full-cycle/SKILL.md": "skill",
            ".codex/config.toml": 'model = "gpt-5.6-terra"',
            ".codex/roles/reviewer.toml": 'sandbox_mode = "read-only"',
        }.items():
            target = repo / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        return task, repo

    def test_bridge_idempotent_and_relative(self):
        with tempfile.TemporaryDirectory() as directory:
            task, repo = self.fixture(directory)
            paths = install(repo, task)
            self.assertEqual(paths, install(repo, task))
            for path in paths:
                self.assertTrue(path.is_symlink())
                self.assertTrue(path.exists())
                self.assertFalse(path.readlink().is_absolute())

    def test_bridge_collision_is_preserved_before_any_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            task, repo = self.fixture(directory)
            (task / ".codex").mkdir()
            existing = task / ".codex/config.toml"
            existing.write_text("user settings")
            with self.assertRaisesRegex(ValueError, "preserving existing"):
                install(repo, task)
            self.assertEqual(existing.read_text(), "user settings")
            self.assertFalse((task / "AGENTS.md").exists())

    def test_bridge_rejects_symlink_parent_and_wrong_root(self):
        with tempfile.TemporaryDirectory() as directory:
            task, repo = self.fixture(directory)
            (task / "elsewhere").mkdir()
            (task / ".agents").symlink_to(task / "elsewhere", target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "unsafe destination parent"):
                install(repo, task)
            with self.assertRaisesRegex(ValueError, "ancestor"):
                install(repo, repo)

    def test_only_commit_preserves_other_staged_and_unstaged_content(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            git(repo, "init", "-q")
            git(repo, "config", "user.name", "Workflow test")
            git(repo, "config", "user.email", "workflow@example.invalid")
            git(repo, "config", "commit.gpgsign", "false")
            user = repo / "unrelated.txt"
            user.write_text("base\n")
            owned = repo / "owned.txt"
            owned.write_text("before\n")
            git(repo, "add", "--", "unrelated.txt", "owned.txt")
            git(repo, "commit", "-qm", "fixture")
            user.write_text("staged user change\n")
            git(repo, "add", "--", "unrelated.txt")
            user.write_text("staged user change\nunstaged user change\n")
            staged = git(repo, "diff", "--cached", "--binary", "--", "unrelated.txt")
            unstaged = git(repo, "diff", "--binary", "--", "unrelated.txt")
            owned.write_text("after\n")
            (repo / "new.txt").write_text("new owned file\n")
            git(repo, "add", "--", "new.txt")
            git(repo, "commit", "--only", "-qm", "candidate", "--", "owned.txt", "new.txt")
            self.assertEqual(git(repo, "diff", "--cached", "--binary", "--", "unrelated.txt"), staged)
            self.assertEqual(git(repo, "diff", "--binary", "--", "unrelated.txt"), unstaged)
            self.assertEqual(git(repo, "show", "HEAD:unrelated.txt"), b"base\n")
            self.assertEqual(set(git(repo, "diff-tree", "--no-commit-id", "--name-only", "-r", "HEAD").splitlines()),
                             {b"owned.txt", b"new.txt"})


if __name__ == "__main__":
    unittest.main()
