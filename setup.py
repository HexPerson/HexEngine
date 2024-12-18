from git import Repo
import os
import subprocess
import glob
import shutil

engineMainDir = os.getcwd() + "\\"
libraryDir = engineMainDir + "Libs/x64/"
binDir = engineMainDir + "Bin/x64/"
includeDir = engineMainDir + "Include/"

def msbuild(msbuildPath, projectPath, args):
    subprocess.check_call([msbuildPath, projectPath] + args)

print("Engine main directory is %s" % engineMainDir)

msbuildPath = glob.glob("C:/Program Files/Microsoft Visual Studio/2022/*/MSBuild/Current/Bin/msbuild.exe")[0]
print("MSBuild located at %s" % msbuildPath)

def build_assimp(buildConfig):
    if not os.path.exists("ThirdParty/assimp/"):
        print("Cloning Assimp...")
        Repo.clone_from("https://github.com/assimp/assimp.git", "ThirdParty/assimp/")

    os.chdir("ThirdParty/assimp/")
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF -DASSIMP_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_ASSIMP_VIEW=OFF -DASSIMP_NO_EXPORT=OFF -S .. -G "Visual Studio 17 2022" -A x64')

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
    if not os.path.exists("ThirdParty/directxtk/"):
        print("Cloning DirectXTK...")
        Repo.clone_from("https://github.com/microsoft/DirectXTK.git", "ThirdParty/directxtk/")
    else:
        repo = Repo("ThirdParty/directxtk/")
        repo.remotes.origin.pull()
        
    os.chdir("ThirdParty/directxtk/")
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -S .. -G "Visual Studio 17 2022" -A x64')

    projectPath = os.path.realpath("DirectXTK.vcxproj")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["/p:Configuration=" + buildConfig + ""])

    print("Copying DirectXTK library file from %s to %s" % (os.path.realpath("lib/" + buildConfig + "/DirectXTK.lib"), libraryDir))
    shutil.copy(os.path.realpath("lib/" + buildConfig + "/DirectXTK.lib"), libraryDir)

    print("Successfully built DirectXTK!")
    os.chdir(engineMainDir)
    
def build_nrd(buildConfig):
    if not os.path.exists("ThirdParty/nrd/"):
        print("Cloning NRD...")
        Repo.clone_from("https://github.com/NVIDIAGameWorks/RayTracingDenoiser.git", "ThirdParty/nrd/", recursive=True)

    os.chdir("ThirdParty/nrd/")
    
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -DNRD_STATIC_LIBRARY=ON -S .. -G "Visual Studio 17 2022" -A x64')
    
    projectPath = os.path.realpath("NRD.vcxproj")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["/p:Configuration=" + buildConfig + ""])
    
    os.chdir("..")

    # HBAO+ comes pre-built so no need to build it, just copy the libs
    print("Copying NRD library file from %s to %s" % (os.path.realpath("_Bin/" + buildConfig + "/NRD.lib"), libraryDir))
    
    shutil.copy(os.path.realpath("_Bin/" + buildConfig + "/NRD.lib"), libraryDir)

    print("Successfully built NRD!")
    os.chdir(engineMainDir)

def build_directxtk_audio(buildConfig):
    if not os.path.exists("ThirdParty/directxtk/"):
        print("Cloning DirectXTK...")
        Repo.clone_from("https://github.com/microsoft/DirectXTK.git", "ThirdParty/directxtk/")

    os.chdir("ThirdParty/directxtk/")
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -S .. -G "Visual Studio 17 2022" -A x64')
    os.chdir("..")

    projectPath = os.path.realpath("Audio/DirectXTKAudio_Desktop_2022_Win8.vcxproj")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["/p:Configuration=" + buildConfig, "/p:Platform=x64"])

    print("Copying DirectXTKAudio library file from %s to %s" % (os.path.realpath("Audio/Bin/Desktop_2022/x64/" + buildConfig + "/DirectXTK.lib"), libraryDir))
    shutil.copy(os.path.realpath("Audio/Bin/Desktop_2022/x64/" + buildConfig + "/DirectXTKAudioWin8.lib"), libraryDir)

    print("Successfully built DirectXTKAudio!")
    os.chdir(engineMainDir)

def build_physx(buildConfig):
    if not os.path.exists("ThirdParty/physx/"):
        print("Cloning PhysX...")
        Repo.clone_from("https://github.com/NVIDIA-Omniverse/PhysX.git", "ThirdParty/physx/")

    os.chdir("ThirdParty/physx/physx/")

    # stupid hack to force physx to build with dynamic crt
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
        "-t:PhysX SDK\PhysXFoundation:rebuild",
        "-t:PhysX SDK\SimulationController:rebuild",
        "-t:PhysX SDK\SceneQuery:rebuild",
        "-t:PhysX SDK\LowLevel:rebuild",
        "-t:PhysX SDK\LowLevelAABB:rebuild",
        "-t:PhysX SDK\LowLevelDynamics:rebuild",
        "-t:PhysX SDK\PhysXCommon:rebuild",
        "-t:PhysX SDK\PhysXPvdSDK:rebuild",
        "-t:PhysX SDK\PhysXTask:rebuild",     
        "-t:PhysX SDK\PhysX:rebuild",
        "-t:PhysX SDK\PhysXExtensions:rebuild",
        "-t:PhysX SDK\PhysXCharacterKinematic:rebuild",
        "-t:PhysX SDK\PhysXCooking:rebuild",
        "-t:PhysX SDK\PhysXVehicle:rebuild",
        "-t:PhysX SDK\PhysXVehicle2:rebuild",
        "/p:Configuration=" + buildConfig + ""
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
    if not os.path.exists("ThirdParty/shaderconductor/"):
        print("Cloning ShaderConductor...")
        Repo.clone_from("https://github.com/microsoft/ShaderConductor.git", "ThirdParty/shaderconductor/")

    os.chdir("ThirdParty/shaderconductor/")
    
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -G "Visual Studio 17 2022" -T host=x64 -A x64 ../ -DCMAKE_CXX_FLAGS="/wd4189" -DCMAKE_BUILD_TYPE='+buildConfig)
    os.system('cmake --build . ')
    os.chdir("..")
    
    #subprocess.check_call(["python", "BuildAll.py", "vs2022", "vc143", "x64", buildConfig])

    scLibDir = "vs2022-win-vc143-x64"

    if buildConfig == "Debug":
        scLibDir = scLibDir + "Debug"

    print("Copying ShaderConductor library file from %s to %s" % (os.path.realpath("Build/lib/" + buildConfig + "/ShaderConductor.lib"), libraryDir))
    shutil.copy(os.path.realpath("Build/lib/" + buildConfig + "/ShaderConductor.lib"), libraryDir)

    print("Successfully built ShaderConductor!")
    os.chdir(engineMainDir)

def build_hbaoplus(buildConfig):
    if not os.path.exists("ThirdParty/hbaoplus/"):
        print("Cloning HBAO+...")
        Repo.clone_from("https://github.com/NVIDIAGameWorks/HBAOPlus.git", "ThirdParty/hbaoplus/")

    os.chdir("ThirdParty/hbaoplus/")

    # HBAO+ comes pre-built so no need to build it, just copy the libs
    print("Copying HBAO+ library file from %s to %s" % (os.path.realpath("bin/CMake/" + buildConfig + "/DirectXTK.lib"), libraryDir))
    
    shutil.copy(os.path.realpath("lib/GFSDK_SSAO_D3D11.win64.lib"), libraryDir)
    shutil.copy(os.path.realpath("lib/GFSDK_SSAO_D3D11.win64.dll"), binDir + "Bin/")

    print("Successfully built HBAO+!")
    os.chdir(engineMainDir)

def build_freetype(buildConfig):
    if not os.path.exists("ThirdParty/freetype/"):
        print("Cloning FreeType...")
        Repo.clone_from("https://github.com/freetype/freetype.git", "ThirdParty/freetype/")

    os.chdir("ThirdParty/freetype/")
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -S .. -G "Visual Studio 17 2022" -A x64')

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
    if not os.path.exists("ThirdParty/directxtex/"):
        print("Cloning DirectXTex...")
        Repo.clone_from("https://github.com/microsoft/DirectXTex.git", "ThirdParty/directxtex/")

    os.chdir("ThirdParty/directxtex/")
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -S .. -G "Visual Studio 17 2022" -A x64')

    projectPath = os.path.realpath("DirectXTex.vcxproj")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["/p:Configuration=" + buildConfig + ""])

    print("Copying DirectXTex library file from %s to %s" % (os.path.realpath("lib/" + buildConfig + "/DirectXTex.lib"), libraryDir))
    shutil.copy(os.path.realpath("lib/" + buildConfig + "/DirectXTex.lib"), libraryDir)

    print("Successfully built DirectXTex!")
    os.chdir(engineMainDir)

def build_brotli(buildConfig):
    if not os.path.exists("ThirdParty/brotli/"):
        print("Cloning brotli...")
        Repo.clone_from("https://github.com/google/brotli.git", "ThirdParty/brotli/")

    os.chdir("ThirdParty/brotli/")
    os.system("mkdir out")
    os.chdir("out")
    os.system('cmake -S .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=' + buildConfig + ' -DCMAKE_INSTALL_PREFIX=./installed')

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
    if not os.path.exists("ThirdParty/angelscript/"):
        print("Cloning angelscript...")
        Repo.clone_from("https://github.com/codecat/angelscript-mirror.git", "ThirdParty/angelscript/")

    os.chdir("ThirdParty/angelscript/sdk/angelscript/projects/msvc2022")
    projectPath = os.path.realpath("angelscript.sln")
    print("Project path is %s" % projectPath)

    msbuild(msbuildPath, projectPath, ["/p:Configuration=" + buildConfig + ""])

    libName = "angelscriptd.lib"
    if buildConfig == "Release":
        libName = "angelscript.lib"
        
    print("Copying angelscript library file from %s to %s" % (os.path.realpath("../../lib/" + libName), libraryDir))
    
    shutil.copy(os.path.realpath("../../lib/" + libName), libraryDir)
    
    print("Successfully built angelscript!")
    os.chdir(engineMainDir)
    
def build_recastnavigation(buildConfig):
    if not os.path.exists("ThirdParty/recastnavigation/"):
        print("Cloning recastnavigation...")
        Repo.clone_from("https://github.com/recastnavigation/recastnavigation.git", "ThirdParty/recastnavigation/")

    os.chdir("ThirdParty/recastnavigation/")
    os.system("mkdir build")
    os.chdir("build")
    os.system('cmake -S .. -G "Visual Studio 17 2022" -A x64')
    
    print("Successfully built recastnavigation!")
    os.chdir(engineMainDir)

def buildConfig(buildConfig):
    global libraryDir
    libraryDir = engineMainDir + "Libs/x64/" + buildConfig + "/"
    print("Library dir is %s" % libraryDir)

    global binDir
    binDir = engineMainDir + "Bin/x64/" + buildConfig + "/"
    
    build_assimp(buildConfig)
    build_directxtk(buildConfig)
    #build_physx(buildConfig.lower())
    build_shaderconductor(buildConfig)
    build_hbaoplus(buildConfig)
    build_freetype(buildConfig)
    build_directxtex(buildConfig)
    #build_directxtk_audio(buildConfig)
    build_brotli(buildConfig)
    build_angelscript(buildConfig)
    build_recastnavigation(buildConfig)
    build_nrd(buildConfig)

def get_cxxopts():
    if not os.path.exists("ThirdParty/cxxopts/"):
        print("Cloning cxxopts...")
        Repo.clone_from("https://github.com/jarro2783/cxxopts.git", "ThirdParty/cxxopts/")
        
    if not os.path.exists(includeDir + "/cxxopts/"):
        shutil.copytree(os.path.realpath("ThirdParty/cxxopts/include/"), includeDir + "/cxxopts/")
        
def get_fastnoiselite():
    if not os.path.exists("ThirdParty/fastnoiselite/"):
        print("Cloning fastnoiselite...")
        Repo.clone_from("https://github.com/Auburn/FastNoiseLite.git", "ThirdParty/fastnoiselite/")
        
    if not os.path.exists(includeDir + "/fastnoiselite/"):
        shutil.copytree(os.path.realpath("ThirdParty/fastnoiselite/Cpp/"), includeDir + "/fastnoiselite/")

def get_rapidxml():
    if not os.path.exists("ThirdParty/rapidxml/"):
        print("Cloning rapidxml...")
        Repo.clone_from("https://github.com/discord/rapidxml.git", "ThirdParty/rapidxml/")
        
    if not os.path.exists(includeDir + "/rapidxml/"):
        shutil.copytree(os.path.realpath("ThirdParty/rapidxml/"), includeDir + "/rapidxml/")

def get_rapidjson():
    if not os.path.exists("ThirdParty/rapidjson/"):
        print("Cloning rapidjson...")
        Repo.clone_from("https://github.com/nlohmann/json.git", "ThirdParty/rapidjson/")
        
    if not os.path.exists(includeDir + "/nlohmann/"):
        shutil.copytree(os.path.realpath("ThirdParty/rapidjson/include/nlohmann/"), includeDir + "/nlohmann/")
        
def get_retpack2d():
    if not os.path.exists("ThirdParty/retpack2d/"):
        print("Cloning retpack2d...")
        Repo.clone_from("https://github.com/TeamHypersomnia/rectpack2D.git", "ThirdParty/retpack2d/")
        
    if not os.path.exists(includeDir + "/retpack2d/"):
        shutil.copytree(os.path.realpath("ThirdParty/retpack2d/src/"), includeDir + "/retpack2d/")
        
def get_streamline():
    if not os.path.exists("ThirdParty/Streamline/"):
        print("Cloning Streamline...")
        Repo.clone_from("https://github.com/NVIDIAGameWorks/Streamline.git", "ThirdParty/Streamline/")
        
    #if not os.path.exists(includeDir + "/Streamline/"):
    #    shutil.copytree(os.path.realpath("ThirdParty/Streamline/src/"), includeDir + "/Streamline/")
        
        
# Create required directories first
if not os.path.exists("Libs/x64/Debug"):
    os.makedirs("Libs/x64/Debug")

if not os.path.exists("Libs/x64/Release"):
    os.makedirs("Libs/x64/Release")
    
if not os.path.exists("Bin/x64/Debug/Bin"):
    os.makedirs("Bin/x64/Debug/Bin")
    
if not os.path.exists("Bin/x64/Release/Bin"):
    os.makedirs("Bin/x64/Release/Bin")

get_cxxopts()
get_fastnoiselite()
get_rapidxml()
get_rapidjson()
get_retpack2d()
get_streamline()
buildConfig("Debug")
buildConfig("Release")
