#!/usr/bin/env python3
"""
HexEngine dependency bootstrap tool (canonical replacement for setup.py execution paths).

Windows-first scope:
- declarative dependency metadata from build/dependencies.lock.json
- pinned ref workflows (plan/check/lock)
- required dependency bootstrap for modern CMake orchestration
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST_PATH = REPO_ROOT / "build" / "dependencies.lock.json"

LEGACY_CMAKE_GENERATOR = "Visual Studio 17 2022"
LEGACY_CMAKE_ARCH = "x64"

CORE_HEADER_BOOTSTRAP = ("cxxopts", "fastnoiselite", "rapidxml")


@dataclass(frozen=True)
class RuntimeContext:
    repo_root: Path
    manifest_path: Path
    frozen: bool
    update: bool
    msbuild_path: Path
    generator: str
    arch: str

    def libs_dir(self, config: str) -> Path:
        return self.repo_root / "Libs" / "x64" / config

    def bin_dir(self, config: str) -> Path:
        return self.repo_root / "Bin" / "x64" / config / "Bin"


def run(cmd: list[str], cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(cmd))
    subprocess.check_call(cmd, cwd=str(cwd) if cwd else None, env=env)


def mkdir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def copy_file(src: Path, dst: Path) -> None:
    mkdir(dst.parent)
    copy_with_retry(src, dst)


def copy_glob(pattern: str, dst_dir: Path, allow_locked: bool = False) -> None:
    mkdir(dst_dir)
    for item in glob.glob(pattern):
        src = Path(item)
        if src.is_file():
            dst = dst_dir / src.name
            try:
                copy_with_retry(src, dst)
            except PermissionError:
                if allow_locked:
                    print(f"warning: file locked, keeping existing destination: {dst}")
                    continue
                raise


def copy_with_retry(src: Path, dst: Path, attempts: int = 6, delay_s: float = 0.5) -> None:
    last_error: PermissionError | None = None
    for _ in range(attempts):
        try:
            shutil.copy2(src, dst)
            return
        except PermissionError as error:
            last_error = error
            time.sleep(delay_s)
    if last_error:
        raise last_error


def load_manifest() -> dict:
    with MANIFEST_PATH.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def save_manifest(manifest: dict) -> None:
    with MANIFEST_PATH.open("w", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")


def dependencies_by_name(manifest: dict) -> dict[str, dict]:
    return {dep["name"].lower(): dep for dep in manifest.get("dependencies", [])}


def dep_path(dep: dict) -> Path:
    return REPO_ROOT / dep["path"]


def git_env() -> dict[str, str]:
    env = os.environ.copy()
    common_lfs_dir = Path("C:/Program Files/Git LFS")
    if common_lfs_dir.exists():
        env["PATH"] = str(common_lfs_dir) + os.pathsep + env.get("PATH", "")
    env["GIT_LFS_SKIP_SMUDGE"] = "1"
    return env


def run_git(args: list[str]) -> None:
    run(
        [
            "git",
            "-c",
            "filter.lfs.required=false",
            "-c",
            "filter.lfs.smudge=",
            "-c",
            "filter.lfs.process=",
            *args,
        ],
        env=git_env(),
    )


def detect_msbuild_path() -> Path:
    msbuild = shutil.which("msbuild")
    if msbuild:
        return Path(msbuild)
    candidates = glob.glob("C:/Program Files/Microsoft Visual Studio/2022/*/MSBuild/Current/Bin/msbuild.exe")
    if not candidates:
        raise FileNotFoundError("Could not find MSBuild.exe under Visual Studio 2022 installation directories.")
    return Path(candidates[0])


def ensure_repo(dep: dict, frozen: bool, update: bool) -> Path:
    path = dep_path(dep)
    url = dep["git_url"]
    ref = dep.get("ref")

    if not path.exists():
        mkdir(path.parent)
        run_git(["clone", url, str(path)])

    if update:
        run_git(["-C", str(path), "fetch", "--all", "--prune", "--force"])
        run_git(["-C", str(path), "pull", "--ff-only"])

    if frozen and ref:
        try:
            run_git(["-C", str(path), "checkout", ref])
        except subprocess.CalledProcessError:
            run_git(["-C", str(path), "fetch", "--all", "--prune", "--force"])
            run_git(["-C", str(path), "checkout", ref])

    return path


def print_plan(manifest: dict) -> None:
    print("HexEngine dependency plan")
    print(f"Manifest: {MANIFEST_PATH}")
    print("")
    for dep in manifest.get("dependencies", []):
        print(f"- {dep['name']}")
        print(f"  path: {dep['path']}")
        print(f"  url: {dep['git_url']}")
        print(f"  ref: {dep.get('ref')}")
        print(f"  build_system: {dep.get('build_system')}")
        print(f"  required: {dep.get('required', False)}")
        print(f"  required_runtime_module: {dep.get('required_runtime_module', False)}")
    print("")
    print("No build steps were executed.")


def check_refs(manifest: dict, strict: bool) -> int:
    missing = []
    for dep in manifest.get("dependencies", []):
        if dep.get("ref"):
            print(f"[ok] {dep['name']}: {dep['ref']}")
        else:
            print(f"[missing] {dep['name']}: ref is null")
            missing.append(dep["name"])
    if strict and missing:
        print("")
        print("Missing refs:", ", ".join(missing))
        return 1
    return 0


def lock_current_refs(manifest: dict) -> None:
    updated = False
    for dep in manifest.get("dependencies", []):
        path = dep_path(dep)
        if not path.exists():
            continue
        try:
            sha = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        except subprocess.CalledProcessError:
            continue
        if dep.get("ref") != sha:
            dep["ref"] = sha
            updated = True
            print(f"[lock] {dep['name']} -> {sha}")

    if updated:
        save_manifest(manifest)
        print("Manifest refs updated.")
    else:
        print("No ref updates were needed.")


def build_directxtk(ctx: RuntimeContext, dep: dict, config: str) -> None:
    repo = ensure_repo(dep, ctx.frozen, ctx.update)
    win10_vcxproj = repo / "DirectXTK_Desktop_2022_Win10.vcxproj"
    if win10_vcxproj.exists():
        run(
            [
                str(ctx.msbuild_path),
                str(win10_vcxproj),
                f"/p:Configuration={config}",
                "/p:Platform=x64",
                "/m",
            ]
        )
        copy_file(
            repo / "Bin" / "Desktop_2022_Win10" / "x64" / config / "DirectXTK.lib",
            ctx.libs_dir(config) / "DirectXTK.lib",
        )
        return

    build_dir = repo / "build"
    mkdir(build_dir)
    run(["cmake", "-S", "..", "-G", ctx.generator, "-A", ctx.arch], cwd=build_dir)
    run([str(ctx.msbuild_path), str(build_dir / "DirectXTK.vcxproj"), f"/p:Configuration={config}", "/m"])
    copy_file(build_dir / "lib" / config / "DirectXTK.lib", ctx.libs_dir(config) / "DirectXTK.lib")


def build_freetype(ctx: RuntimeContext, dep: dict, config: str) -> None:
    repo = ensure_repo(dep, ctx.frozen, ctx.update)
    build_dir = repo / "build"
    mkdir(build_dir)
    run(["cmake", "-S", "..", "-G", ctx.generator, "-A", ctx.arch], cwd=build_dir)
    run([str(ctx.msbuild_path), str(build_dir / "freetype.sln"), "-t:freetype", f"/p:Configuration={config}", "/m"])
    lib_name = "freetyped.lib" if config == "Debug" else "freetype.lib"
    copy_file(build_dir / config / lib_name, ctx.libs_dir(config) / lib_name)


def build_directxtex(ctx: RuntimeContext, dep: dict, config: str) -> None:
    repo = ensure_repo(dep, ctx.frozen, ctx.update)
    build_dir = repo / "build"
    mkdir(build_dir)
    run(["cmake", "-S", "..", "-G", ctx.generator, "-A", ctx.arch], cwd=build_dir)
    run([str(ctx.msbuild_path), str(build_dir / "DirectXTex.vcxproj"), f"/p:Configuration={config}", "/m"])
    copy_file(build_dir / "lib" / config / "DirectXTex.lib", ctx.libs_dir(config) / "DirectXTex.lib")


def build_brotli(ctx: RuntimeContext, dep: dict, config: str) -> None:
    repo = ensure_repo(dep, ctx.frozen, ctx.update)
    out_dir = repo / "out"
    mkdir(out_dir)
    run(
        [
            "cmake",
            "-S",
            "..",
            "-G",
            ctx.generator,
            "-A",
            ctx.arch,
            f"-DCMAKE_BUILD_TYPE={config}",
            "-DCMAKE_INSTALL_PREFIX=./installed",
        ],
        cwd=out_dir,
    )
    run([str(ctx.msbuild_path), str(out_dir / "brotli.sln"), f"/p:Configuration={config}", "/m"])
    copy_glob(str(out_dir / config / "*.lib"), ctx.libs_dir(config))
    copy_glob(str(out_dir / config / "*.dll"), ctx.bin_dir(config))
    copy_glob(str(out_dir / config / "*.exe"), ctx.bin_dir(config))


def build_physx(ctx: RuntimeContext, dep: dict, config: str) -> None:
    repo = ensure_repo(dep, ctx.frozen, ctx.update)
    candidates = (repo / "physx", repo)
    root = None
    for candidate in candidates:
        if (candidate / "buildtools/presets/public/vc17win64.xml").exists() and (candidate / "generate_projects.bat").exists():
            root = candidate
            break
    if root is None:
        raise RuntimeError("PhysX layout missing expected buildtools/generate_projects files.")

    run(["cmd", "/c", "generate_projects.bat", "vc17win64"], cwd=root)
    sln = root / "compiler" / "vc17win64" / "PhysXSDK.sln"
    targets = [
        r"-t:PhysX SDK\PhysXFoundation:rebuild",
        r"-t:PhysX SDK\SimulationController:rebuild",
        r"-t:PhysX SDK\SceneQuery:rebuild",
        r"-t:PhysX SDK\LowLevel:rebuild",
        r"-t:PhysX SDK\LowLevelAABB:rebuild",
        r"-t:PhysX SDK\LowLevelDynamics:rebuild",
        r"-t:PhysX SDK\PhysXCommon:rebuild",
        r"-t:PhysX SDK\PhysXPvdSDK:rebuild",
        r"-t:PhysX SDK\PhysXTask:rebuild",
        r"-t:PhysX SDK\PhysX:rebuild",
        r"-t:PhysX SDK\PhysXExtensions:rebuild",
        r"-t:PhysX SDK\PhysXCharacterKinematic:rebuild",
        r"-t:PhysX SDK\PhysXCooking:rebuild",
        r"-t:PhysX SDK\PhysXVehicle:rebuild",
        r"-t:PhysX SDK\PhysXVehicle2:rebuild",
    ]
    run([str(ctx.msbuild_path), str(sln), *targets, f"/p:Configuration={config.lower()}", "/m"])

    bin_root = root / "bin" / "win.x86_64.vc143.md" / config.lower()
    copy_glob(str(bin_root / "*.lib"), ctx.libs_dir(config))
    copy_glob(str(bin_root / "*.dll"), ctx.bin_dir(config), allow_locked=True)


def build_shaderconductor(ctx: RuntimeContext, dep: dict, config: str) -> None:
    repo = ensure_repo(dep, ctx.frozen, ctx.update)
    prebuilt_candidates = [
        repo / "Build" / "lib" / config / "ShaderConductor.lib",
        repo / "build" / "lib" / config / "ShaderConductor.lib",
    ]
    for candidate in prebuilt_candidates:
        if candidate.exists():
            copy_file(candidate, ctx.libs_dir(config) / "ShaderConductor.lib")
            return

    build_dir = repo / "build"
    mkdir(build_dir)
    run(
        [
            "cmake",
            "-G",
            ctx.generator,
            "-T",
            "host=x64",
            "-A",
            "x64",
            "../",
            '-DCMAKE_CXX_FLAGS=/wd4189',
            f"-DCMAKE_BUILD_TYPE={config}",
        ],
        cwd=build_dir,
    )
    run(["cmake", "--build", ".", "--config", config], cwd=build_dir)

    for candidate in prebuilt_candidates:
        if candidate.exists():
            copy_file(candidate, ctx.libs_dir(config) / "ShaderConductor.lib")
            return

    raise RuntimeError(f"ShaderConductor.lib not found after bootstrap for {config}.")


def detect_git_lfs() -> str | None:
    lfs = shutil.which("git-lfs")
    if lfs:
        return lfs
    common = Path("C:/Program Files/Git LFS/git-lfs.exe")
    if common.exists():
        return str(common)
    return None


def ensure_streamline(ctx: RuntimeContext, dep: dict) -> None:
    repo = ensure_repo(dep, ctx.frozen, ctx.update)
    lfs = detect_git_lfs()
    if lfs:
        env = git_env()
        env.pop("GIT_LFS_SKIP_SMUDGE", None)
        run([lfs, "pull", "--include=lib/x64/*"], cwd=repo, env=env)

    required_lib = repo / "lib" / "x64" / "sl.interposer.lib"
    required_header = repo / "include" / "sl_helpers.h"
    if not required_lib.exists():
        raise RuntimeError(f"Missing required Streamline library: {required_lib}")
    if not required_header.exists():
        raise RuntimeError(f"Missing required Streamline header: {required_header}")

    with required_lib.open("rb") as stream:
        head = stream.read(200)
    if b"version https://git-lfs.github.com/spec/v1" in head:
        raise RuntimeError(f"Streamline library unresolved git-lfs pointer: {required_lib}")


def ensure_only(ctx: RuntimeContext, dep: dict, _: str) -> None:
    ensure_repo(dep, ctx.frozen, ctx.update)


def required_dep_names(manifest: dict) -> list[str]:
    names = []
    for dep in manifest.get("dependencies", []):
        if dep.get("required", False) or dep.get("required_runtime_module", False):
            names.append(dep["name"].lower())
    return names


def bootstrap_minimal_dep_names(manifest: dict) -> list[str]:
    names = set(required_dep_names(manifest))
    names.update(CORE_HEADER_BOOTSTRAP)
    return [dep["name"].lower() for dep in manifest.get("dependencies", []) if dep["name"].lower() in names]


def create_handlers() -> dict[str, Callable[[RuntimeContext, dict, str], None]]:
    return {
        "directxtk": build_directxtk,
        "freetype": build_freetype,
        "directxtex": build_directxtex,
        "brotli": build_brotli,
        "physx": build_physx,
        "shaderconductor": build_shaderconductor,
        "streamline": lambda ctx, dep, cfg: ensure_streamline(ctx, dep),
        "rapidjson": ensure_only,
        "retpack2d": ensure_only,
        "cxxopts": ensure_only,
        "fastnoiselite": ensure_only,
        "rapidxml": ensure_only,
    }


def bootstrap_dependencies(ctx: RuntimeContext, dep_names: list[str], configs: list[str]) -> None:
    manifest = load_manifest()
    dep_map = dependencies_by_name(manifest)
    handlers = create_handlers()

    for name in dep_names:
        if name not in dep_map:
            raise RuntimeError(f"Dependency '{name}' not found in manifest.")
        if name not in handlers:
            raise RuntimeError(f"No bootstrap handler implemented for '{name}'.")

    print("Bootstrap dependency set:", ", ".join(dep_names))
    print("Configurations:", ", ".join(configs))

    for name in dep_names:
        dep = dep_map[name]
        handler = handlers[name]
        if name in ("streamline", "rapidjson", "retpack2d", "cxxopts", "fastnoiselite", "rapidxml"):
            print(f"Bootstrapping {name} (no per-config build)")
            handler(ctx, dep, "Debug")
            continue
        for config in configs:
            print(f"Bootstrapping {name} ({config})")
            handler(ctx, dep, config)


def parse_configs(raw: str) -> list[str]:
    values = [token.strip() for token in raw.split(",") if token.strip()]
    normalized = []
    for value in values:
        if value.lower() == "debug":
            normalized.append("Debug")
        elif value.lower() == "release":
            normalized.append("Release")
        else:
            raise ValueError(f"Unsupported config '{value}'. Use Debug,Release.")
    if not normalized:
        raise ValueError("No configurations provided.")
    return normalized


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="HexEngine dependency bootstrap tool.")
    parser.add_argument("--frozen", action="store_true", default=True, help="Checkout pinned refs from manifest.")
    parser.add_argument("--update", action="store_true", help="Pull latest changes before optional ref checkout.")
    parser.add_argument("--msbuild-path", default=None, help="Optional explicit MSBuild path.")
    parser.add_argument("--generator", default=LEGACY_CMAKE_GENERATOR, help="CMake generator for dependency builds.")
    parser.add_argument("--arch", default=LEGACY_CMAKE_ARCH, help="CMake architecture for dependency builds.")

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("plan", help="Print manifest dependency plan.")
    subparsers.add_parser("check-refs", help="Check pin status for manifest refs.")
    subparsers.add_parser("check-refs-strict", help="Check refs and fail if any are missing.")
    subparsers.add_parser("lock-current-refs", help="Lock refs to local dependency HEAD commits.")

    required_cmd = subparsers.add_parser("bootstrap-required", help="Bootstrap required dependencies.")
    required_cmd.add_argument("--configs", default="Debug,Release", help="Comma-separated configs (Debug,Release).")

    minimal_cmd = subparsers.add_parser("bootstrap-minimal", help="Bootstrap required + core header dependencies.")
    minimal_cmd.add_argument("--configs", default="Debug,Release", help="Comma-separated configs (Debug,Release).")

    header_cmd = subparsers.add_parser("bootstrap-headeronly", help="Bootstrap core header-only dependencies.")
    header_cmd.add_argument("--configs", default="Debug", help="Ignored (for preset compatibility).")

    return parser


def main() -> int:
    if not MANIFEST_PATH.exists():
        print(f"Dependency manifest not found: {MANIFEST_PATH}", file=sys.stderr)
        return 1

    parser = build_parser()
    args = parser.parse_args()
    manifest = load_manifest()

    if args.command == "plan":
        print_plan(manifest)
        return 0
    if args.command == "check-refs":
        return check_refs(manifest, strict=False)
    if args.command == "check-refs-strict":
        return check_refs(manifest, strict=True)
    if args.command == "lock-current-refs":
        lock_current_refs(manifest)
        return 0

    msbuild_path = Path(args.msbuild_path) if args.msbuild_path else detect_msbuild_path()
    ctx = RuntimeContext(
        repo_root=REPO_ROOT,
        manifest_path=MANIFEST_PATH,
        frozen=args.frozen,
        update=args.update,
        msbuild_path=msbuild_path,
        generator=args.generator,
        arch=args.arch,
    )

    if args.command == "bootstrap-required":
        configs = parse_configs(args.configs)
        deps = required_dep_names(manifest)
        if not deps:
            print("No required dependencies are marked in the manifest.", file=sys.stderr)
            return 1
        bootstrap_dependencies(ctx, deps, configs)
        return 0

    if args.command == "bootstrap-minimal":
        configs = parse_configs(args.configs)
        deps = bootstrap_minimal_dep_names(manifest)
        bootstrap_dependencies(ctx, deps, configs)
        return 0

    if args.command == "bootstrap-headeronly":
        deps = [name for name in CORE_HEADER_BOOTSTRAP if name in dependencies_by_name(manifest)]
        bootstrap_dependencies(ctx, deps, ["Debug"])
        return 0

    print(f"Unsupported command: {args.command}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
