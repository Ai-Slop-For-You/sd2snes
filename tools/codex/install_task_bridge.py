#!/usr/bin/env python3
"""Expose this repository's workflow from an ancestor task cwd, without overwrites."""
import argparse
import os
from pathlib import Path


def install(repo: Path, task: Path) -> list[Path]:
    repo, task = repo.resolve(), task.resolve()
    if task == repo or task not in repo.parents:
        raise ValueError("task directory must be an ancestor of the repository")
    links = {
        task / "AGENTS.md": repo / "tools/codex/task-AGENTS.md",
        task / ".agents/skills/full-cycle": repo / ".agents/skills/full-cycle",
        task / ".codex/config.toml": repo / ".codex/config.toml",
        task / ".codex/roles": repo / ".codex/roles",
    }
    # Preflight every path before creating anything. Never replace user guidance.
    for dest, source in links.items():
        if not source.exists():
            raise ValueError(f"missing source: {source}")
        if dest.is_symlink() or dest.exists():
            if not dest.is_symlink() or dest.resolve() != source.resolve():
                raise ValueError(f"preserving existing path; manual integration needed: {dest}")
        for parent in dest.parents:
            if parent == task:
                break
            if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
                raise ValueError(f"unsafe destination parent: {parent}")
    for dest, source in links.items():
        if not dest.is_symlink():
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.symlink_to(os.path.relpath(source, dest.parent),
                            target_is_directory=source.is_dir())
    return list(links)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("task_directory", type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    # The checked-in entry point intentionally targets the existing task layout.
    if (args.task_directory.resolve() / "work/sd2snes").resolve() != repo:
        parser.error("expected repository at TASK_DIRECTORY/work/sd2snes")
    try:
        for path in install(repo, args.task_directory):
            print(path)
    except ValueError as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
