#!/usr/bin/env python3
"""
basler_configure.py — Interactive camera configurator using pypylon

Opens the camera with pypylon, lets you inspect and set any GenICam parameter,
then saves the full configuration to a Pylon feature set file (.pfs).

That .pfs file can be loaded by BaslerCameraThread via:
    ctx.feature_file = "camera.pfs"

Usage:
    python3 basler_configure.py [options]

    --list               List all connected cameras and exit
    --serial SN          Use camera with this serial number (default: first available)
    --load PATH          Load existing .pfs before entering interactive mode
    --save PATH          Where to save the configuration (default: camera.pfs)
    --set KEY=VAL        Set a GenICam parameter non-interactively (repeatable)
    --get KEY            Print a GenICam parameter value and exit
    --dump               Print all readable parameters (tree view) and exit

Requirements:
    pip install pypylon --break-system-packages

Test without hardware:
    PYLON_CAMEMU=1 python3 basler_configure.py
"""

import sys
import argparse

try:
    from pypylon import pylon, genicam
except ImportError:
    print("pypylon is not installed.")
    print("Install it with:  pip install pypylon --break-system-packages")
    sys.exit(1)


# ── Camera open/list ──────────────────────────────────────────────────────────

def list_cameras():
    tl = pylon.TlFactory.GetInstance()
    devices = tl.EnumerateDevices()
    if not devices:
        print("No Basler cameras found.")
        return
    print(f"{len(devices)} camera(s) found:\n")
    for i, d in enumerate(devices):
        print(f"  [{i}]  {d.GetModelName():<30}  serial={d.GetSerialNumber()}")


def open_camera(serial: str) -> pylon.InstantCamera:
    tl = pylon.TlFactory.GetInstance()
    if serial:
        info = pylon.DeviceInfo()
        info.SetSerialNumber(serial)
        dev = tl.CreateDevice(info)
    else:
        dev = tl.CreateFirstDevice()
    cam = pylon.InstantCamera(dev)
    cam.Open()
    return cam


# ── GenICam parameter access ──────────────────────────────────────────────────
# In pypylon, camera features are accessed via camera attributes:
#   getattr(cam, "ExposureTime").GetValue() / .SetValue(val)
# The nodemap object (cam.GetNodeMap()) is only used for category traversal.

def get_param(cam, key: str) -> str:
    try:
        return str(getattr(cam, key).GetValue())
    except AttributeError:
        return "<not found>"
    except Exception as e:
        return f"<{e}>"


def set_param(cam, key: str, value: str):
    try:
        feature = getattr(cam, key)
        cur     = feature.GetValue()
        # Convert string to the node's native Python type
        if isinstance(cur, bool):
            feature.SetValue(value.lower() in ("true", "1", "yes"))
        elif isinstance(cur, int):
            feature.SetValue(int(float(value)))
        elif isinstance(cur, float):
            feature.SetValue(float(value))
        else:
            feature.SetValue(value)         # enum / string node
        print(f"  {key} = {feature.GetValue()}")
    except AttributeError:
        print(f"  [!] '{key}' not found")
    except Exception as e:
        print(f"  [!] Cannot set '{key}' to '{value}': {e}")


def dump_params(cam):
    """Print all readable parameters as a category tree."""
    nodemap = cam.GetNodeMap()
    visited = set()

    def recurse(node, indent=0):
        if node is None:
            return
        try:
            name = node.GetName()
        except Exception:
            return
        if name in visited:
            return
        visited.add(name)

        if genicam.IsCategory(node):
            print(f"{'  ' * indent}[{name}]")
            try:
                for child in genicam.CCategoryPtr(node).GetFeatures():
                    recurse(child, indent + 1)
            except Exception:
                pass
        elif genicam.IsReadable(node):
            val = get_param(cam, name)
            if not val.startswith("<"):
                print(f"{'  ' * indent}{name} = {val}")

    recurse(nodemap.GetNode("Root"))


# ── Interactive shell ─────────────────────────────────────────────────────────

def interactive(cam):
    print("\nInteractive mode. Commands:")
    print("  get KEY        — read a GenICam parameter")
    print("  set KEY VALUE  — write a GenICam parameter")
    print("  dump           — print all readable parameters")
    print("  quit / exit    — leave interactive mode")
    print()
    while True:
        try:
            line = input("basler> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if not line:
            continue
        parts = line.split(None, 2)
        cmd = parts[0].lower()
        if cmd in ("quit", "exit", "q"):
            break
        elif cmd == "get" and len(parts) >= 2:
            print(f"  {parts[1]} = {get_param(cam, parts[1])}")
        elif cmd == "set" and len(parts) >= 3:
            set_param(cam, parts[1], parts[2])
        elif cmd == "dump":
            dump_params(cam)
        else:
            print("  Unknown command. Try: get KEY | set KEY VALUE | dump | quit")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(
        description="Basler camera interactive configurator (pypylon)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--list",   action="store_true", help="List cameras and exit")
    p.add_argument("--serial", default="", help="Camera serial (default: first available)")
    p.add_argument("--load",   default="", metavar="PATH",
                   help="Load .pfs file before interactive mode")
    p.add_argument("--save",   default="camera.pfs", metavar="PATH",
                   help="Save configuration after interactive mode (default: camera.pfs)")
    p.add_argument("--set",    action="append", default=[], metavar="KEY=VALUE",
                   help="Set a parameter non-interactively (repeatable)")
    p.add_argument("--get",    default="", metavar="KEY",
                   help="Print a single parameter value and exit")
    p.add_argument("--dump",   action="store_true",
                   help="Print all readable parameters and exit")
    args = p.parse_args()

    if args.list:
        list_cameras()
        sys.exit(0)

    cam = open_camera(args.serial)
    print(f"Opened: {cam.GetDeviceInfo().GetModelName()}  "
          f"serial={cam.GetDeviceInfo().GetSerialNumber()}")

    if args.load:
        try:
            pylon.FeaturePersistence.Load(args.load, cam.GetNodeMap(), True)
            print(f"Loaded: {args.load}")
        except Exception as e:
            print(f"[!] Cannot load '{args.load}': {e}")

    # Non-interactive --set flags
    for kv in args.set:
        if "=" not in kv:
            print(f"[!] --set expects KEY=VALUE, got: {kv}")
            continue
        k, v = kv.split("=", 1)
        set_param(cam, k.strip(), v.strip())

    if args.get:
        print(f"{args.get} = {get_param(cam, args.get)}")
        cam.Close()
        sys.exit(0)

    if args.dump:
        dump_params(cam)
        cam.Close()
        sys.exit(0)

    # Quick status of common parameters
    for key in ("ExposureAuto", "ExposureTime", "GainAuto", "Gain",
                "PixelFormat", "Width", "Height", "AcquisitionFrameRate"):
        val = get_param(cam, key)
        if not val.startswith("<"):
            print(f"  {key:30s} = {val}")

    if sys.stdin.isatty():
        interactive(cam)

    # Save
    try:
        pylon.FeaturePersistence.Save(args.save, cam.GetNodeMap())
        print(f"\nSaved: {args.save}")
        print(f"\nTo use in basler_dump.py:")
        print(f"  python3 basler_dump.py --feature-file {args.save}")
    except Exception as e:
        print(f"[!] Cannot save '{args.save}': {e}")

    cam.Close()


if __name__ == "__main__":
    main()
