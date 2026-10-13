# The Arduino USB library's host MSC header includes FS.h. PlatformIO does
# not see that include through the TinyUSB guards, so the FS headers are
# missing when those files compile.
Import("env")
import os

fs = os.path.join(
    env.PioPlatform().get_package_dir("framework-arduinoespressif32"),
    "libraries",
    "FS",
    "src",
)
env.Append(CPPPATH=[fs])
