import argparse
import glob
import json
import os
import shutil
import stat
import subprocess
import time
from pathlib import Path

try:
    from git import Repo
    from git.exc import GitCommandError, InvalidGitRepositoryError
except ModuleNotFoundError:
    Repo = None

    class InvalidGitRepositoryError(Exception):
        pass

    class GitCommandError(Exception):
        pass

engineMainDir = os.getcwd() + "\\"
libraryDir = engineMainDir + "Libs/x64/"
binDir = engineMainDir + "Bin/x64/"
includeDir = engineMainDir + "Include/"

LEGACY_CMAKE_GENERATOR = 'Visual Studio 17 2022'
LEGACY_CMAKE_ARCH = 'x64'

manifestPath = Path("build") / "dependencies.lock.json"
dependencyManifest = None
dependenciesByName = {}

runtimeArgs = None
msbuildPath = None


def git_env():
    env = os.environ.copy()
    env["GIT_LFS_SKIP_SMUDGE"] = "1"
    return env


def with_lfs_disabled(args):
    if not args or args[0] != "git":
        return args

    return [
        "git",
        "-c", "filter.lfs.smudge=",
        "-c", "filter.lfs.process=",
        "-c", "filter.lfs.required=false",
    ] + args[1:]


def git_check_call(args):
    subprocess.check_call(with_lfs_disabled(args), env=git_env())


def git_check_output(args):
    return subprocess.check_output(with_lfs_disabled(args), text=True, env=git_env())


def remove_readonly(func, path, _):
    os.chmod(path, stat.S_IWRITE)
    func(path)


def remove_dependency_directory(dep_path):
    if not os.path.exists(dep_path):
        return

    shutil.rmtree(dep_path, onerror=remove_readonly)

    if os.path.exists(dep_path):
        stale_path = f"{dep_path}.stale.{int(time.time())}"
        os.replace(dep_path, stale_path)
        print(f"Moved stale dependency directory to: {stale_path}")


def msbuild(msbuildPath, projectPath, args):
    subprocess.check_call([msbuildPath, projectPath] + args)


def load_dependency_manifest():
    global dependencyManifest
    global dependenciesByName

    if not manifestPath.exists():
        raise FileNotFoundError(f"Dependency manifest not found: {manifestPath}")

    with manifestPath.open("r", encoding="utf-8-sig") as manifestFile:
        dependencyManifest = json.load(manifestFile)

    dependenciesByName = {}
    for dep in dependencyManifest.get("dependencies", []):
        dependenciesByName[dep["name"].lower()] = dep



def save_dependency_manifest():
    with manifestPath.open("w", encoding="utf-8", newline="\n") as manifestFile:
        json.dump(dependencyManifest, manifestFile, indent=2)
        manifestFile.write("\n")


def get_dependency_head(dep):
    depPath = dep["path"]
    if not os.path.exists(depPath):
        return None

    if Repo is not None:
        try:
            repo = Repo(depPath)
            return repo.head.commit.hexsha
        except (InvalidGitRepositoryError, ValueError):
            return None

    if not os.path.exists(os.path.join(depPath, ".git")):
        return None

    try:
        return git_check_output(["git", "-C", depPath, "rev-parse", "HEAD"]).strip()
    except subprocess.CalledProcessError:
        return None


def check_refs(strict=False):
    missing = []
    deps = dependencyManifest.get("dependencies", [])

    print("HexEngine dependency ref status")
    print(f"Manifest: {manifestPath}")
    print("")

    for dep in deps:
        ref = dep.get("ref")
        status = "pinned" if ref else "missing"
        print(f"- {dep['name']}: {status} ({ref})")
        if not ref:
            missing.append(dep["name"])

    print("")
    print(f"Pinned refs: {len(deps) - len(missing)}")
    print(f"Missing refs: {len(missing)}")

    if strict and missing:
        print("Strict check failed: some dependency refs are missing.")
        return 1

    return 0


def lock_current_refs():
    changed = 0
    skipped = []

    for dep in dependencyManifest.get("dependencies", []):
        head = get_dependency_head(dep)
        if head is None:
            skipped.append(dep["name"])
            continue

        if dep.get("ref") != head:
            dep["ref"] = head
            changed += 1

    save_dependency_manifest()

    print(f"Updated refs: {changed}")
    if skipped:
        print("Skipped (not a local git repo or missing):")
        for name in skipped:
            print(f"  - {name}")


def get_dependency(name):
    dep = dependenciesByName.get(name.lower())
    if dep is None:
        raise KeyError(f"Dependency '{name}' is not defined in {manifestPath}")
    return dep



def get_dependency_path(name):
    dep = get_dependency(name)
    return dep["path"]



def maybe_checkout_ref(repo, dep):
    ref = dep.get("ref")
    if not runtimeArgs.frozen:
        return

    if not ref:
        print(f"[frozen] {dep['name']}: no ref pinned yet; leaving current state unchanged")
        return

    print(f"[frozen] {dep['name']}: checking out {ref}")
    try:
        repo.git.checkout(ref)
    except GitCommandError:
        print(f"[frozen] {dep['name']}: checkout failed, attempting fetch + clean/reset recovery")
        repo.git.fetch("--all", "--tags")
        repo.git.reset("--hard")
        repo.git.clean("-fd")
        repo.git.checkout(ref)



def clone_dependency_repo(dep, recursive=False):
    depPath = dep["path"]
    if os.path.exists(depPath):
        remove_dependency_directory(depPath)

    cloneArgs = ["git", "clone", dep["git_url"], depPath]
    if recursive:
        cloneArgs.insert(2, "--recurse-submodules")
    git_check_call(cloneArgs)


def sync_submodules(dep_path, recursive):
    if not recursive:
        return

    git_check_call(["git", "-C", dep_path, "submodule", "update", "--init", "--recursive"])


def ensure_repo(name, recursive=False):
    dep = get_dependency(name)
    depPath = dep["path"]

    if not os.path.exists(depPath):
        print(f"Cloning {dep['name']}...")
        clone_dependency_repo(dep, recursive=recursive)
    else:
        print(f"Using existing dependency directory: {depPath}")

    if not os.path.exists(os.path.join(depPath, ".git")):
        print(f"Warning: '{depPath}' is not a git repository; skipping update/checkout controls")
        return

    if runtimeArgs.update and not runtimeArgs.frozen:
        print(f"[update] Pulling latest for {dep['name']} from origin")
        git_check_call(["git", "-C", depPath, "pull", "--ff-only"])
        sync_submodules(depPath, recursive)

    if runtimeArgs.frozen:
        ref = dep.get("ref")
        if not ref:
            print(f"[frozen] {dep['name']}: no ref pinned yet; leaving current state unchanged")
        else:
            print(f"[frozen] {dep['name']}: checking out {ref}")
            try:
                git_check_call(["git", "-C", depPath, "checkout", ref])
            except subprocess.CalledProcessError:
                print(f"[frozen] {dep['name']}: checkout failed, attempting fetch + clean/reset recovery")
                try:
                    git_check_call(["git", "-C", depPath, "fetch", "--all", "--tags"])
                    git_check_call(["git", "-C", depPath, "reset", "--hard"])
                    git_check_call(["git", "-C", depPath, "clean", "-fd"])
                    git_check_call(["git", "-C", depPath, "checkout", ref])
                except subprocess.CalledProcessError:
                    print(f"[frozen] {dep['name']}: recovery failed, re-cloning dependency and retrying checkout")
                    clone_dependency_repo(dep, recursive=recursive)
                    git_check_call(["git", "-C", depPath, "checkout", ref])

        sync_submodules(depPath, recursive)



def print_plan():
    print("HexEngine dependency plan")
    print(f"Manifest: {manifestPath}")
    print(f"Mode: {runtimeArgs.mode}")
    print(f"Frozen refs: {runtimeArgs.frozen}")
    print(f"Update existing repos: {runtimeArgs.update}")
    print("")

    for dep in dependencyManifest.get("dependencies", []):
        print(f"- {dep['name']}")
        print(f"  path: {dep['path']}")
        print(f"  url: {dep['git_url']}")
        print(f"  ref: {dep.get('ref')}")
        print(f"  build_system: {dep.get('build_system')}")

        notes = dep.get("notes")
        if notes:
            print(f"  notes: {notes}")

        stage_paths = dep.get("legacy_stage_paths", [])
        if stage_paths:
            print("  legacy_stage_paths:")
            for stage_path in stage_paths:
                print(f"    - {stage_path}")

    print("")
    print("No build steps were executed (--print-plan).")



def locate_msbuild_path():
    candidates = glob.glob("C:/Program Files/Microsoft Visual Studio/2022/*/MSBuild/Current/Bin/msbuild.exe")
    if not candidates:
        raise FileNotFoundError("Could not find MSBuild.exe under Visual Studio 2022 installation directories")
    return candidates[0]


print("Engine main directory is %s" % engineMainDir)


def build_assimp(buildConfig):
    ensure_repo("assimp")

    os.chdir(get_dependency_path("assimp"))
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF -DASSIMP_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_ASSIMP_VIEW=OFF -DASSIMP_NO_EXPORT=OFF -S .. -G "' + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH)

    projectPath = os.path.realpath("ALL_BUILD.vcxproj")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["/p:Configuration=" + buildConfig + ""])

    libName = "assimp-vc143-mtd.lib"
    if buildConfig == "Release":
        libName = "assimp-vc143-mt.lib"

    print("Copying assimp library file from %s to %s" % (os.path.realpath("lib/" + buildConfig + "/" + libName), libraryDir))

    shutil.copy(os.path.realpath("lib/" + buildConfig + "/" + libName), libraryDir)

    if buildConfig == "Release":
        shutil.copy(os.path.realpath("contrib/zlib/" + buildConfig + "/zlibstatic.lib"), libraryDir)
    else:
        shutil.copy(os.path.realpath("contrib/zlib/" + buildConfig + "/zlibstaticd.lib"), libraryDir)

    shutil.copy(os.path.realpath("include/assimp/config.h"), os.path.realpath("../include/assimp/"))

    print("Successfully built assimp!")
    os.chdir(engineMainDir)


def build_directxtk(buildConfig):
    ensure_repo("directxtk")

    os.chdir(get_dependency_path("directxtk"))
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -S .. -G "' + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH)

    projectPath = os.path.realpath("DirectXTK.vcxproj")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["/p:Configuration=" + buildConfig + ""])

    print("Copying DirectXTK library file from %s to %s" % (os.path.realpath("lib/" + buildConfig + "/DirectXTK.lib"), libraryDir))
    shutil.copy(os.path.realpath("lib/" + buildConfig + "/DirectXTK.lib"), libraryDir)

    print("Successfully built DirectXTK!")
    os.chdir(engineMainDir)


# noqa: C901 - Preserve existing legacy build behavior with minimal migration edits.
def build_nrd(buildConfig):
    ensure_repo("nrd", recursive=True)

    os.chdir(get_dependency_path("nrd"))

    runtimeLib = "/MDd"
    runtimeLib2 = "MultiThreadedDebugDLL"

    if buildConfig == "Release":
        runtimeLib = "/MD"
        runtimeLib2 = "MultiThreadedDLL"

    os.system("mkdir build")
    os.chdir("build")
    os.system(
        'cmake -DNRD_STATIC_LIBRARY=ON -DNRD_EMBEDS_SPIRV_SHADERS=OFF -S .. -G "'
        + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH + ' -DCMAKE_CXX_FLAGS="' + runtimeLib + ' /wd4530 "'
    )

    if not os.path.exists("NRD.sln"):
        print("Warning: NRD.sln was not generated on first configure attempt. Retrying with a clean build directory.")
        os.chdir("..")
        shutil.rmtree("build", ignore_errors=True)
        os.makedirs("build", exist_ok=True)
        os.chdir("build")
        os.system(
            'cmake -DNRD_STATIC_LIBRARY=ON -DNRD_EMBEDS_SPIRV_SHADERS=OFF -S .. -G "'
            + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH + ' -DCMAKE_CXX_FLAGS="' + runtimeLib + ' /wd4530 "'
        )

    projectPath = os.path.realpath("NRD.sln")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, [
        "/p:Configuration=" + buildConfig,
        "/p:WarningLevel=0",
        "/p:RuntimeLibrary=" + runtimeLib2,
    ])

    os.chdir("..")

    print("Copying NRD library file from %s to %s" % (os.path.realpath("_Bin/" + buildConfig + "/NRD.lib"), libraryDir))

    shutil.copy(os.path.realpath("_Bin/" + buildConfig + "/NRD.lib"), libraryDir)

    print("Successfully built NRD!")
    os.chdir(engineMainDir)


def build_directxtk_audio(buildConfig):
    ensure_repo("directxtk")

    os.chdir(get_dependency_path("directxtk"))
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -S .. -G "' + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH)
    os.chdir("..")

    projectPath = os.path.realpath("Audio/DirectXTKAudio_Desktop_2022_Win8.vcxproj")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["/p:Configuration=" + buildConfig, "/p:Platform=x64"])

    print("Copying DirectXTKAudio library file from %s to %s" % (os.path.realpath("Audio/Bin/Desktop_2022/x64/" + buildConfig + "/DirectXTK.lib"), libraryDir))
    shutil.copy(os.path.realpath("Audio/Bin/Desktop_2022/x64/" + buildConfig + "/DirectXTKAudioWin8.lib"), libraryDir)

    print("Successfully built DirectXTKAudio!")
    os.chdir(engineMainDir)


def build_physx(buildConfig):
    ensure_repo("physx")
    physxRepoRoot = os.path.realpath(get_dependency_path("physx"))
    layoutCandidates = [
        os.path.join(physxRepoRoot, "physx"),
        physxRepoRoot,
    ]

    buildRoot = None
    for candidate in layoutCandidates:
        presetPath = os.path.join(candidate, "buildtools/presets/public/vc17win64.xml")
        generateScriptPath = os.path.join(candidate, "generate_projects.bat")
        if os.path.exists(presetPath) and os.path.exists(generateScriptPath):
            buildRoot = candidate
            break

    if buildRoot is None:
        raise RuntimeError(
            "PhysX dependency layout is not compatible with legacy bootstrap. "
            "Expected buildtools/presets/public/vc17win64.xml and generate_projects.bat under ThirdParty/physx or ThirdParty/physx/physx. "
            "Try running with --frozen to use the pinned ref in build/dependencies.lock.json."
        )

    os.chdir(buildRoot)

    presetPath = "buildtools/presets/public/vc17win64.xml"
    with open(presetPath, "rt", encoding="utf-8") as fin:
        data = fin.read()
    data = data.replace('NV_USE_STATIC_WINCRT" value="True"', 'NV_USE_STATIC_WINCRT" value="False"')
    with open(presetPath, "wt", encoding="utf-8") as fin:
        fin.write(data)

    os.system("generate_projects.bat vc17win64")
    os.chdir("compiler/vc17win64/")

    projectPath = os.path.realpath("PhysXSDK.sln")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, [
        "-t:PhysX SDK\\PhysXFoundation:rebuild",
        "-t:PhysX SDK\\SimulationController:rebuild",
        "-t:PhysX SDK\\SceneQuery:rebuild",
        "-t:PhysX SDK\\LowLevel:rebuild",
        "-t:PhysX SDK\\LowLevelAABB:rebuild",
        "-t:PhysX SDK\\LowLevelDynamics:rebuild",
        "-t:PhysX SDK\\PhysXCommon:rebuild",
        "-t:PhysX SDK\\PhysXPvdSDK:rebuild",
        "-t:PhysX SDK\\PhysXTask:rebuild",
        "-t:PhysX SDK\\PhysX:rebuild",
        "-t:PhysX SDK\\PhysXExtensions:rebuild",
        "-t:PhysX SDK\\PhysXCharacterKinematic:rebuild",
        "-t:PhysX SDK\\PhysXCooking:rebuild",
        "-t:PhysX SDK\\PhysXVehicle:rebuild",
        "-t:PhysX SDK\\PhysXVehicle2:rebuild",
        "/p:Configuration=" + buildConfig + "",
    ])

    os.chdir("../../")

    print("Copying PhysX library file from %s to %s" % (os.path.realpath("bin/win.x86_64.vc143.md/" + buildConfig), libraryDir))

    for file in glob.glob("bin/win.x86_64.vc143.md/" + buildConfig + "/*.lib"):
        shutil.copy(file, libraryDir)

    for file in glob.glob("bin/win.x86_64.vc143.md/" + buildConfig + "/*.dll"):
        shutil.copy(file, binDir + "Bin")

    print("Successfully built PhysX!")
    os.chdir(engineMainDir)


def build_shaderconductor(buildConfig):
    ensure_repo("shaderconductor")

    os.chdir(get_dependency_path("shaderconductor"))

    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -G "' + LEGACY_CMAKE_GENERATOR + '" -T host=x64 -A x64 ../ -DCMAKE_CXX_FLAGS="/wd4189" -DCMAKE_BUILD_TYPE=' + buildConfig)
    os.system("cmake --build . --config " + buildConfig)
    os.chdir("..")

    print("Copying ShaderConductor library file from %s to %s" % (os.path.realpath("Build/lib/" + buildConfig + "/ShaderConductor.lib"), libraryDir))
    shutil.copy(os.path.realpath("Build/lib/" + buildConfig + "/ShaderConductor.lib"), libraryDir)

    print("Successfully built ShaderConductor!")
    os.chdir(engineMainDir)


def build_hbaoplus(buildConfig):
    ensure_repo("hbaoplus")

    os.chdir(get_dependency_path("hbaoplus"))

    print("Copying HBAO+ library file from %s to %s" % (os.path.realpath("bin/CMake/" + buildConfig + "/DirectXTK.lib"), libraryDir))

    shutil.copy(os.path.realpath("lib/GFSDK_SSAO_D3D11.win64.lib"), libraryDir)
    shutil.copy(os.path.realpath("lib/GFSDK_SSAO_D3D11.win64.dll"), binDir + "Bin/")

    print("Successfully built HBAO+!")
    os.chdir(engineMainDir)


def build_freetype(buildConfig):
    ensure_repo("freetype")

    os.chdir(get_dependency_path("freetype"))
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -S .. -G "' + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH)

    projectPath = os.path.realpath("freetype.sln")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["-t:freetype", "/p:Configuration=" + buildConfig + ""])

    libName = "freetype.lib"
    if buildConfig == "Debug":
        libName = "freetyped.lib"

    print("Copying freetype library file from %s to %s" % (os.path.realpath(buildConfig + "/" + libName), libraryDir))
    shutil.copy(os.path.realpath(buildConfig + "/" + libName), libraryDir)

    print("Successfully built freetype!")
    os.chdir(engineMainDir)


def build_directxtex(buildConfig):
    ensure_repo("directxtex")

    os.chdir(get_dependency_path("directxtex"))
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -S .. -G "' + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH)

    projectPath = os.path.realpath("DirectXTex.vcxproj")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["/p:Configuration=" + buildConfig + ""])

    print("Copying DirectXTex library file from %s to %s" % (os.path.realpath("lib/" + buildConfig + "/DirectXTex.lib"), libraryDir))
    shutil.copy(os.path.realpath("lib/" + buildConfig + "/DirectXTex.lib"), libraryDir)

    print("Successfully built DirectXTex!")
    os.chdir(engineMainDir)


def build_brotli(buildConfig):
    ensure_repo("brotli")

    os.chdir(get_dependency_path("brotli"))
    os.system("mkdir out")
    os.chdir("out")
    os.system('cmake -S .. -G "' + LEGACY_CMAKE_GENERATOR + '" -A x64 -DCMAKE_BUILD_TYPE=' + buildConfig + ' -DCMAKE_INSTALL_PREFIX=./installed')

    projectPath = os.path.realpath("brotli.sln")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["/p:Configuration=" + buildConfig + ""])

    print("Copying brotli files")

    for file in glob.glob(buildConfig + "/*.lib"):
        shutil.copy(file, libraryDir)

    for file in glob.glob(buildConfig + "/*.dll"):
        shutil.copy(file, binDir + "Bin")

    for file in glob.glob(buildConfig + "/*.exe"):
        shutil.copy(file, binDir + "Bin")

    print("Successfully built brotli!")
    os.chdir(engineMainDir)


def build_angelscript(buildConfig):
    ensure_repo("angelscript")

    os.chdir("ThirdParty/angelscript/sdk/angelscript/projects/msvc2022")
    projectPath = os.path.realpath("angelscript.sln")
    print("Project path is %s" % projectPath)

    libName = "angelscriptd.lib"
    runtimeLib = "MultiThreadedDebugDLL"

    if buildConfig == "Release":
        libName = "angelscript.lib"
        runtimeLib = "MultiThreadedDLL"

    msbuild(msbuildPath, projectPath, [
        "/p:Configuration=" + buildConfig,
        "/p:RuntimeLibrary=" + runtimeLib,
        "/p:ANGELSCRIPT_EXPORT=0",
    ])

    print("Copying angelscript library file from %s to %s" % (os.path.realpath("../../lib/" + libName), libraryDir))

    shutil.copy(os.path.realpath("../../lib/" + libName), libraryDir)

    print("Successfully built angelscript!")
    os.chdir(engineMainDir)


def build_recastnavigation(buildConfig):
    ensure_repo("recastnavigation")

    os.chdir(get_dependency_path("recastnavigation"))
    os.system("mkdir build")
    os.chdir("build")
    cmakeExitCode = os.system(
        'cmake -S .. -G "' + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH
        + ' -DRECASTNAVIGATION_DEMO=OFF -DRECASTNAVIGATION_TESTS=OFF -DRECASTNAVIGATION_EXAMPLES=OFF'
    )
    if cmakeExitCode != 0:
        print("Warning: recastnavigation configure failed; continuing because this dependency is optional in legacy setup.")
    else:
        print("Successfully configured recastnavigation!")
    os.chdir(engineMainDir)


def build_oidn(buildConfig):
    print("Skipping oidn bootstrap: optional dependency in legacy setup (known submodule path-length instability on Windows clones).")
    return


def buildConfig(buildConfig):
    global libraryDir
    libraryDir = engineMainDir + "Libs/x64/" + buildConfig + "/"
    print("Library dir is %s" % libraryDir)

    global binDir
    binDir = engineMainDir + "Bin/x64/" + buildConfig + "/"

    build_assimp(buildConfig)
    build_directxtk(buildConfig)
    build_physx(buildConfig.lower())
    build_shaderconductor(buildConfig)
    build_hbaoplus(buildConfig)
    build_freetype(buildConfig)
    build_directxtex(buildConfig)
    # build_directxtk_audio(buildConfig)
    build_brotli(buildConfig)
    build_angelscript(buildConfig)
    build_recastnavigation(buildConfig)
    build_nrd(buildConfig)
    build_oidn(buildConfig)


def get_cxxopts():
    ensure_repo("cxxopts")

    if runtimeArgs.header_layout == "external":
        print("[header-layout=external] Skipping Include/cxxopts copy; use ThirdParty/cxxopts/include via target-based include paths.")
        return

    if not os.path.exists(includeDir + "/cxxopts/"):
        shutil.copytree(os.path.realpath("ThirdParty/cxxopts/include/"), includeDir + "/cxxopts/")


def get_fastnoiselite():
    ensure_repo("fastnoiselite")

    if runtimeArgs.header_layout == "external":
        print("[header-layout=external] Skipping Include/fastnoiselite copy; use ThirdParty/fastnoiselite/Cpp via target-based include paths.")
        return

    if not os.path.exists(includeDir + "/fastnoiselite/"):
        shutil.copytree(os.path.realpath("ThirdParty/fastnoiselite/Cpp/"), includeDir + "/fastnoiselite/")


def get_rapidxml():
    ensure_repo("rapidxml")

    if runtimeArgs.header_layout == "external":
        print("[header-layout=external] Skipping Include/rapidxml copy; use ThirdParty/rapidxml via target-based include paths.")
        return

    if not os.path.exists(includeDir + "/rapidxml/"):
        shutil.copytree(os.path.realpath("ThirdParty/rapidxml/"), includeDir + "/rapidxml/")


def get_rapidjson():
    ensure_repo("rapidjson")

    if runtimeArgs.header_layout == "external":
        print("[header-layout=external] Skipping Include/nlohmann copy; use ThirdParty/rapidjson/include via target-based include paths.")
        return

    if not os.path.exists(includeDir + "/nlohmann/"):
        shutil.copytree(os.path.realpath("ThirdParty/rapidjson/include/nlohmann/"), includeDir + "/nlohmann/")


def get_retpack2d():
    ensure_repo("retpack2d")

    if runtimeArgs.header_layout == "external":
        print("[header-layout=external] Skipping Include/retpack2d copy; use ThirdParty/retpack2d/src via target-based include paths.")
        return

    if not os.path.exists(includeDir + "/retpack2d/"):
        shutil.copytree(os.path.realpath("ThirdParty/retpack2d/src/"), includeDir + "/retpack2d/")


def get_streamline():
    try:
        ensure_repo("streamline")
    except subprocess.CalledProcessError as ex:
        warningContext = "header-only mode" if runtimeArgs.header_only_bootstrap else "legacy mode"
        print(f"Warning: streamline bootstrap failed in {warningContext} ({ex}). Continuing without streamline.")
        return



def parse_args():
    parser = argparse.ArgumentParser(description="HexEngine dependency bootstrap (legacy flow with migration scaffolding)")
    parser.add_argument("--mode", choices=["legacy"], default="legacy", help="Execution mode (legacy is current supported mode)")
    parser.add_argument("--frozen", action="store_true", help="Use pinned refs from dependency manifest where available")
    parser.add_argument("--update", action="store_true", help="Update existing repositories from origin before build")
    parser.add_argument("--print-plan", action="store_true", help="Print dependency plan and exit without cloning/building")
    parser.add_argument("--check-refs", action="store_true", help="Print dependency ref pin status and exit")
    parser.add_argument("--check-refs-strict", action="store_true", help="Fail if any dependency ref is missing")
    parser.add_argument("--lock-current-refs", action="store_true", help="Write current local dependency HEAD commits into manifest refs")
    parser.add_argument("--header-only-bootstrap", action="store_true", help="Fetch/copy header-only dependencies only (skip native library builds)")
    parser.add_argument("--header-layout", choices=["legacy", "external"], default="legacy", help="Header staging mode for migrated dependencies")
    return parser.parse_args()


def ensure_output_directories():
    if not os.path.exists("Libs/x64/Debug"):
        os.makedirs("Libs/x64/Debug")

    if not os.path.exists("Libs/x64/Release"):
        os.makedirs("Libs/x64/Release")

    if not os.path.exists("Bin/x64/Debug/Bin"):
        os.makedirs("Bin/x64/Debug/Bin")

    if not os.path.exists("Bin/x64/Release/Bin"):
        os.makedirs("Bin/x64/Release/Bin")


def main():
    global runtimeArgs
    global msbuildPath

    runtimeArgs = parse_args()
    load_dependency_manifest()

    if runtimeArgs.print_plan:
        print_plan()
        return

    if runtimeArgs.check_refs:
        raise SystemExit(check_refs(strict=False))

    if runtimeArgs.check_refs_strict:
        raise SystemExit(check_refs(strict=True))

    if runtimeArgs.lock_current_refs:
        lock_current_refs()
        return

    if runtimeArgs.header_only_bootstrap:
        get_cxxopts()
        get_fastnoiselite()
        get_rapidxml()
        get_rapidjson()
        get_retpack2d()
        get_streamline()
        return

    msbuildPath = locate_msbuild_path()
    print("MSBuild located at %s" % msbuildPath)

    ensure_output_directories()

    get_cxxopts()
    get_fastnoiselite()
    get_rapidxml()
    get_rapidjson()
    get_retpack2d()
    get_streamline()
    buildConfig("Debug")
    buildConfig("Release")


if __name__ == "__main__":
    main()
