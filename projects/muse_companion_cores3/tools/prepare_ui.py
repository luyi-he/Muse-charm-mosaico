"""Build the CoreS3 320x240 scenes from the original, preserving GSP bindings."""
import json
from pathlib import Path
from PIL import Image

project = Path(__file__).resolve().parents[1]
source = project.parent / "muse_companion" / "ui"
output = project / "ui"
(output / "assets").mkdir(parents=True, exist_ok=True)

def scale(value, factor):
    if isinstance(value, dict):
        return {key: scale(v, factor) if key in ("default", "min", "max") else v
                for key, v in value.items()}
    return round(value * factor)

poses = {"idle", "listening", "thinking", "speaking", "sleepy", "error", "happy", "boot"}
for scene in ("main", "settings"):
    data = json.loads((source / f"{scene}.json").read_text(encoding="utf-8"))
    data.update(w=320, h=240, default_font_size=12,
                font="../../muse_companion/ui/fonts/NotoSansSC-Regular.ttf")
    for obj in data["objects"]:
        for key in ("x", "w"):
            if key in obj: obj[key] = scale(obj[key], 2 / 3)
        for key in ("y", "h", "radius", "font_size"):
            if key in obj: obj[key] = scale(obj[key], .5)
        if "font_size" in obj: obj["font_size"] = max(12, obj["font_size"])
        if "font_charset" in obj: obj["font_charset"] += "电脑桥程启动序十太短超二序百USB连接说明"
        name = obj.get("name", "")
        if name in poses:
            obj.update(x={"property": "x", "default": 85, "min": 77, "max": 93},
                       y={"property": "y", "default": 30, "min": 14, "max": 36}, w=150, h=150)
        if name == "pet": obj.update(x=100, y=40, w=120, h=120)
        if name == "talk": obj.update(x=52, y=208, w=216, h=32, bg_color="#D0EBDD", fg_color="#211C19")
        if name == "talk_hint": obj.update(x=52, y=214, w=216, h=18, fg_color="#211C19")
        if name == "hint": obj.update(hidden=True)
        if name == "reply_card": obj.update(x=8, y=173, w=304, h=34)
        if name == "caption": obj.update(x=10, y=173, w=300, h=34)
        if name == "reply_text": obj.update(x=12, y=175, w=296, h=15)
        if name == "reply_text_2": obj.update(x=12, y=191, w=296, h=15)
        if name == "connection": obj.update(x=10, y=4, w=206, h=20)
        if name == "battery": obj.update(y=9, h=18)
        if name in ("mute", "settings_open"): obj["h"] = 32
        if name.startswith("heart_"):
            obj["y"] = {"property": "y", "default": 40, "min": 24, "max": 50}
        if scene == "settings":
            if name == "sleep": obj.update(hidden=True)
            if name == "sleep_text": obj["font_charset"] += "常"
            rows = ("volume", "brightness", "language", "sleep", "rate", "page")
            for index, row in enumerate(rows):
                if name == row or name.startswith(row + "_"):
                    obj.update(y=34 + index * 25, h=24)
            if name in ("title", "settings_back", "back_label"): obj.update(y=3, h=26)
            if name == "pair_status": obj.update(y=187, h=18)
            if name == "phone_setup": obj.update(x=84, y=209, w=152, h=30)
        if "image" in obj:
            path = obj["image"]
            image = Image.open(source / path).convert("RGBA")
            image.resize((obj["w"], obj["h"]), Image.Resampling.LANCZOS).save(output / path)
    (output / f"{scene}.json").write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
(output / "companion_rgb565.yaml").write_text(
    "name: muse_companion_cores3_rgb565\npixel_format: rgb565\nresource_alignment: 64\n"
    "stride_alignment: 64\nanimation_max_players: 8\nanimation_frame_budget_bytes: 2097152\n",
    encoding="utf-8", newline="\n")
print("CoreS3 scenes generated: 320x240")
