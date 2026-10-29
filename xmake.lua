-- SilentPatch for GTA San Andreas 1.0 (US/EU)
-- Vendored dependencies: ModUtils (SilentPatch/Utils), miniaudio, RenderWare 3.6 headers

set_project("SilentPatchSA")
set_xmakever("3.0.1")

set_languages("c17", "cxx17")

set_allowedplats("windows")
set_plat("windows")
set_allowedarchs("x86")
set_arch("x86")

set_toolchains("msvc")

add_rules("mode.debug", "mode.releasedbg", "mode.release")

if is_mode("release") then
    set_optimize("fastest")
end

-- miniaudio is only used to decode streamed FLAC user tracks, everything else
-- (device I/O, engine, resource manager, other codecs) is compiled out.
local miniaudio_defines = {
    "MA_NO_DEVICE_IO",
    "MA_NO_ENGINE",
    "MA_NO_NODE_GRAPH",
    "MA_NO_RESOURCE_MANAGER",
    "MA_NO_GENERATION",
    "MA_NO_WAV",
    "MA_NO_MP3",
    "MA_NO_VORBIS"
}

target("miniaudio")
    set_kind("static")
    set_basename("miniaudio")

    add_files("third_party/miniaudio/miniaudio.c")
    add_includedirs("third_party/miniaudio", { public = true })
    add_defines(miniaudio_defines, { public = true })
    add_defines("_CRT_SECURE_NO_WARNINGS")

    add_cflags("/W3")

-- Shared GTA SA / ModUtils code (no PCH, matches the original projects)
target("silentpatch-common")
    set_kind("static")

    add_defines("PATTERNS_USE_HINTS=1")
    add_defines("_GTA_SA")
    add_defines("_HAS_EXCEPTIONS=0")

    add_includedirs("SilentPatch")

    add_files("SilentPatch/Desktop.cpp")
    add_files("SilentPatch/FriendlyMonitorNames.cpp")
    add_files("SilentPatch/ParseUtils.cpp")
    add_files("SilentPatch/SVF.cpp")
    add_files("SilentPatch/TheFLAUtils.cpp")
    add_files("SilentPatch/Utils/Patterns.cpp")

target("SilentPatchSA")
    set_kind("shared")
    set_basename("SilentPatchSA")
    set_extension(".asi")

    add_files("SilentPatchSA/*.cpp")
    add_files("SilentPatchSA/*.rc")

    set_pcxxheader("SilentPatchSA/StdAfxSA.h")

    -- Version metadata consumed by the resource script
    add_defines("SILENTPATCH_NAME=SilentPatchSA")
    add_defines("SILENTPATCH_EXT=.asi")
    add_defines("SILENTPATCH_FULL_NAME=SilentPatch for San Andreas")
    add_defines("SILENTPATCH_REVISION_ID=34")
    add_defines("SILENTPATCH_BUILD_ID=1")
    add_defines("SILENTPATCH_COPYRIGHT=2014-2026")

    add_defines("PATTERNS_USE_HINTS=1")
    add_defines("_GTA_SA")
    add_defines("_HAS_EXCEPTIONS=0")
    add_defines("UNICODE", "_UNICODE")
    add_defines("_SECURE_SCL=0")

    add_includedirs("SilentPatch")
    add_includedirs("SilentPatchSA")
    add_includedirs("third_party/rw3603/include/d3d9")

    add_deps("silentpatch-common", "miniaudio")

    add_syslinks("kernel32", "user32", "gdi32", "winspool", "comdlg32", "advapi32")
    add_syslinks("shell32", "ole32", "oleaut32", "uuid", "odbc32", "odbccp32")
    add_syslinks("comctl32", "setupapi", "shlwapi")
    add_syslinks("delayimp")

    add_cxflags("/W4", "/MP")
    add_cxflags("/Zc:threadSafeInit-", "/Zc:strictStrings", "/permissive-")
    add_cxflags("/GR-")
    add_cxflags("/arch:IA32", { force = true })

    add_ldflags("/LARGEADDRESSAWARE", { force = true })
    add_ldflags("/DELAYLOAD:\"shell32.dll\"", { force = true })
    add_ldflags("/DELAYLOAD:\"shlwapi.dll\"", { force = true })
    add_ldflags("/SUBSYSTEM:WINDOWS", { force = true })
