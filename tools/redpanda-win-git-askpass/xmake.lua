target("redpanda-win-git-askpass")
    set_kind("binary")

    add_files("main.cpp")
    add_defines("UNICODE", "_UNICODE")
    if is_plat("mingw") then
        add_ldflags("-municode", {force = true})
    end

    add_files(
        -- "redpanda-git-askpass_private.rc",  -- drives xmake crazy
        "resource.rc")

    add_links("user32", "shell32")

    set_install_libexec()
