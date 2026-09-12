#!/usr/bin/env python3
"""Validate workflow config and discovery with the installed Codex, without inference.

Uses a disposable trusted fixture and CODEX_HOME, never modifies global settings.
Requires Python 3.11+, Git, and Codex with app-server/strict-config support.
"""
import json
import os
import selectors
import shutil
import subprocess
import tempfile
import tomllib
from pathlib import Path

from install_task_bridge import install


def environment(home):
    env = dict(os.environ)
    for key in list(env):
        if key.startswith(("CODEX_", "OPENAI_")):
            del env[key]
    env["CODEX_HOME"] = str(home)
    return env


def rpc(cwd, home, calls):
    with tempfile.TemporaryFile(mode="w+") as errors:
        proc = subprocess.Popen(["codex", "app-server", "--strict-config"],
                                cwd=cwd, env=environment(home), text=True,
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors)
        selector = selectors.DefaultSelector()
        selector.register(proc.stdout, selectors.EVENT_READ)
        results = []
        try:
            init = ("initialize", {"clientInfo": {"name": "workflow_validation", "version": "1"},
                                   "capabilities": {"experimentalApi": True}})
            for ident, (method, params) in enumerate([init, *calls]):
                proc.stdin.write(json.dumps({"id": ident, "method": method, "params": params}) + "\n")
                proc.stdin.flush()
                while True:
                    if not selector.select(20):
                        raise RuntimeError(f"timeout: {method}")
                    line = proc.stdout.readline()
                    if not line:
                        errors.seek(0)
                        raise RuntimeError(errors.read())
                    response = json.loads(line)
                    if response.get("id") == ident:
                        if "error" in response:
                            raise RuntimeError(str(response["error"]))
                        results.append(response["result"])
                        break
            return results[1:]
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            selector.close()
            proc.stdin.close()
            proc.stdout.close()


def main():
    source = Path(__file__).resolve().parents[2]
    report = {"codex": subprocess.check_output(["codex", "--version"], text=True).strip()}
    with tempfile.TemporaryDirectory(prefix="codex-workflow-") as directory:
        task = Path(directory) / "task"
        repo = task / "work/sd2snes"
        repo.mkdir(parents=True)
        home = Path(directory) / "home"
        home.mkdir()
        subprocess.run(["git", "init", "-q", str(repo)], check=True)
        for relative in [".codex", ".agents/skills/full-cycle", "tools/codex"]:
            shutil.copytree(source / relative, repo / relative, ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copy2(source / "AGENTS.md", repo / "AGENTS.md")
        (home / "config.toml").write_text("\n".join(
            f"[projects.{json.dumps(str(path))}]\ntrust_level = \"trusted\"" for path in [task, repo]))
        install(repo, task)
        registry = tomllib.loads((repo / ".codex/config.toml").read_text())
        expected_roles = {"investigator", "planner", "reviewer", "tester", "implementer", "debugger"}
        report["discovery"] = []
        for cwd in [repo, task]:
            cfg, skills = rpc(cwd, home, [
                ("config/read", {"cwd": str(cwd), "includeLayers": True}),
                ("skills/list", {"cwds": [str(cwd)], "forceReload": True}),
            ])
            effective = cfg["config"]
            assert effective["model"] == "gpt-5.6-terra"
            assert effective["model_reasoning_effort"] == "medium"
            assert effective["agents"]["max_concurrent_threads_per_session"] == 2
            for name in expected_roles:
                role = effective["agents"][name]
                assert Path(role["config_file"]).resolve() == (repo / ".codex/roles" / (name + ".toml")).resolve()
            entry = skills["data"][0]
            assert not entry["errors"], entry["errors"]
            found = [s for s in entry["skills"] if s["name"] == "full-cycle"]
            assert len(found) == 1 and found[0]["enabled"], found
            prompt = subprocess.run(["codex", "debug", "prompt-input", "Discovery probe only."],
                                    cwd=cwd, env=environment(home), text=True, capture_output=True,
                                    timeout=30, check=True).stdout
            assert "full-cycle" in prompt
            assert ("FXPAK OS engineering workflow" if cwd == repo else "Existing sd2snes task entry point") in prompt
            report["discovery"].append({"cwd": "repository" if cwd == repo else "parent task",
                                        "roles": sorted(expected_roles), "skill": "full-cycle", "instructions": "loaded"})
        # Each registered role is a normal config layer. Validate its exact bytes
        # independently using the native strict loader (including a negative control).
        role_root = Path(directory) / "role-fixture"
        (role_root / ".codex").mkdir(parents=True)
        subprocess.run(["git", "init", "-q", str(role_root)], check=True)
        with (home / "config.toml").open("a") as file:
            file.write(f"\n[projects.{json.dumps(str(role_root))}]\ntrust_level = \"trusted\"\n")
        report["role_layers"] = {}
        for name in sorted(expected_roles):
            data = (repo / ".codex" / registry["agents"][name]["config_file"]).read_text()
            expected = tomllib.loads(data)
            (role_root / ".codex/config.toml").write_text(data)
            actual = rpc(role_root, home, [("config/read", {"cwd": str(role_root)})])[0]["config"]
            for key, value in expected.items():
                assert actual[key] == value, (name, key, actual[key], value)
            report["role_layers"][name] = {k: actual[k] for k in ["model", "model_reasoning_effort", "sandbox_mode"]}
        (role_root / ".codex/config.toml").write_text('unknown_workflow_setting = true\n')
        try:
            rpc(role_root, home, [("config/read", {"cwd": str(role_root)})])
        except (RuntimeError, BrokenPipeError) as error:
            assert "unknown_workflow_setting" in str(error), str(error)
            report["negative_control"] = "unknown config key rejected"
        else:
            raise AssertionError("strict-config did not reject unknown key")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
