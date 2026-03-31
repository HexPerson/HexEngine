import argparse
import glob
import json
import os
import shutil
import subprocess
from pathlib import Path

try:
    from git import Repo
    from git.exc import InvalidGitRepositoryError
except ModuleNotFoundError:
    Repo = None

    class InvalidGitRepositoryError(Exception):
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
    repo.git.checkout(ref)



def ensure_repo(name, recursive=False):
    dep = get_dependency(name)
    depPath = dep["path"]

    if not os.path.exists(depPath):
        print(f"Cloning {dep['name']}...")
        if Repo is not None:
            Repo.clone_from(dep["git_url"], depPath, recursive=recursive)
        else:
            cloneArgs = ["git", "clone", dep["git_url"], depPath]
            if recursive:
                cloneArgs.insert(2, "--recurse-submodules")
            subprocess.check_call(cloneArgs)
    else:
        print(f"Using existing dependency directory: {depPath}")

    if Repo is not None:
        try:
            repo = Repo(depPath)
        except InvalidGitRepositoryError:
            print(f"Warning: '{depPath}' is not a git repository; skipping update/checkout controls")
            return

        if runtimeArgs.update and not runtimeArgs.frozen:
            print(f"[update] Pulling latest for {dep['name']} from origin")
            repo.remotes.origin.pull()

        maybe_checkout_ref(repo, dep)
        return

    if not os.path.exists(os.path.join(depPath, ".git")):
        print(f"Warning: '{depPath}' is not a git repository; skipping update/checkout controls")
        return

    if runtimeArgs.update and not runtimeArgs.frozen:
        print(f"[update] Pulling latest for {dep['name']} from origin")
        subprocess.check_call(["git", "-C", depPath, "pull", "--ff-only"])

    if runtimeArgs.frozen:
        ref = dep.get("ref")
        if not ref:
            print(f"[frozen] {dep['name']}: no ref pinned yet; leaving current state unchanged")
        else:
            print(f"[frozen] {dep['name']}: checking out {ref}")
            subprocess.check_call(["git", "-C", depPath, "checkout", ref])



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
    os.system('cmake -DNRD_STATIC_LIBRARY=ON -S .. -G "' + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH + ' -DCMAKE_CXX_FLAGS="' + runtimeLib + ' /wd4530 "')

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

    os.chdir("ThirdParty/physx/physx/")

    fin = open("buildtools/presets/public/vc17win64.xml", "rt")
    data = fin.read()
    data = data.replace('NV_USE_STATIC_WINCRT" value="True"', 'NV_USE_STATIC_WINCRT" value="False"')
    fin.close()

    fin = open("buildtools/presets/public/vc17win64.xml", "wt")
    fin.write(data)
    fin.close()

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
    os.system('cmake -S .. -G "' + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH)

    print("Successfully built recastnavigation!")
    os.chdir(engineMainDir)


def build_oidn(buildConfig):
    ensure_repo("oidn", recursive=True)

    os.chdir(get_dependency_path("oidn"))
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -S .. -G "' + LEGACY_CMAKE_GENERATOR + '" -A ' + LEGACY_CMAKE_ARCH)

    print("Successfully built oidn!")
    os.chdir(engineMainDir)


def buildConfig(buildConfig):
    global libraryDir
    libraryDir = engineMainDir + "Libs/x64/" + buildConfig + "/"
    print("Library dir is %s" % libraryDir)

    global binDir
    binDir = engineMainDir + "Bin/x64/" + buildConfig + "/"

    build_assimp(buildConfig)
    build_directxtk(buildConfig)
    # build_physx(buildConfig.lower())
    # build_shaderconductor(buildConfig)
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

    if not os.path.exists(includeDir + "/cxxopts/"):
        shutil.copytree(os.path.realpath("ThirdParty/cxxopts/include/"), includeDir + "/cxxopts/")


def get_fastnoiselite():
    ensure_repo("fastnoiselite")

    if not os.path.exists(includeDir + "/fastnoiselite/"):
        shutil.copytree(os.path.realpath("ThirdParty/fastnoiselite/Cpp/"), includeDir + "/fastnoiselite/")


def get_rapidxml():
    ensure_repo("rapidxml")

    if not os.path.exists(includeDir + "/rapidxml/"):
        shutil.copytree(os.path.realpath("ThirdParty/rapidxml/"), includeDir + "/rapidxml/")


def get_rapidjson():
    ensure_repo("rapidjson")

    if not os.path.exists(includeDir + "/nlohmann/"):
        shutil.copytree(os.path.realpath("ThirdParty/rapidjson/include/nlohmann/"), includeDir + "/nlohmann/")


def get_retpack2d():
    ensure_repo("retpack2d")

    if not os.path.exists(includeDir + "/retpack2d/"):
        shutil.copytree(os.path.realpath("ThirdParty/retpack2d/src/"), includeDir + "/retpack2d/")


def get_streamline():
    ensure_repo("streamline")



def parse_args():
    parser = argparse.ArgumentParser(description="HexEngine dependency bootstrap (legacy flow with migration scaffolding)")
    parser.add_argument("--mode", choices=["legacy"], default="legacy", help="Execution mode (legacy is current supported mode)")
    parser.add_argument("--frozen", action="store_true", help="Use pinned refs from dependency manifest where available")
    parser.add_argument("--update", action="store_true", help="Update existing repositories from origin before build")
    parser.add_argument("--print-plan", action="store_true", help="Print dependency plan and exit without cloning/building")
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
