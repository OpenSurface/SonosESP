#
# use_ccache.py — speed up compilation by routing the C/C++ compiler through ccache.
#
# OPT-IN and GRACEFUL: only activates when the environment variable USE_CCACHE=1
# is set (CI does this) AND ccache is actually on PATH. Local/normal builds are
# completely unaffected (the script is a no-op), so firmware output is unchanged.
#
# CI installs + caches ccache via hendrikmuhs/ccache-action and exports USE_CCACHE=1,
# turning a full ~2-min recompile into a fast cache-hit build.
#
import os
import shutil

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

if os.environ.get("USE_CCACHE") == "1":
    ccache = shutil.which("ccache")
    if ccache:
        env.Replace(
            CC="%s %s" % (ccache, env["CC"]),
            CXX="%s %s" % (ccache, env["CXX"]),
        )
        print("[use_ccache] ccache enabled for env '%s'" % env.get("PIOENV", "?"))
    else:
        print("[use_ccache] USE_CCACHE=1 but ccache not found on PATH — skipping")
