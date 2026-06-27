#!/usr/bin/env python3
import argparse
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from build_skia_sdk import host_arch, host_platform


REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = "skia-sdk-daily.yml"


def run(cmd, *, cwd=None):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.check_call([str(c) for c in cmd], cwd=cwd)


def install_archive(archive: Path, sdk_root: Path):
    sdk_root.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "r") as zf:
        zf.extractall(sdk_root)


def latest_successful_run(repo: str) -> str:
    output = subprocess.check_output(
        [
            "gh",
            "run",
            "list",
            "--repo",
            repo,
            "--workflow",
            WORKFLOW,
            "--status",
            "success",
            "--limit",
            "1",
            "--json",
            "databaseId",
            "--jq",
            ".[0].databaseId",
        ],
        text=True,
    ).strip()
    if not output:
        raise RuntimeError(f"no successful {WORKFLOW} runs found for {repo}")
    return output


def download_artifact(repo: str, run_id: str, artifact_name: str, dest: Path):
    dest.mkdir(parents=True, exist_ok=True)
    run(["gh", "run", "download", run_id, "--repo", repo, "--name", artifact_name, "--dir", dest])
    archives = sorted(dest.glob("*.zip"))
    if not archives:
        raise RuntimeError(f"artifact {artifact_name} did not contain a zip archive")
    return archives[0]


def main() -> int:
    parser = argparse.ArgumentParser(description="Install a ReactCpp Skia SDK.")
    parser.add_argument("--sdk-root", type=Path, default=REPO_ROOT / ".reactcpp" / "skia-sdk")
    parser.add_argument("--platform", default=host_platform(), choices=["linux", "macos", "windows"])
    parser.add_argument("--arch", default=host_arch(), choices=["x86", "x64", "arm", "arm64", "universal2"])
    parser.add_argument("--archive", type=Path, help="Install an existing reactcpp-skia-sdk zip archive")
    parser.add_argument("--from-github-artifact", action="store_true", help="Download the latest successful daily artifact via gh")
    parser.add_argument("--repo", default="faywong/ReactCpp", help="GitHub repo used with --from-github-artifact")
    parser.add_argument("--run-id", help="Specific GitHub Actions run id used with --from-github-artifact")
    args, build_args = parser.parse_known_args()

    if args.archive:
        install_archive(args.archive.resolve(), args.sdk_root.resolve())
        print(f"Installed SDK archive into {args.sdk_root.resolve()}")
        return 0

    if args.from_github_artifact:
        if shutil.which("gh") is None:
            raise RuntimeError("gh is required for --from-github-artifact")
        artifact = f"reactcpp-skia-sdk-{args.platform}-{args.arch}"
        run_id = args.run_id or latest_successful_run(args.repo)
        with tempfile.TemporaryDirectory(prefix="reactcpp-skia-artifact-") as tmp:
            archive = download_artifact(args.repo, run_id, artifact, Path(tmp))
            install_archive(archive, args.sdk_root.resolve())
        print(f"Installed {artifact} from GitHub Actions run {run_id}")
        return 0

    build_script = Path(__file__).resolve().with_name("build_skia_sdk.py")
    run([
        sys.executable,
        build_script,
        "--sdk-root",
        args.sdk_root,
        "--platform",
        args.platform,
        "--arch",
        args.arch,
        *build_args,
    ])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
