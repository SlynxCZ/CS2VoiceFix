add_rules("mode.debug", "mode.release")

includes("@builtin/xpack")

-- HL2SDK & Metamod Source paths from environment
local SDK_PATH = os.getenv("HL2SDKCS2")
local MM_PATH  = os.getenv("MMSOURCE112")

------------------------------------------------------
-- PROTOBUFS TARGET (Generuje a buildí staticky)
------------------------------------------------------

target("Protobufs")
    set_kind("static")

    local protoc    = SDK_PATH .. "/devtools/bin/linux/protoc"
    local proto_dir = "vendor/Protobufs/csgo"
    local out_dir   = "protobufs/generated"

    local protos = {
        "network_connection.proto",
        "networkbasetypes.proto",
        "cs_gameevents.proto",
        "engine_gcmessages.proto",
        "gcsdk_gcmessages.proto",
        "cstrike15_gcmessages.proto",
        "cstrike15_usermessages.proto",
        "netmessages.proto",
        "steammessages.proto",
        "usermessages.proto",
        "gameevents.proto",
        "clientmessages.proto",
        "te.proto"
    }

    before_build(function (target)
        for _, f in ipairs(protos) do
            local in_path  = path.join(proto_dir, f)
            local out_path = path.join(out_dir, path.basename(f) .. ".pb.cc")

            local need_regen = false
            if not os.exists(out_path) then
                need_regen = true
            else
                local in_mtime  = os.mtime(in_path) or 0
                local out_mtime = os.mtime(out_path) or 0
                if in_mtime > out_mtime then
                    need_regen = true
                end
            end

            if need_regen then
                cprint("${bright blue}[Protobuf]${clear} Generating " .. f)
                os.execv(protoc, {
                    "-I", SDK_PATH .. "/thirdparty/protobuf-3.21.8/src",
                    "--proto_path=" .. proto_dir,
                    "--cpp_out=" .. out_dir,
                    in_path
                })
            end
        end
    end)

    if os.isdir(out_dir) then
        add_files(out_dir .. "/*.cc")
    end

    add_includedirs(out_dir, {public = true})
    add_includedirs(SDK_PATH .. "/thirdparty/protobuf-3.21.8/src", {public = true})
    add_links(SDK_PATH .. "/lib/linux64/release/libprotobuf.a")

------------------------------------------------------
-- MAIN PLUGIN TARGET
------------------------------------------------------

target("CS2VoiceFix")
    set_kind("shared")
    set_symbols("hidden")
    set_languages("cxx20")

    add_deps("Protobufs")

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")

    -- SDK source files required for linking
    add_files({
        SDK_PATH.."/tier1/convar.cpp",
        SDK_PATH.."/public/tier0/memoverride.cpp",
        SDK_PATH.."/tier1/generichash.cpp",
        SDK_PATH.."/entity2/entitysystem.cpp",
        SDK_PATH.."/entity2/entityidentity.cpp",
        SDK_PATH.."/entity2/entitykeyvalues.cpp",
        SDK_PATH.."/tier1/keyvalues3.cpp",
    })

    if is_plat("windows") then
        add_links({
            SDK_PATH.."/lib/public/win64/tier0.lib",
            SDK_PATH.."/lib/public/win64/tier1.lib",
            SDK_PATH.."/lib/public/win64/interfaces.lib",
            SDK_PATH.."/lib/public/win64/mathlib.lib",
        })
    else
        add_links({
            SDK_PATH.."/lib/linux64/libtier0.so",
            SDK_PATH.."/lib/linux64/tier1.a",
            SDK_PATH.."/lib/linux64/interfaces.a",
            SDK_PATH.."/lib/linux64/mathlib.a",
        })
    end

    add_linkdirs({
        "vendor/funchook/lib/Release",
    })

    add_links({
        "funchook",
        "distorm",
        "protobufs"  -- static target defined above
    })

    if is_plat("windows") then
        add_links("psapi")
        add_files("src/utils/plat_win.cpp")
    else
        add_files("src/utils/plat_unix.cpp")
    end

    add_includedirs({
        "src",
        "vendor/funchook/include",
        "vendor",
        -- SDK includes
        SDK_PATH,
        SDK_PATH.."/thirdparty/protobuf-3.21.8/src",
        SDK_PATH.."/common",
        SDK_PATH.."/game/shared",
        SDK_PATH.."/game/server",
        SDK_PATH.."/public",
        SDK_PATH.."/public/engine",
        SDK_PATH.."/public/mathlib",
        SDK_PATH.."/public/tier0",
        SDK_PATH.."/public/tier1",
        SDK_PATH.."/public/entity2",
        SDK_PATH.."/public/game/server",
        -- Metamod
        MM_PATH.."/core",
        MM_PATH.."/core/sourcehook",
    })

    if is_plat("windows") then
        add_defines({
            "COMPILER_MSVC",
            "COMPILER_MSVC64",
            "PLATFORM_64BITS",
            "WIN32",
            "WINDOWS",
            "CRT_SECURE_NO_WARNINGS",
            "CRT_SECURE_NO_DEPRECATE",
            "CRT_NONSTDC_NO_DEPRECATE",
            "_MBCS",
            "META_IS_SOURCE2"
        })
    else
        add_defines({
            "_LINUX",
            "LINUX",
            "POSIX",
            "GNUC",
            "COMPILER_GCC",
            "PLATFORM_64BITS",
            "META_IS_SOURCE2",
            "_GLIBCXX_USE_CXX11_ABI=1",
            "stricmp=strcasecmp",
            "_stricmp=strcasecmp",
            "_snprintf=snprintf",
            "_vsnprintf=vsnprintf"
        })
    end
