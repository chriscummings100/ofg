# Builds the pinned Slang compiler with shared-memory support; release WASM archives are single-threaded.
param([int]$Parallel = 6)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo 'build/slang-threaded-source'
$hostBuild = Join-Path $repo 'build/slang-generators'
$webBuild = Join-Path $repo 'build/slang-web'
$revision = 'ca6e0a657c52881166295f11c14d57afdf0de481' # Slang v2026.17.1, matching pinned RHI.

# Stops at the first failed tool instead of packaging incomplete or incompatible libraries.
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

if (-not $env:EMSDK) { throw 'Set EMSDK to the installed Emscripten SDK root before running this script.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio x64 C++ build tools are required for Slang generators.' }
& (Join-Path $vsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

if (-not (Test-Path -LiteralPath $source)) {
    Invoke-Checked git @('clone', '--depth', '1', '--branch', 'v2026.17.1', 'https://github.com/shader-slang/slang.git', $source)
}
$actualRevision = & git -C $source rev-parse HEAD
if ($actualRevision -ne $revision) { throw "Slang source must remain at $revision; found $actualRevision" }
Invoke-Checked git @('-C', $source, 'submodule', 'update', '--init', '--depth', '1',
    'external/miniz', 'external/lz4', 'external/cmark', 'external/unordered_dense',
    'external/spirv-headers', 'external/fast_float', 'external/nlohmann-json', 'external/lua', 'external/vulkan')

$options = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DSLANG_LIB_TYPE=STATIC',
    '-DSLANG_SLANG_LLVM_FLAVOR=DISABLE', '-DSLANG_ENABLE_SLANG_RHI=OFF', '-DSLANG_ENABLE_TESTS=OFF',
    '-DSLANG_ENABLE_EXAMPLES=OFF', '-DSLANG_ENABLE_GFX=OFF', '-DSLANG_ENABLE_SLANG_GLSLANG=OFF',
    '-DSLANG_ENABLE_CUDA=OFF', '-DSLANG_ENABLE_OPTIX=OFF', '-DSLANG_ENABLE_AFTERMATH=OFF',
    '-DSLANG_ENABLE_SLANGD=OFF', '-DSLANG_ENABLE_SLANGC=OFF', '-DSLANG_ENABLE_SLANGI=OFF',
    '-DSLANG_ENABLE_SLANGRT=OFF', '-DSLANG_ENABLE_REPLAYER=OFF', '-DSLANG_ENABLE_SPLIT_DEBUG_INFO=OFF',
    '-DSLANG_ENABLE_DXIL=OFF', '-DSLANG_EXCLUDE_DAWN=ON', '-DSLANG_EXCLUDE_TINT=ON')
Invoke-Checked cmake (@('-S', $source, '-B', $hostBuild) + $options)
Invoke-Checked cmake @('--build', $hostBuild, '--target', 'all-generators', '--parallel', "$Parallel")

$env:PATH = (Join-Path $env:EMSDK 'upstream/emscripten') + ';' + $env:PATH
$toolchain = (Join-Path $env:EMSDK 'upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake').Replace('\', '/')
$generators = (Join-Path $hostBuild 'generators/Release/bin').Replace('\', '/')
Invoke-Checked cmake (@('-S', $source, '-B', $webBuild, "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DSLANG_GENERATORS_PATH=$generators", "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$($webBuild.Replace('\', '/'))/lib", '-DCMAKE_C_FLAGS=-pthread -fwasm-exceptions',
    '-DCMAKE_CXX_FLAGS=-pthread -fwasm-exceptions', '-DCMAKE_EXE_LINKER_FLAGS=-pthread -fwasm-exceptions') + $options)
Invoke-Checked cmake @('--build', $webBuild, '--target', 'slang', '--parallel', "$Parallel")
