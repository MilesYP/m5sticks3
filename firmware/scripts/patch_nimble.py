# ─────────────────────────────────────────────────────────────────────────
#  PlatformIO 建置前腳本：修補 NimBLE-Arduino 2.5.0 的 deinit 競態（M4，31-m4d）
#
#  問題：NimBLEDevice::deinit() 呼叫 nimble_port_stop() 後立刻 nimble_port_deinit()；
#        但 host task 這時還在從 nimble_port_run() 返回、準備刪掉自己，兩邊同時收尾 →
#        Guru Meditation InstrFetchProhibited, PC=0（NimBLEDevice.cpp host_task）。
#  修補：stop 成功後先等 100ms，讓 host task 完全結束，再 deinit。
#  每次建置都會檢查；已修補就跳過；找不到要改的那段就讓建置失敗（避免函式庫升版後默默失效）。
# ─────────────────────────────────────────────────────────────────────────
Import("env")
import os

MARK = "m5sticks3 patch"
OLD = "        rc = nimble_port_stop();\n        if (rc == 0) {\n"
NEW = OLD + "            vTaskDelay(pdMS_TO_TICKS(100));  // " + MARK + ": wait for host task to exit before deinit\n"

libdeps = env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV")
path = os.path.join(libdeps, "NimBLE-Arduino", "src", "NimBLEDevice.cpp")
if not os.path.isfile(path):
    print("[patch_nimble] 還沒下載 NimBLE-Arduino，略過（下載後下一次建置會修補）")
else:
    with open(path, encoding="utf-8") as f:
        src = f.read()
    if MARK in src:
        print("[patch_nimble] NimBLEDevice.cpp 已修補")
    elif OLD in src:
        with open(path, "w", encoding="utf-8") as f:
            f.write(src.replace(OLD, NEW, 1))
        print("[patch_nimble] 已修補 NimBLEDevice::deinit（stop 後等 100ms）")
    else:
        raise SystemExit("[patch_nimble] 找不到要修補的程式碼（NimBLE 版本變了？）— 先看 scripts/patch_nimble.py")
