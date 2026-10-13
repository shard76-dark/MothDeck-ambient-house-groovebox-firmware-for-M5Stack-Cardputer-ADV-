# Compile MothDeck's advert builder and BLE MIDI parser into this
# firmware so the on-air bytes and the self-test use the same code.
Import("env")
import os

root = os.path.abspath(os.path.join(env.subst("$PROJECT_DIR"), "..", ".."))
env.Append(CPPPATH=[os.path.join(root, "include")])
env.BuildSources(
    os.path.join("$BUILD_DIR", "mothdeck"),
    os.path.join(root, "src"),
    src_filter="+<MidiProtocol.cpp> +<BleAdvert.cpp>",
)
