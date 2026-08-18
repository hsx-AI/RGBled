"""Keep the linker response file free of non-ASCII paths.

Once FastLED joins the link the command line exceeds the Windows limit, so SCons
moves it into a response file that it writes as UTF-8 while ld reads it in the
system ANSI code page. Any absolute path containing non-ASCII characters (such as
a project stored under a Chinese folder name) is mangled and the link fails with
"cannot open map file". Rewriting the only such flag as a path relative to the
project directory keeps the map file exactly where PlatformIO expects it.
"""

import os

Import("env")

build_dir = env.subst("$BUILD_DIR")
project_dir = env.subst("$PROJECT_DIR")
progname = env.subst("$PROGNAME")

try:
    rel_build = os.path.relpath(build_dir, project_dir).replace("\\", "/")
except ValueError:  # different drives, nothing we can do
    rel_build = None

if rel_build and not rel_build.startswith(".."):
    kept = [f for f in env["LINKFLAGS"] if "-Map" not in str(f)]
    kept.append('-Wl,-Map="%s/%s.map"' % (rel_build, progname))
    env.Replace(LINKFLAGS=kept)
