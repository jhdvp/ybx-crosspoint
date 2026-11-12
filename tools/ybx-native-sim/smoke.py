#!/usr/bin/env python3
"""Read-only smoke checks for the native FreeInkUI preview HTTP server."""
import base64
from io import BytesIO
import json
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import urlopen

from PIL import Image, ImageChops

BASE = "http://127.0.0.1:8768"
# Pass YBX_SIM_URL if server binds only to the Mac LAN address.
import os
BASE = os.environ.get("YBX_SIM_URL", BASE).rstrip("/")

def frame(**kwargs):
    url = BASE + "/frame?" + urlencode(kwargs)
    with urlopen(url, timeout=8) as res:
        assert res.status == 200
        payload = json.load(res)
    raw = base64.b64decode(payload["image"].split(",", 1)[1])
    im = Image.open(BytesIO(raw)).convert("L")
    return payload, im

def run():
    count = 0
    for model in ("x3", "x4", "x4pro"):
        w,h = (528,792) if model=="x4pro" else (480,800)
        for screen in ("sources", "pair", "settings"):
            images = {}
            for orientation in ("portrait", "portrait-inverted",
                                "landscape", "landscape-inverted"):
                payload,img = frame(model=model,screen=screen,
                                    orientation=orientation,scenario="linked")
                expected=(w,h) if orientation.startswith("portrait") else (h,w)
                assert img.size==expected,(model,screen,orientation,img.size,expected)
                assert img.getextrema()==(0,255),(model,screen,orientation)
                assert payload["hits"],(model,screen,orientation)
                for hit in payload["hits"]:
                    assert 0 <= hit["x"] < expected[0] and 0 <= hit["y"] < expected[1]
                    assert hit["x"]+hit["w"] <= expected[0]+1
                    assert hit["y"]+hit["h"] <= expected[1]+1
                images[orientation] = img
                count += 1
            # Inverted views are correctly rotated into logical upright
            # coordinates by FreeInkUI/DisplayTarget, not merely relabelled.
            for a,b in (("portrait","portrait-inverted"),
                        ("landscape","landscape-inverted")):
                assert ImageChops.difference(images[a],images[b]).getbbox() is None, (model,screen,a,b)
    for scenario in ("linked","all","empty","offline","unpaired","removed",
                     "expired","provider","loading","codeExpired"):
        frame(model="x4pro",screen="sources",scenario=scenario,orientation="portrait")
        count += 1
    print(f"PASS {count} frames, native rotations, 1-bit content, hitbox geometry")

if __name__=="__main__":
    run()
