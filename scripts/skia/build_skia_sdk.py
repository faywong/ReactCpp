#!/usr/bin/env python3
import argparse
import importlib.util
import json
import os
import platform
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUILDER_URL = "https://github.com/fonttools/skia-builder.git"
DEFAULT_BUILDER_REF = "auto"


def host_platform() -> str:
    system = platform.system().lower()
    if system == "darwin":
        return "macos"
    if system == "windows":
        return "windows"
    if system == "linux":
        return "linux"
    return system


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in {"x86_64", "amd64"}:
        return "x64"
    if machine in {"aarch64", "arm64"}:
        return "arm64"
    if machine in {"i386", "i686", "x86"}:
        return "x86"
    return machine


def run(cmd, *, cwd=None, env=None):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.check_call([str(c) for c in cmd], cwd=cwd, env=env)


def resolve_builder_ref(builder_root: Path, ref: str) -> str:
    if ref != "auto":
        return ref

    output = subprocess.check_output(
        ["git", "symbolic-ref", "--quiet", "--short", "refs/remotes/origin/HEAD"],
        cwd=builder_root,
        text=True,
    ).strip()
    if output.startswith("origin/"):
        return output.removeprefix("origin/")
    return output


def ensure_builder(builder_root: Path, url: str, ref: str, skip_update: bool):
    if not builder_root.exists():
        builder_root.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", url, builder_root])

    if not skip_update:
        run(["git", "fetch", "--tags", "origin"], cwd=builder_root)
        resolved_ref = resolve_builder_ref(builder_root, ref)
        run(["git", "checkout", resolved_ref], cwd=builder_root)
        run(["git", "submodule", "update", "--init", "--recursive"], cwd=builder_root)


def load_builder_module(builder_root: Path):
    path = builder_root / "build_skia.py"
    spec = importlib.util.spec_from_file_location("reactcpp_skia_builder", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def bool_gn(name: str, value: bool) -> str:
    return f"{name}={'true' if value else 'false'}"


def reactcpp_gn_args(target_platform: str) -> list[str]:
    is_linux = target_platform == "linux"
    is_macos = target_platform == "macos"

    return [
        "is_official_build=true",
        "is_debug=false",
        "skia_enable_pdf=false",
        "skia_enable_discrete_gpu=false",
        "skia_enable_ganesh=true",
        "skia_enable_graphite=false",
        "skia_enable_skottie=false",
        "skia_enable_skshaper=false",
        "skia_use_gl=true",
        bool_gn("skia_use_freetype", is_linux),
        bool_gn("skia_use_fontconfig", is_linux),
        bool_gn("skia_use_fonthost_mac", is_macos),
        bool_gn("skia_use_harfbuzz", is_linux),
        "skia_use_icu=false",
        "skia_use_dng_sdk=false",
        "skia_use_expat=false",
        "skia_use_libjpeg_turbo_encode=false",
        "skia_use_libjpeg_turbo_decode=false",
        "skia_use_libpng_encode=false",
        "skia_use_libpng_decode=false",
        "skia_use_libwebp_encode=false",
        "skia_use_libwebp_decode=false",
        "skia_use_piex=false",
        "skia_use_xps=false",
        "skia_use_zlib=false",
        "skia_enable_spirv_validation=false",
        "skia_use_lua=false",
        "skia_use_wuffs=false",
        bool_gn("skia_enable_fontmgr_empty", not is_linux and not is_macos),
        'extra_cflags=["-DSK_DISABLE_LEGACY_PNG_WRITEBUFFER"]',
    ]


def copy_tree(src: Path, dst: Path):
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def copy_skcms_public_headers(skia_src: Path, sdk_dir: Path):
    src_root = skia_src / "modules" / "skcms"
    dst_root = sdk_dir / "skia" / "modules" / "skcms"
    if not src_root.exists():
        return

    header = src_root / "skcms.h"
    if header.exists():
        dst_root.mkdir(parents=True, exist_ok=True)
        shutil.copy2(header, dst_root / "skcms.h")

    src_headers = src_root / "src"
    if src_headers.exists():
        for path in sorted(src_headers.rglob("*.h")):
            dst = dst_root / "src" / path.relative_to(src_headers)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dst)


def package_sdk(sdk_dir: Path, archive: Path):
    archive.parent.mkdir(parents=True, exist_ok=True)
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(sdk_dir.rglob("*")):
            if path.is_file():
                zf.write(path, path.relative_to(sdk_dir.parent))


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a ReactCpp-ready Skia SDK.")
    parser.add_argument("--builder-root", type=Path, default=REPO_ROOT / ".reactcpp" / "skia-builder")
    parser.add_argument("--builder-url", default=os.environ.get("REACTCPP_SKIA_BUILDER_URL", DEFAULT_BUILDER_URL))
    parser.add_argument("--builder-ref", default=os.environ.get("REACTCPP_SKIA_BUILDER_REF", DEFAULT_BUILDER_REF))
    parser.add_argument("--sdk-root", type=Path, default=REPO_ROOT / ".reactcpp" / "skia-sdk")
    parser.add_argument("--platform", default=host_platform(), choices=["linux", "macos", "windows"])
    parser.add_argument("--arch", default=host_arch(), choices=["x86", "x64", "arm", "arm64", "universal2"])
    parser.add_argument("--skip-builder-update", action="store_true")
    parser.add_argument("--skip-sync-deps", action="store_true")
    parser.add_argument("--no-virtualenv", action="store_true")
    parser.add_argument("--archive", type=Path, default=None)
    args = parser.parse_args()

    builder_root = args.builder_root.resolve()
    sdk_dir = (args.sdk_root / f"{args.platform}-{args.arch}").resolve()
    build_dir = (builder_root / "build-reactcpp" / f"{args.platform}-{args.arch}").resolve()

    ensure_builder(builder_root, args.builder_url, args.builder_ref, args.skip_builder_update)
    builder = load_builder_module(builder_root)

    env = os.environ.copy()
    if args.platform == "macos":
        env.setdefault("MACOSX_DEPLOYMENT_TARGET", "11.0")
    if not args.no_virtualenv:
        env = builder.make_virtualenv(str(build_dir / "venv"))
        if args.platform == "macos":
            env.setdefault("MACOSX_DEPLOYMENT_TARGET", "11.0")

    skia_src = builder_root / "skia"
    if not args.skip_sync_deps:
        run([sys.executable, skia_src / "tools" / "git-sync-deps"], cwd=skia_src, env=env)
    elif not (skia_src / "bin" / ("gn.exe" if args.platform == "windows" else "gn")).exists():
        run([sys.executable, skia_src / "bin" / "fetch-gn"], cwd=skia_src, env=env)

    gn_args = reactcpp_gn_args(args.platform)
    for var in ("CC", "CXX", "AR"):
        value = os.environ.get(var)
        if value:
            gn_args.append(f'{var.lower()}="{value}"')

    shared_lib = args.platform == "windows"
    builder.build_skia(
        str(skia_src),
        str(build_dir),
        gn_args,
        target_cpu=args.arch,
        env=env,
        shared_lib=shared_lib,
        gn_path=None,
    )

    if sdk_dir.exists():
        shutil.rmtree(sdk_dir)
    (sdk_dir / "lib").mkdir(parents=True)
    (sdk_dir / "skia").mkdir(parents=True)

    copy_tree(skia_src / "include", sdk_dir / "skia" / "include")
    copy_skcms_public_headers(skia_src, sdk_dir)

    for name in ("libskia.a", "libskcms.a", "skia.lib", "skia.dll", "skia.dll.lib"):
        src = build_dir / name
        if src.exists():
            shutil.copy2(src, sdk_dir / "lib" / name)

    manifest = {
        "name": "reactcpp-skia-sdk",
        "platform": args.platform,
        "arch": args.arch,
        "builder_url": args.builder_url,
        "builder_ref": args.builder_ref,
        "gn_args": gn_args,
        "shared_lib": shared_lib,
    }
    (sdk_dir / "reactcpp-skia-sdk.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    archive = args.archive or (args.sdk_root / f"reactcpp-skia-sdk-{args.platform}-{args.arch}.zip")
    package_sdk(sdk_dir, archive.resolve())
    print(f"SDK: {sdk_dir}")
    print(f"Archive: {archive.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
