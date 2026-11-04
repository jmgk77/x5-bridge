import os
import shutil

Import("env")


def copy_firmware(source, target, env):
    project_dir = env.get("PROJECT_DIR")
    src = target[0].path
    dst = os.path.join(project_dir, os.path.basename(src))

    shutil.copy(src, dst)
    print("Copied {} to {}".format(src, dst))

    gz_src = src + ".gz"
    if os.path.exists(gz_src):
        gz_dst = dst + ".gz"
        shutil.copy(gz_src, gz_dst)
        print("Copied {} to {}".format(gz_src, gz_dst))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_firmware)
