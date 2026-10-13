Import("env")
import os

root = os.path.abspath(os.path.join(env.subst("$PROJECT_DIR"), "..", ".."))
env.Append(CPPPATH=[os.path.join(root, "include")])
env.BuildSources(
    os.path.join("$BUILD_DIR", "shared"),
    os.path.join(root, "src"),
    src_filter="+<MothCrc.cpp> +<SdPattern.cpp> +<FatPlan.cpp> +<SdLimit.cpp> +<MothBootJump.cpp>",
)
