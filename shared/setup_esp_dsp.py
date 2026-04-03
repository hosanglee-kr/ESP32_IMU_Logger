import os
import shutil
from os.path import join, isdir, exists

Import("env")

# 경로 설정
lib_deps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
board_env = env.subst("$PIOENV")
esp_dsp_src = join(lib_deps_dir, board_env, "esp-dsp")
target_lib_dir = join(env.subst("$PROJECT_DIR"), "lib", "esp-dsp")

def filter_esp_dsp(source, target, env):
    if not exists(esp_dsp_src):
        print(f"--> [esp-dsp] Source not found at {esp_dsp_src}. Waiting for download...")
        return

    # 이미 복사되어 있다면 스킵 (강제 업데이트 시 lib/esp-dsp 삭제 필요)
    if exists(target_lib_dir):
        return

    print(f"--> [esp-dsp] Cleaning up and copying modules to {target_lib_dir}")
    os.makedirs(target_lib_dir, exist_ok=True)

    # modules 폴더 내부 내용물만 복사 (에러 유발하는 applications, test 제외)
    modules_path = join(esp_dsp_src, "modules")
    if exists(modules_path):
        for item in os.listdir(modules_path):
            s = join(modules_path, item)
            d = join(target_lib_dir, item)
            if isdir(s):
                shutil.copytree(s, d, dirs_exist_ok=True)
            else:
                shutil.copy2(s, d)
        print("--> [esp-dsp] Successfully isolated modules!")

# 빌드 전 실행 (Pre-action)
env.AddPreAction("buildprog", filter_esp_dsp)

