#!/usr/bin/env python3
"""End-to-end test: starts the simulator (time x8) and drives it over HTTP like a shortcut would."""
import json, os, subprocess, sys, tempfile, time, urllib.request, urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 18080
BASE = "http://127.0.0.1:%d" % PORT
TOKEN = "t" * 40
state = os.path.join(tempfile.mkdtemp(), "state.json")
proc = subprocess.Popen([sys.executable, os.path.join(HERE, "garage_door_simulator.py"),
                         "--port", str(PORT), "--host", "127.0.0.1", "--token", TOKEN,
                         "--speed", "8", "--state", state, "--fresh", "--quiet",
                         "--wifi-delay", "0.5"], stdout=subprocess.DEVNULL)
fails = []

def req(method, path, header=None, body=None, token=TOKEN, ctype="text/plain", timeout=5):
    h = {}
    if token is not None: h["X-Garage-Token"] = token
    if header is not None: h["X-Garage-Control"] = header
    if body is not None: h["Content-Type"] = ctype
    r = urllib.request.Request(BASE + path, method=method, headers=h,
                               data=None if body is None else body.encode())
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return resp.status, resp.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()

def cmd(name, **kw):
    return req("POST", "/api/door/" + name, name, name, **kw)

def status(): return json.loads(req("GET", "/api/status")[1])

def check(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name + (("  " + str(extra)) if (extra and not cond) else ""))
    if not cond: fails.append(name)

def wait(pred, secs=6):
    end = time.time() + secs
    while time.time() < end:
        try:
            if pred(status()): return True
        except (urllib.error.URLError, ConnectionError, OSError, ValueError):
            pass            # device offline (booting / Wi-Fi joining)
        time.sleep(0.1)
    return False

def settle(): return wait(lambda s: not s['flashMoving'] and not s['controlPending'], 8)

def sim(action, value=None, **kw):
    return req("POST", "/sim/api/action", body=json.dumps({"action": action, "value": value, **kw}), token=None)

try:
    for _ in range(50):
        try: urllib.request.urlopen(BASE + "/sim/api/state", timeout=1); break
        except Exception: time.sleep(0.1)
    # device is offline until Wi-Fi joins, like the real one
    t0 = time.time(); ok = wait(lambda s: True, 5); check("serves status after Wi-Fi joins", ok)

    page = req("GET", "/")[1]
    check("same page as firmware", page == open(os.path.join(HERE, "status_page.html")).read())
    check("404 for unknown path", req("GET", "/nope")[0] == 404)
    check("404 for GET on POST path", req("GET", "/api/door/open")[0] == 404)
    s = status()
    check("status fields", all(k in s for k in ["doorClosed","doorState","doorMoving","flashMoving","obstruction","controlPending","controlEnabled","doorChanges","lastChangeAgoS","uptimeMs","epoch","wifi","ssid","rssi","deviceName","ip","bootCount","resetReason","freeHeap","minFreeHeap","firmware"]))
    check("starts closed, control disabled", s["doorClosed"] and s["doorState"] == "Closed" and not s["controlEnabled"], s)

    check("401 without token", cmd("open", token=None)[0] == 401)
    check("401 wrong token", cmd("open", token="x")[0] == 401)
    check("403 control disabled", cmd("open")[0] == 403)
    check("400 wrong header", req("POST", "/api/door/open", "close", "open")[0] == 400)
    check("400 wrong body", req("POST", "/api/door/open", "open", "close")[0] == 400)
    check("400 form-encoded body (ESP32 has no 'plain')", req("POST", "/api/door/open", "open", "open", ctype="application/x-www-form-urlencoded")[0] == 400)
    check("enable control", req("POST", "/api/door/control", "control", "enable")[0] == 200 and status()["controlEnabled"])
    check("400 bad control value", req("POST", "/api/door/control", "control", "maybe")[0] == 400)

    # no-ops while closed
    c, b = cmd("close"); check("close when closed is a no-op", json.loads(b) == {"ok": True, "pressed": False, "reason": "already closed"}, b)
    c, b = cmd("stop"); check("stop when stopped is a no-op", json.loads(b)["reason"] == "already stopped", b)

    # open
    c, b = cmd("open"); check("open presses", c == 200 and json.loads(b) == {"ok": True, "pressed": True}, (c, b))
    check("busy during pulse/pending", req("POST", "/api/door/press", "press", "press")[0] in (409, 200))
    check("goes Opening", wait(lambda s: s["doorState"] == "Opening"))
    s = status(); check("motion reported while opening", s["flashMoving"] and s["doorMoving"], s)
    check("reed opens", wait(lambda s: not s["doorClosed"]))
    check("reaches Open", wait(lambda s: s["doorState"] == "Open", 8))
    check("motion stops", wait(lambda s: not s["flashMoving"]))
    c, b = cmd("open"); check("open when open is a no-op", json.loads(b)["reason"] == "already open", b)

    # close
    c, b = cmd("close"); check("close presses", c == 200 and json.loads(b)["pressed"] is True, b)
    check("goes Closing", wait(lambda s: s["doorState"] == "Closing"))
    check("reaches Closed", wait(lambda s: s["doorState"] == "Closed", 8))
    check("reed closed", status()["doorClosed"])
    check("changes counted", status()["doorChanges"] >= 2)

    # stop mid-travel, then continue in the reverse direction
    settle()
    c, b = cmd("open"); check("open accepted before stop test", c == 200, (c, b)); wait(lambda s: s["doorState"] == "Opening"); time.sleep(0.35)
    c, b = cmd("stop"); check("stop presses while opening", json.loads(b)["pressed"] is True, b)
    check("goes Stopped", wait(lambda s: s["doorState"] == "Stopped", 6))
    c, b = cmd("open"); check("open from stopped refused (next press would close)", json.loads(b)["ok"] is False and "opposite" in json.loads(b)["reason"], b)
    c, b = cmd("close"); check("close from stopped presses", json.loads(b)["pressed"] is True, b)
    check("closes fully", wait(lambda s: s["doorState"] == "Closed", 8))

    # obstruction while closing
    settle(); cmd("open"); check("open again", wait(lambda s: s["doorState"] == "Open", 8))
    sim("obstruction", None, enabled=True, position=0.5)
    cmd("close")
    check("obstruction -> not Closed", wait(lambda s: s["obstruction"] or s["doorState"] in ("Opening", "Stopped"), 8))
    time.sleep(3)
    s = status(); print("  state after obstruction:", s["doorState"], "obstruction:", s["obstruction"])
    sim("obstruction", None, enabled=False, position=0.5)

    # opener ignores a press -> "Opener did not start"
    wait(lambda s: not s["flashMoving"], 8); time.sleep(0.6)
    sim("set_position", 0.0)
    wait(lambda s: s["doorState"] == "Closed", 4)
    sim("ignore_next_press", True)
    cmd("open")
    check("start fails when opener ignores press", wait(lambda s: s["doorState"] == "Closed" and not s["controlPending"], 8))
    log = json.loads(req("GET", "/api/log?since=0")[1])
    check("log reports it", any("did not start" in e["m"] for e in log["entries"]), [e["m"] for e in log["entries"]][-6:])
    check("log since works", json.loads(req("GET", "/api/log?since=%d" % log["latest"])[1])["entries"] == [])

    # reboot: software reboot keeps the position of an open, stationary door
    settle(); cmd("open"); check("open", wait(lambda s: s["doorState"] == "Open", 8)); settle(); time.sleep(0.4)
    sim("reboot", "software")
    time.sleep(0.03)
    try:
        urllib.request.urlopen(BASE + "/api/status", timeout=1); offline = False
    except Exception: offline = True
    check("unreachable while rebooting", offline)
    check("back after reboot", wait(lambda s: True, 6))
    s = status(); check("software reboot reported", s["resetReason"] == "Software restart" and s["bootCount"] == 2, s)
    time.sleep(0.4)
    c, b = cmd("close"); check("close allowed after restored position", c == 200 and json.loads(b)["pressed"] is True, b)
    wait(lambda s: s["doorState"] == "Closed", 8); settle()
    # power-on reboot with the door open: direction unknown -> refused
    cmd("open"); wait(lambda s: s["doorState"] == "Open", 8); settle(); time.sleep(0.4)
    sim("reboot", "power"); time.sleep(0.03); wait(lambda s: True, 6); time.sleep(0.4)
    msgs = [e["m"] for e in json.loads(req("GET", "/api/log?since=0")[1])["entries"]]
    check("power-on reboot does not restore position", any("not restored: reset reason" in m for m in msgs), msgs)
    c, b = cmd("open"); check("power-on with open door: open is a no-op", json.loads(b)["reason"] == "already open", b)
    c, b = cmd("close"); check("power-on with open door: close allowed (4.2.0+ rule)", json.loads(b)["pressed"] is True, b)
    wait(lambda s: s["doorState"] == "Closed", 10); settle()
    check("raw press allowed", req("POST", "/api/door/press", "press", "press")[0] == 200)
    wait(lambda s: s["doorState"] == "Opening", 4); wait(lambda s: s["doorState"] == "Open", 10); settle()
    c, b = cmd("close"); wait(lambda s: s["doorState"] == "Closed", 10); settle()

    # wall button seen as movement
    sim("wall_button"); check("wall button seen as motion", wait(lambda s: s["flashMoving"], 4))
    wait(lambda s: not s["flashMoving"], 8)
    check("disable control", req("POST", "/api/door/control", "control", "disable")[0] == 200 and not status()["controlEnabled"])
    check("press forbidden when disabled", req("POST", "/api/door/press", "press", "press")[0] == 403)
finally:
    proc.terminate()
print("\n%d failure(s)" % len(fails))
sys.exit(1 if fails else 0)
