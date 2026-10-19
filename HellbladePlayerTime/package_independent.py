import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def files(root):
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"Linked input is not supported: {path}")
        if not path.is_file():
            continue
        relative = path.relative_to(root)
        if any(part in {".git", "__pycache__", "build", "build-release", ".vs"} for part in relative.parts):
            continue
        if path.name.startswith(".redsand-") or path.suffix.lower() in {".dll", ".lib", ".pdb", ".obj", ".exe", ".pyc"}:
            continue
        yield path, relative.as_posix()


def add_tree(archive, root, prefix):
    for path, relative in files(root):
        archive.write(path, prefix + "/" + relative)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--prepared", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    dll = args.build / "Release/HellbladeTimedBlock.dll"
    pdb = args.build / "Release/HellbladeTimedBlock.pdb"
    for path in (dll, pdb):
        if not path.is_file():
            raise RuntimeError(f"Missing native output: {path}")
    for path in root.joinpath("source").rglob("*"):
        if path.is_file() and path.suffix in {".cpp", ".hpp", ".h"}:
            content = path.read_text(encoding="utf-8-sig")
            if "Redsand_IsPlayerUnaffectedByBlockTime_v1" in content or "bPlayerUnaffectedBySlowTime" in content:
                raise RuntimeError(f"Retired Redsand bridge remains: {path}")
    test_log = args.build.parent / "hellblade-tests.log"
    if "100% tests passed" not in test_log.read_text(encoding="utf-8-sig"):
        raise RuntimeError("Time regression tests did not pass")
    stage = args.work / "Data"
    if stage.exists():
        raise RuntimeError("Use a new package work directory")
    docs = args.work / "Source/HellbladePlayerTime"
    internal = root.parent / "output/BuildData"
    source_output = internal / "Sources"
    symbols = internal / "Symbols"
    source_output.mkdir(parents=True, exist_ok=True)
    symbols.mkdir(parents=True, exist_ok=True)
    plugins = stage / "SKSE/Plugins"
    docs.mkdir(parents=True)
    plugins.mkdir(parents=True)
    args.output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(dll, plugins / dll.name)
    shutil.copytree(root / "assets", stage, dirs_exist_ok=True)
    install = """Hellblade - Player Time Independence

格挡成功后世界减慢，玩家移动与攻击保持正常速度。原 MCM 的减速时长乘以1.3，不扩大格挡判定时间窗。
原 Hellblade 的格挡判定、音效、反击与 MCM 配置继续使用。补丁不修改玩家移动/攻击的 Actor Value。

安装：在 MO2 中放在原 Hellblade 之后，覆盖同名 DLL。保留原模组完整的 ESP、BSA、脚本与 MCM。
前置：原 Hellblade - Timed Block - AE Support / Hotfix 及其前置、匹配版本的 SKSE64 与 Address Library。
本补丁无 ESP/ESL、音效、贴图或 SKSE 菜单。Redsand Particles 不是本补丁前置。

攻击随机减速默认关闭。此前的屏幕闪电默认关闭，不再与三维闪电混用。
需要格挡三维闪电时可另外使用 Redsand Particles，在它的 SKSE 菜单“命中与格挡 / 格挡闪电”中调整。
本补丁只向已安装的红沙发送成功格挡通知；未装红沙时不做任何事。通知不更改减速或格挡功能。
闪电的开关、概率与颜色不属于 Hellblade 的时间设置。红沙未安装也不影响本补丁原有格挡功能。

时间设置仍在原 MCM 与 SKSE/Plugins/HellbladePlayerTime.ini，修改 INI 后重启或关闭原 MCM 重读。
只支持构建配置中的 SE/AE；未承诺所有运行时实机验证，不支持 VR。当前更新需重新启动游戏加载 DLL。
"""
    credits = """Hellblade - Player Time Independence: local derivative, 2026-10-19 (local build 1.3.5.5)

Hellblade - Timed Block - AE Support: thisdarkocean (moon_river / Tofunmi).
Author page and permissions: https://www.nexusmods.com/skyrimspecialedition/mods/87645
Official source: https://gitlab.com/moon_river/hellbladere
Source baseline: e21165d54fe99a4d3d9e36ea2556fccfba9a55c7
The author permits credited modification and redistribution; the original author restrictions remain in force. Do not represent original assets or code as newly authored by this patch.
The official source metadata says 1.3.2; the installed Hotfix says 1.3.5.1. This local build preserves the DLL name and API compatibility; it is not a byte-identical reconstruction of the original 1.3.5.1 binary.

Local changes: independent world/player VATS time rates; held slowdown and gradual release; pause/foreign ownership handling; player time always 1x; extended block duration; 10% player melee swing activation; block-priority sessions; original procedural branching lightning geometry and shaders with a separate glow pass. An optional visual-only block notification is available for Redsand; no Redsand preference bridge, new UI, ESP, actor speed edits or sounds. Attack slowdown and the legacy screen overlay default to disabled.
Lightning geometry and shader code were created for this patch; no Stellar Blade assets, images, code, downloaded VFX, or other mod artwork is redistributed. The game reference describes the intended style, not a claim of matching its production quality.
Keep this Hellblade derivative free and Donation Points disabled under the upstream distribution terms.
CommonLibSSE-NG: https://github.com/alandtse/CommonLibSSE-NG
Fixed revision: 8b032fa992750d654d6d38a33731714d8b86be1f
GPL-3.0 and the retained Modding / Linking exceptions are included. Matching derivative and dependency source is retained in the separate source archive. Other dependencies retain their bundled licenses.

Source archive BUILDING.txt describes the exact prepared dependency layout used. Matching PDB and native build records remain in output/BuildData; they are not needed by players.
This package is local only; no GitHub Release, tag or upload was created.
"""
    docs.joinpath("INSTALL.zh-CN.txt").write_text(install, encoding="utf-8-sig")
    docs.joinpath("CREDITS.txt").write_text(credits, encoding="utf-8")
    license_dir = docs / "Licenses"
    license_dir.mkdir()
    commonlib = args.prepared / "CommonLibSSE-NG"
    for name in ("COPYING", "EXCEPTIONS"):
        path = commonlib / name
        if not path.is_file():
            path = root.parent / "licenses" / ("CommonLibSSE-NG-" + ("COPYING.txt" if name == "COPYING" else "EXCEPTIONS.txt"))
        shutil.copy2(path, license_dir / ("CommonLibSSE-NG-" + name + ".txt"))
    deps = ("directxmath", "directxtk", "fmt", "rapidcsv", "rsm-binary-io", "simpleini", "spdlog", "tomlplusplus", "xbyak")
    installed_share = args.prepared / "Precision/build-release/vcpkg_installed/x64-windows-static-md/share"
    source_dependencies = {}
    source_zip = source_output / "Hellblade - Corresponding Source.zip"
    with zipfile.ZipFile(source_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in ("source", "tests", "assets"):
            add_tree(archive, root / name, "HellbladePlayerTime/" + name)
        for name in ("CMakeLists.txt", "package_independent.py", "baseline.json", "upstream-project.json", "BUILDING.md"):
            if (root / name).is_file():
                archive.write(root / name, "HellbladePlayerTime/" + name)
        archive.write(root.parent / "LICENSE", "LICENSE")
        add_tree(archive, root.parent / "licenses", "licenses")
        add_tree(archive, root.parent / "cmake", "cmake")
        add_tree(archive, commonlib, "Dependencies/CommonLibSSE-NG")
        minhook_candidates = [args.prepared / "MinHook",
                              args.prepared / "Precision/build-release/_deps/hde64-src",
                              args.prepared / "Precision/build-release/src/CommonLibSSE/_deps/hde64-src",
                              root.parent / "ThirdParty/Dependencies/MinHook"]
        minhook = next((path for path in minhook_candidates
                        if (path / "src/hde/hde64.c").is_file() and (path / "LICENSE.txt").is_file()), None)
        if minhook is None:
            raise RuntimeError("Matching CommonLib Patch Safety decoder source is missing")
        add_tree(archive, minhook, "Dependencies/MinHook")
        for name in deps:
            candidates = sorted((args.prepared / "vcpkg/buildtrees" / name / "src").glob("*.clean"))
            if not candidates:
                retained = root.parent / "ThirdParty/Dependencies" / name
                if retained.is_dir():
                    candidates = [retained]
            if len(candidates) != 1:
                raise RuntimeError(f"Dependency source must be unambiguous: {name}")
            source_dependencies[name] = candidates[0].name
            add_tree(archive, candidates[0], "Dependencies/" + name)
            copyright_path = installed_share / name / "copyright"
            if copyright_path.is_file():
                shutil.copy2(copyright_path, license_dir / (name + "-copyright.txt"))
        archive.write(args.prepared / "Precision/vcpkg.json", "Dependencies/precision-vcpkg.json")
        build_text = """Matching local build source for Hellblade - Player Time Independence.

Toolchain: VS2022 C++ x64, Windows SDK 10.0.26100, CMake 3.31, Release, SE+AE enabled, VR disabled.
Prepared dependency workspace contains CommonLibSSE-NG at revision 8b032fa992750d654d6d38a33731714d8b86be1f and the fixed vcpkg manifest included as Dependencies/precision-vcpkg.json.
All source files used for the derivative and the dependency libraries/headers are included in this archive. Third-party source licenses are retained in their original trees.

The local CMake wrapper expects a prepared workspace: <workspace>/CommonLibSSE-NG, <workspace>/Precision/build-release/src/CommonLibSSE/Release/CommonLibSSE.lib, and <workspace>/Precision/build-release/vcpkg_installed/x64-windows-static-md.
Build the included CommonLib source with SE/AE enabled and VR disabled; build manifest dependencies with x64-windows-static-md and the included baseline. Configure HellbladePlayerTime/CMakeLists.txt with -DREDSAND_BUILD=<workspace>, then build Release and run CTest.
REDSAND_BUILD names the reuse of a local developer dependency workspace only. The produced DLL optionally notifies an installed Redsand DLL of successful player blocks. It never reads Redsand time settings; Redsand is not a runtime requirement.

Official baseline and upstream project identity are recorded in baseline.json and upstream-project.json. Original Hellblade game data is not included: install the original full mod separately.
"""
        archive.writestr("BUILDING.txt", build_text)
        for path in sorted(docs.rglob("*")):
            if path.is_file():
                archive.write(path, "Documentation/" + path.relative_to(docs).as_posix())
    expected_runtime = {"SKSE/Plugins/HellbladeTimedBlock.dll", "SKSE/Plugins/HellbladePlayerTime.ini"}
    actual_runtime = {path.relative_to(stage).as_posix() for path in stage.rglob("*") if path.is_file()}
    if actual_runtime != expected_runtime:
        raise RuntimeError("Unexpected or missing runtime file in Hellblade installer")
    payload = {relative: digest(path) for path, relative in files(stage)}
    payload["SKSE/Plugins/HellbladeTimedBlock.dll"] = digest(plugins / dll.name)
    package = args.output / "Hellblade - Player Time Independence.zip"
    with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(stage).as_posix())
    with zipfile.ZipFile(package) as archive:
        if archive.testzip() is not None:
            raise RuntimeError("Package CRC validation failed")
        names = archive.namelist()
        if any(Path(name).suffix.lower() in {".esp", ".esl", ".esm", ".wav", ".xwm", ".fuz"} for name in names):
            raise RuntimeError("Unexpected game plugin or audio in patch")
        if set(names) != set(payload):
            raise RuntimeError("Package payload mismatch")
        for name, expected in payload.items():
            if hashlib.sha256(archive.read(name)).hexdigest() != expected:
                raise RuntimeError(f"Package content mismatch: {name}")
    shutil.copy2(pdb, symbols / pdb.name)
    record = {
        "name": "Hellblade - Player Time Independence",
        "game_tested": False,
        "dll_sha256": digest(dll),
        "pdb_sha256": digest(pdb),
        "package_sha256": digest(package),
        "source_zip_sha256": digest(source_zip),
        "source_archive": str(source_zip),
        "runtime_only": True,
        "native_build": str(args.build),
        "tests_passed": 5,
        "local_build": "1.3.5.5",
        "attack_trigger_paths": ["SKSE weapon swing", "active player animation graph swing", "custom collision attack start"],
        "neutral_time_pairs_supported": ["world<=0 with stale finite player rate", "world=1/player=1"],
        "defaults": {"attack_enabled": False, "legacy_lightning_enabled": False, "block_duration_multiplier": 1.3, "attack_chance": 0.10, "attack_duration_seconds": 1.5, "attack_world_rate": 0.08},
        "lightning": {"procedural": True, "external_art_assets": False, "screen_space": True, "own_glow": True, "enabled_default": False, "legacy_overlay": True, "random_color_when_enabled": True, "color_per_activation": True, "enb_required": False, "draw_calls_active": 4, "vertex_budget": 131072},
        "runtime_requirements": ["Original Hellblade complete mod and its SKSE/MCM Helper requirements", "matching Address Library"],
        "redsand_required": False,
        "skse_menu_framework_required": False,
        "game_plugins_added": [],
        "audio_added": [],
        "source_dependencies": source_dependencies,
        "payload": payload,
        "maintained_source": {name: digest(root / name) for name in ("CMakeLists.txt", "package_independent.py", "baseline.json", "upstream-project.json")},
    }
    for folder in ("source", "tests", "assets"):
        record["maintained_source"].update({folder + "/" + relative: digest(path) for path, relative in files(root / folder)})
    internal.joinpath("HELLBLADE_BUILD.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"package": str(package), "dll_sha256": record["dll_sha256"], "pdb_sha256": record["pdb_sha256"], "files": len(payload), "tests_passed": 5, "game_tested": False}, ensure_ascii=False))


if __name__ == "__main__":
    main()
