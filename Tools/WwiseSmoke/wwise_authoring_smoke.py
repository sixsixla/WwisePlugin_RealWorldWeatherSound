#!/usr/bin/env python3
"""WAAPI smoke test for the RealWorld Weather Acoustics Authoring plug-in.

This is test tooling. It intentionally depends on the third-party ``waapi-client``
Python package, but the product build and plug-in do not.
"""

from __future__ import print_function

import argparse
import ctypes
import hashlib
import json
import math
import os
import struct
import sys
import time
import traceback
from ctypes import wintypes
from datetime import datetime, timezone
from pathlib import Path

from waapi import CannotConnectToWaapiException, WaapiClient


SMOKE_NAME = "RWWA_Smoke"
SOURCE_NAME = "RWWA_Smoke_Source"
DEFAULT_SOURCE_CLASS_ID = 2031682562
PROJECT_INFO_URI = "ak.wwise.core.getProjectInfo"
UNDO_URI = "ak.wwise.core.undo.undo"
CANVAS_CLASS_NAME = "RealWorldWeatherAcousticsPreviewCanvas"
CANVAS_CONTROL_ID = 2000
DURATION_CONTROL_ID = 2100
RAIN_INTENSITY_CONTROL_ID = 2102
SEED_CONTROL_ID = 2103
GEOMETRY_ENABLED_CONTROL_ID = 2104
LISTENER_YAW_CONTROL_ID = 2108
FEATURE_COUNT_CONTROL_ID = 2109
WIND_SPEED_CONTROL_ID = 2110
WIND_DIRECTION_CONTROL_ID = 2111
WIND_GUSTINESS_CONTROL_ID = 2112
FEATURE_X_CONTROL_ID = 2200
FEATURE_Z_CONTROL_ID = 2202
FEATURE_RADIUS_CONTROL_ID = 2203
FEATURE_MASK_CONTROL_ID = 2205
FEATURE_PRIORITY_CONTROL_ID = 2206
ADD_FEATURE_CONTROL_ID = 2600
DELETE_FEATURE_CONTROL_ID = 2601
FEATURE_PROPERTY_SUFFIXES = ("X", "Y", "Z", "Radius", "Profile", "Mask", "Priority")
SOURCE_BANK_PARAMETER_BYTES = 273
SOURCE_BANK_PROPERTY_COUNT = 69
GEOMETRY_ENABLED_BANK_OFFSET = 16
SOUNDBANK_EVENT_NAME = "RWWA_Smoke_Bank_Event"
SOUNDBANK_TRUE_NAME = "RWWA_GeometryTrue"
SOUNDBANK_FALSE_NAME = "RWWA_GeometryFalse"

BM_GETCHECK = 0x00F0
BM_CLICK = 0x00F5
BST_CHECKED = 0x0001
WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
WM_KEYDOWN = 0x0100
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MOUSEMOVE = 0x0200
MK_LBUTTON = 0x0001
VK_DELETE = 0x002E
SMTO_BLOCK = 0x0001
SMTO_ABORTIFHUNG = 0x0002


class SmokeAssertionError(RuntimeError):
    pass


class Rect(ctypes.Structure):
    _fields_ = [
        ("left", wintypes.LONG),
        ("top", wintypes.LONG),
        ("right", wintypes.LONG),
        ("bottom", wintypes.LONG),
    ]


def configure_user32():
    if os.name != "nt":
        raise RuntimeError("The Wwise Authoring GUI smoke test requires Windows.")

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    window_enum_proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [window_enum_proc, wintypes.LPARAM]
    user32.EnumWindows.restype = wintypes.BOOL
    user32.EnumChildWindows.argtypes = [wintypes.HWND, window_enum_proc, wintypes.LPARAM]
    user32.EnumChildWindows.restype = wintypes.BOOL
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetClassNameW.restype = ctypes.c_int
    user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
    user32.GetWindowTextLengthW.restype = ctypes.c_int
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.restype = ctypes.c_int
    user32.SendMessageW.argtypes = [
        wintypes.HWND,
        wintypes.UINT,
        wintypes.WPARAM,
        ctypes.c_void_p,
    ]
    user32.SendMessageW.restype = wintypes.LPARAM
    user32.GetDlgCtrlID.argtypes = [wintypes.HWND]
    user32.GetDlgCtrlID.restype = ctypes.c_int
    user32.GetParent.argtypes = [wintypes.HWND]
    user32.GetParent.restype = wintypes.HWND
    user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(Rect)]
    user32.GetWindowRect.restype = wintypes.BOOL
    user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(Rect)]
    user32.GetClientRect.restype = wintypes.BOOL
    user32.IsWindow.argtypes = [wintypes.HWND]
    user32.IsWindow.restype = wintypes.BOOL
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.IsWindowVisible.restype = wintypes.BOOL
    user32.IsWindowEnabled.argtypes = [wintypes.HWND]
    user32.IsWindowEnabled.restype = wintypes.BOOL
    user32.SendMessageTimeoutW.argtypes = [
        wintypes.HWND,
        wintypes.UINT,
        wintypes.WPARAM,
        wintypes.LPARAM,
        wintypes.UINT,
        wintypes.UINT,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    user32.SendMessageTimeoutW.restype = wintypes.LPARAM
    return user32, window_enum_proc


def window_class_name(user32, hwnd):
    buffer = ctypes.create_unicode_buffer(256)
    if user32.GetClassNameW(hwnd, buffer, len(buffer)) <= 0:
        return ""
    return buffer.value


def window_text(user32, hwnd):
    # GetWindowTextW cannot retrieve Edit text across process boundaries.
    # Standard Win32 controls marshal WM_GETTEXT for us, so use that path for
    # the Wwise process and keep the visual inspector assertion meaningful.
    length = max(0, int(user32.SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, None)))
    buffer = ctypes.create_unicode_buffer(length + 1)
    user32.SendMessageW(
        hwnd,
        WM_GETTEXT,
        len(buffer),
        ctypes.cast(buffer, ctypes.c_void_p),
    )
    return buffer.value


def window_process_id(user32, hwnd):
    process_id = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(process_id))
    return int(process_id.value)


def enumerate_process_windows(user32, window_enum_proc, expected_pid=None):
    handles = []

    @window_enum_proc
    def collect_top_level(hwnd, _):
        if expected_pid is None or window_process_id(user32, hwnd) == expected_pid:
            handles.append(int(hwnd))
        return True

    if not user32.EnumWindows(collect_top_level, 0):
        raise ctypes.WinError(ctypes.get_last_error())

    top_level_handles = list(handles)
    for top_level in top_level_handles:
        @window_enum_proc
        def collect_child(hwnd, _):
            if expected_pid is None or window_process_id(user32, hwnd) == expected_pid:
                handles.append(int(hwnd))
            return True

        if not user32.EnumChildWindows(top_level, collect_child, 0):
            error_code = ctypes.get_last_error()
            if error_code:
                raise ctypes.WinError(error_code)
    return handles


def find_preview_controls(expected_pid, timeout_seconds):
    user32, window_enum_proc = configure_user32()
    deadline = time.monotonic() + timeout_seconds
    last_inventory = []
    while time.monotonic() < deadline:
        handles = enumerate_process_windows(user32, window_enum_proc, expected_pid)
        inventory = []
        candidates = {}
        for hwnd in handles:
            class_name = window_class_name(user32, hwnd)
            control_id = int(user32.GetDlgCtrlID(hwnd))
            parent = int(user32.GetParent(hwnd) or 0)
            process_id = window_process_id(user32, hwnd)
            if class_name == CANVAS_CLASS_NAME or control_id in (
                CANVAS_CONTROL_ID,
                GEOMETRY_ENABLED_CONTROL_ID,
                ADD_FEATURE_CONTROL_ID,
                DELETE_FEATURE_CONTROL_ID,
            ):
                inventory.append(
                    {
                        "handle": hwnd,
                        "parentHandle": parent,
                        "processId": process_id,
                        "className": class_name,
                        "controlId": control_id,
                    }
                )
            candidates[(parent, control_id)] = hwnd

        last_inventory = inventory
        for item in inventory:
            if (
                item["className"] == CANVAS_CLASS_NAME
                and item["controlId"] == CANVAS_CONTROL_ID
            ):
                parent = item["parentHandle"]
                geometry_checkbox = candidates.get(
                    (parent, GEOMETRY_ENABLED_CONTROL_ID)
                )
                inspector_ids = {
                    "durationEditHandle": DURATION_CONTROL_ID,
                    "rainIntensityEditHandle": RAIN_INTENSITY_CONTROL_ID,
                    "seedEditHandle": SEED_CONTROL_ID,
                    "listenerYawEditHandle": LISTENER_YAW_CONTROL_ID,
                    "featureCountEditHandle": FEATURE_COUNT_CONTROL_ID,
                    "windSpeedEditHandle": WIND_SPEED_CONTROL_ID,
                    "windDirectionEditHandle": WIND_DIRECTION_CONTROL_ID,
                    "windGustinessEditHandle": WIND_GUSTINESS_CONTROL_ID,
                    "featureXEditHandle": FEATURE_X_CONTROL_ID,
                    "featureZEditHandle": FEATURE_Z_CONTROL_ID,
                    "featureRadiusEditHandle": FEATURE_RADIUS_CONTROL_ID,
                    "featureMaskEditHandle": FEATURE_MASK_CONTROL_ID,
                    "featurePriorityEditHandle": FEATURE_PRIORITY_CONTROL_ID,
                }
                inspector_handles = {
                    name: candidates.get((parent, control_id))
                    for name, control_id in inspector_ids.items()
                }
                add_button = candidates.get((parent, ADD_FEATURE_CONTROL_ID))
                delete_button = candidates.get((parent, DELETE_FEATURE_CONTROL_ID))
                if (
                    geometry_checkbox
                    and add_button
                    and delete_button
                    and all(inspector_handles.values())
                ):
                    result = {
                        "processId": item["processId"],
                        "dialogHandle": parent,
                        "canvasHandle": item["handle"],
                        "geometryCheckboxHandle": geometry_checkbox,
                        "addButtonHandle": add_button,
                        "deleteButtonHandle": delete_button,
                        "inventory": inventory,
                    }
                    result.update(inspector_handles)
                    return user32, result
        time.sleep(0.1)
    raise RuntimeError(
        "Could not find the RWWA preview canvas and sibling Add/Delete controls within "
        "{0:.1f}s for Wwise PID {1}. Last matching window inventory: {2}".format(
            timeout_seconds, expected_pid, last_inventory
        )
    )


def send_message(user32, hwnd, message, w_param=0, l_param=0, timeout_ms=5000):
    if not user32.IsWindow(hwnd):
        raise RuntimeError("Win32 control handle is no longer valid: {0}".format(hwnd))
    result = ctypes.c_size_t()
    ctypes.set_last_error(0)
    delivered = user32.SendMessageTimeoutW(
        hwnd,
        message,
        w_param,
        l_param,
        SMTO_BLOCK | SMTO_ABORTIFHUNG,
        timeout_ms,
        ctypes.byref(result),
    )
    if not delivered:
        error_code = ctypes.get_last_error()
        if error_code:
            raise ctypes.WinError(error_code)
        raise RuntimeError(
            "SendMessageTimeoutW timed out for hwnd={0}, message=0x{1:04X}.".format(
                hwnd, message
            )
        )
    return int(result.value)


def make_mouse_lparam(x_value, y_value):
    return ((int(y_value) & 0xFFFF) << 16) | (int(x_value) & 0xFFFF)


def cpp_lround(value):
    return int(math.floor(value + 0.5)) if value >= 0.0 else int(math.ceil(value - 0.5))


def rect_to_dict(rect):
    return {
        "left": int(rect.left),
        "top": int(rect.top),
        "right": int(rect.right),
        "bottom": int(rect.bottom),
        "width": int(rect.right - rect.left),
        "height": int(rect.bottom - rect.top),
    }


def read_window_rect(user32, hwnd):
    rect = Rect()
    if not user32.GetWindowRect(hwnd, ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    return rect_to_dict(rect)


def read_client_rect(user32, hwnd):
    rect = Rect()
    if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    return rect_to_dict(rect)


def rect_is_nonzero(rect):
    return rect["width"] > 0 and rect["height"] > 0


def inspect_control(user32, hwnd):
    if not user32.IsWindow(hwnd):
        raise RuntimeError("Win32 control handle is no longer valid: {0}".format(hwnd))
    return {
        "handle": hwnd,
        "processId": window_process_id(user32, hwnd),
        "parentHandle": int(user32.GetParent(hwnd) or 0),
        "className": window_class_name(user32, hwnd),
        "controlId": int(user32.GetDlgCtrlID(hwnd)),
        "text": window_text(user32, hwnd),
        "isWindow": True,
        "isWindowVisible": bool(user32.IsWindowVisible(hwnd)),
        "isWindowEnabled": bool(user32.IsWindowEnabled(hwnd)),
        "windowRect": read_window_rect(user32, hwnd),
        "clientRect": read_client_rect(user32, hwnd),
    }


def assert_preview_control_state(user32, controls, expected_pid, timeout_seconds=0.0):
    deadline = time.monotonic() + max(0.0, timeout_seconds)
    while True:
        states = {
            "dialog": inspect_control(user32, controls["dialogHandle"]),
            "canvas": inspect_control(user32, controls["canvasHandle"]),
            "geometryCheckbox": inspect_control(
                user32, controls["geometryCheckboxHandle"]
            ),
            "addButton": inspect_control(user32, controls["addButtonHandle"]),
            "deleteButton": inspect_control(user32, controls["deleteButtonHandle"]),
            "duration": inspect_control(user32, controls["durationEditHandle"]),
            "rainIntensity": inspect_control(
                user32, controls["rainIntensityEditHandle"]
            ),
            "seed": inspect_control(user32, controls["seedEditHandle"]),
            "listenerYaw": inspect_control(user32, controls["listenerYawEditHandle"]),
            "featureCount": inspect_control(
                user32, controls["featureCountEditHandle"]
            ),
            "windSpeed": inspect_control(user32, controls["windSpeedEditHandle"]),
            "windDirection": inspect_control(
                user32, controls["windDirectionEditHandle"]
            ),
            "windGustiness": inspect_control(
                user32, controls["windGustinessEditHandle"]
            ),
            "featureX": inspect_control(user32, controls["featureXEditHandle"]),
            "featureZ": inspect_control(user32, controls["featureZEditHandle"]),
            "featureRadius": inspect_control(
                user32, controls["featureRadiusEditHandle"]
            ),
            "featureMask": inspect_control(user32, controls["featureMaskEditHandle"]),
            "featurePriority": inspect_control(
                user32, controls["featurePriorityEditHandle"]
            ),
        }
        expected_inspector_text = {
            "duration": "60",
            "rainIntensity": "0.75",
            "seed": "24681357",
            "listenerYaw": "20",
            "featureCount": "4 / 8",
            "windSpeed": "14",
            "windDirection": "35",
            "windGustiness": "0.65",
            "featureX": "0",
            "featureZ": "6",
            "featureRadius": "2.5",
            "featureMask": "3",
            "featurePriority": "10",
        }
        assertions = {
            "allControlsBelongToExpectedProcess": all(
                state["processId"] == expected_pid for state in states.values()
            ),
            "dialogIsVisible": states["dialog"]["isWindowVisible"],
            "dialogWindowRectIsNonzero": rect_is_nonzero(states["dialog"]["windowRect"]),
            "dialogClientRectIsNonzero": rect_is_nonzero(states["dialog"]["clientRect"]),
            "canvasIsVisible": states["canvas"]["isWindowVisible"],
            "canvasWindowRectIsNonzero": rect_is_nonzero(states["canvas"]["windowRect"]),
            "canvasClientRectIsNonzero": rect_is_nonzero(states["canvas"]["clientRect"]),
            "geometryCheckboxIsVisible": states["geometryCheckbox"]["isWindowVisible"],
            "geometryCheckboxIsEnabled": states["geometryCheckbox"]["isWindowEnabled"],
            "geometryCheckboxWindowRectIsNonzero": rect_is_nonzero(
                states["geometryCheckbox"]["windowRect"]
            ),
            "geometryCheckboxMatchesProperty": send_message(
                user32, controls["geometryCheckboxHandle"], BM_GETCHECK
            )
            == BST_CHECKED,
            "addButtonIsVisible": states["addButton"]["isWindowVisible"],
            "addButtonIsEnabled": states["addButton"]["isWindowEnabled"],
            "addButtonWindowRectIsNonzero": rect_is_nonzero(
                states["addButton"]["windowRect"]
            ),
            "addButtonClientRectIsNonzero": rect_is_nonzero(
                states["addButton"]["clientRect"]
            ),
            "deleteButtonIsVisible": states["deleteButton"]["isWindowVisible"],
            "deleteButtonIsEnabled": states["deleteButton"]["isWindowEnabled"],
            "deleteButtonWindowRectIsNonzero": rect_is_nonzero(
                states["deleteButton"]["windowRect"]
            ),
            "deleteButtonClientRectIsNonzero": rect_is_nonzero(
                states["deleteButton"]["clientRect"]
            ),
            "inspectorTextMatchesStablePreset": all(
                states[name]["text"] == expected
                for name, expected in expected_inspector_text.items()
            ),
        }
        if all(assertions.values()):
            return states, assertions
        if time.monotonic() >= deadline:
            raise SmokeAssertionError(
                "Authoring GUI control-state assertions failed: {0}; states={1}.".format(
                    [name for name, passed in assertions.items() if not passed], states
                )
            )
        time.sleep(0.1)


def wait_for_checkbox_state(user32, control_handle, expected_checked, timeout_seconds):
    expected_state = BST_CHECKED if expected_checked else 0
    deadline = time.monotonic() + timeout_seconds
    while True:
        actual_state = send_message(user32, control_handle, BM_GETCHECK)
        if actual_state == expected_state:
            return actual_state
        if time.monotonic() >= deadline:
            raise SmokeAssertionError(
                "Checkbox {0} did not reach state {1}; actual={2}.".format(
                    control_handle, expected_state, actual_state
                )
            )
        time.sleep(0.1)


def utc_now():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def canonical_path(value):
    return os.path.normcase(os.path.realpath(os.path.abspath(value)))


def json_safe(value):
    if isinstance(value, float) and not math.isfinite(value):
        if math.isnan(value):
            return "NaN"
        return "Infinity" if value > 0 else "-Infinity"
    if isinstance(value, dict):
        return {str(key): json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    return value


def write_report(path, report):
    report_path = Path(path)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = report_path.with_suffix(report_path.suffix + ".tmp")
    payload = json.dumps(json_safe(report), indent=2, sort_keys=False, allow_nan=False)
    temporary_path.write_text(payload + "\n", encoding="utf-8")
    os.replace(str(temporary_path), str(report_path))


def record_step(report, name, status, detail=None):
    step = {"name": name, "status": status, "atUtc": utc_now()}
    if detail is not None:
        step["detail"] = detail
    report["steps"].append(step)


def call(client, report, uri, args=None, options=None):
    call_args = args or {}
    if options is None:
        result = client.call(uri, call_args)
    else:
        result = client.call(uri, call_args, options=options)
    if result is None:
        raise RuntimeError("WAAPI call returned no result: {0}".format(uri))
    record_step(report, uri, "passed")
    return result


def connect_with_retry(url, timeout_seconds, report):
    deadline = time.monotonic() + timeout_seconds
    attempts = 0
    last_error = None
    while time.monotonic() < deadline:
        attempts += 1
        try:
            client = WaapiClient(url=url, allow_exception=True)
            report["connection"] = {
                "attempts": attempts,
                "connectedAtUtc": utc_now(),
                "url": url,
            }
            record_step(report, "connectWaapi", "passed", {"attempts": attempts})
            return client
        except CannotConnectToWaapiException as error:
            last_error = str(error)
            time.sleep(0.5)
    raise RuntimeError(
        "WAAPI did not become ready within {0:.1f}s ({1})".format(
            timeout_seconds, last_error or "connection refused"
        )
    )


def smoke_source_definition(source_class_id):
    source = {
        "type": "SourcePlugin",
        "name": SOURCE_NAME,
        "classId": source_class_id,
        "@Duration": 60.0,
        "@MasterGainDb": -6.0,
        "@RainIntensity": 0.75,
        "@WindSpeed": 14.0,
        "@WindDirectionDegrees": 35.0,
        "@WindGustiness": 0.65,
        "@Seed": 24681357,
        "@GeometryEnabled": True,
        "@ListenerX": 0.0,
        "@ListenerY": 0.0,
        "@ListenerZ": 0.0,
        "@ListenerYawDegrees": 20.0,
        "@FeatureCount": 4,
    }

    ring = (
        (0.0, 0.0, 6.0),
        (6.0, 0.0, 0.0),
        (0.0, 0.0, -6.0),
        (-6.0, 0.0, 0.0),
    )
    for index, (x_value, y_value, z_value) in enumerate(ring, start=1):
        source["@Feature{0}X".format(index)] = x_value
        source["@Feature{0}Y".format(index)] = y_value
        source["@Feature{0}Z".format(index)] = z_value
        source["@Feature{0}Radius".format(index)] = 2.5
        source["@Feature{0}Profile".format(index)] = index - 1
        source["@Feature{0}Mask".format(index)] = 3
        source["@Feature{0}Priority".format(index)] = 10
    return source


def create_smoke_object(client, report, source_class_id):
    args = {
        "objects": [
            {
                "object": "\\Actor-Mixer Hierarchy\\Default Work Unit",
                "children": [
                    {
                        "type": "Sound",
                        "name": SMOKE_NAME,
                        "notes": "Owned by the automated RealWorld Weather Acoustics Authoring smoke test.",
                        "children": [smoke_source_definition(source_class_id)],
                    }
                ],
            }
        ],
        "onNameConflict": "replace",
        "autoAddToSourceControl": False,
    }
    result = call(
        client,
        report,
        "ak.wwise.core.object.set",
        args,
        {"return": ["id", "name"]},
    )
    try:
        sound = result["objects"][0]["children"][0]
        source = sound["children"][0]
        sound_id = sound["id"]
        source_id = source["id"]
    except (KeyError, IndexError, TypeError) as error:
        raise RuntimeError(
            "object.set did not return the created smoke Sound and SourcePlugin IDs: {0}".format(error)
        )

    report["objectSet"] = {
        "mode": "replace",
        "parent": "\\Actor-Mixer Hierarchy\\Default Work Unit",
        "soundName": SMOKE_NAME,
        "soundId": sound_id,
        "sourceName": SOURCE_NAME,
        "sourceId": source_id,
        "sourceClassId": source_class_id,
        "sourceProperties": smoke_source_definition(source_class_id),
    }
    return sound_id, source_id


def read_source_properties(client, source_id, property_names):
    return_names = ["id"] + ["@" + name for name in property_names]
    result = client.call(
        "ak.wwise.core.object.get",
        {"from": {"id": [source_id]}},
        options={"return": return_names},
    )
    objects = result.get("return", []) if result else []
    if len(objects) != 1:
        raise RuntimeError(
            "object.get returned {0} objects while reading SourcePlugin {1}.".format(
                len(objects), source_id
            )
        )
    source = objects[0]
    values = {}
    for name in property_names:
        key = "@" + name
        if key not in source:
            raise RuntimeError("object.get omitted SourcePlugin property '{0}'.".format(key))
        values[name] = source[key]
    return values


def wait_for_property(client, source_id, property_name, predicate, timeout_seconds=5.0):
    deadline = time.monotonic() + timeout_seconds
    last_value = None
    while time.monotonic() < deadline:
        last_value = read_source_properties(client, source_id, [property_name])[property_name]
        if predicate(last_value):
            return last_value
        time.sleep(0.05)
    raise SmokeAssertionError(
        "Property '{0}' did not reach the expected state within {1:.1f}s; last value was {2!r}.".format(
            property_name, timeout_seconds, last_value
        )
    )


def property_values_match(actual, expected, tolerance=0.001):
    if isinstance(actual, bool) or isinstance(expected, bool):
        return bool(actual) == bool(expected)
    if isinstance(actual, (int, float)) and isinstance(expected, (int, float)):
        return math.isfinite(float(actual)) and abs(float(actual) - float(expected)) <= tolerance
    return actual == expected


def wait_for_properties(client, source_id, expected_values, timeout_seconds=5.0):
    deadline = time.monotonic() + timeout_seconds
    last_values = None
    property_names = list(expected_values)
    while time.monotonic() < deadline:
        last_values = read_source_properties(client, source_id, property_names)
        if all(
            property_values_match(last_values[name], expected_values[name])
            for name in property_names
        ):
            return last_values
        time.sleep(0.05)
    raise SmokeAssertionError(
        "Properties did not return to the expected state within {0:.1f}s; "
        "expected={1!r}, last={2!r}.".format(timeout_seconds, expected_values, last_values)
    )


def source_bank_property_layout():
    layout = [
        ("Duration", "f"),
        ("MasterGainDb", "f"),
        ("RainIntensity", "f"),
        ("Seed", "i"),
        ("GeometryEnabled", "?"),
        ("ListenerX", "f"),
        ("ListenerY", "f"),
        ("ListenerZ", "f"),
        ("ListenerYawDegrees", "f"),
        ("FeatureCount", "i"),
    ]
    for feature_index in range(1, 9):
        layout.extend(
            [
                ("Feature{0}X".format(feature_index), "f"),
                ("Feature{0}Y".format(feature_index), "f"),
                ("Feature{0}Z".format(feature_index), "f"),
                ("Feature{0}Radius".format(feature_index), "f"),
                ("Feature{0}Profile".format(feature_index), "i"),
                ("Feature{0}Mask".format(feature_index), "i"),
                ("Feature{0}Priority".format(feature_index), "i"),
            ]
        )
    layout.extend(
        [
            ("WindSpeed", "f"),
            ("WindDirectionDegrees", "f"),
            ("WindGustiness", "f"),
        ]
    )
    if len(layout) != SOURCE_BANK_PROPERTY_COUNT:
        raise RuntimeError(
            "Source bank property layout contains {0} entries instead of {1}.".format(
                len(layout), SOURCE_BANK_PROPERTY_COUNT
            )
        )
    return layout


def pack_source_bank_parameters(property_values):
    chunks = []
    for property_name, value_format in source_bank_property_layout():
        if property_name not in property_values:
            raise RuntimeError(
                "Cannot pack SourcePlugin bank parameters without '{0}'.".format(property_name)
            )
        value = property_values[property_name]
        if value_format == "f":
            value = float(value)
        elif value_format == "i":
            value = int(value)
        elif value_format == "?":
            value = bool(value)
        chunks.append(struct.pack("<" + value_format, value))
    block = b"".join(chunks)
    if len(block) != SOURCE_BANK_PARAMETER_BYTES:
        raise RuntimeError(
            "Packed SourcePlugin parameter block is {0} bytes instead of {1}.".format(
                len(block), SOURCE_BANK_PARAMETER_BYTES
            )
        )
    return block


def byte_match_offsets(haystack, needle):
    if not needle:
        raise ValueError("Cannot search for an empty byte sequence.")
    offsets = []
    cursor = 0
    while True:
        offset = haystack.find(needle, cursor)
        if offset < 0:
            return offsets
        offsets.append(offset)
        cursor = offset + 1


def set_source_property(client, report, source_id, property_name, value):
    call(
        client,
        report,
        "ak.wwise.core.object.setProperty",
        {"object": source_id, "property": property_name, "value": value},
    )


def create_soundbank_serialization_event(client, report, sound_id):
    result = call(
        client,
        report,
        "ak.wwise.core.object.set",
        {
            "objects": [
                {
                    "object": "\\Events\\Default Work Unit",
                    "children": [
                        {
                            "type": "Event",
                            "name": SOUNDBANK_EVENT_NAME,
                            "children": [
                                {
                                    "name": "",
                                    "type": "Action",
                                    "@ActionType": 1,
                                    "@Target": sound_id,
                                }
                            ],
                        }
                    ],
                }
            ],
            "onNameConflict": "replace",
            "autoAddToSourceControl": False,
        },
        {"return": ["id", "name"]},
    )
    try:
        return result["objects"][0]["children"][0]["id"]
    except (KeyError, IndexError, TypeError) as error:
        raise RuntimeError(
            "object.set did not return the temporary SoundBank event ID: {0}".format(error)
        )


def generate_soundbank_serialization_variant(
    client, report, project_path, source_id, event_id, geometry_enabled
):
    variant_name = SOUNDBANK_TRUE_NAME if geometry_enabled else SOUNDBANK_FALSE_NAME
    set_source_property(
        client, report, source_id, "GeometryEnabled", bool(geometry_enabled)
    )
    wait_for_property(
        client,
        source_id,
        "GeometryEnabled",
        lambda value: bool(value) == bool(geometry_enabled),
    )

    property_layout = source_bank_property_layout()
    property_names = [name for name, _ in property_layout]
    property_values = read_source_properties(client, source_id, property_names)
    expected_block = pack_source_bank_parameters(property_values)
    generation_result = call(
        client,
        report,
        "ak.wwise.core.soundbank.generate",
        {
            "soundbanks": [
                {
                    "name": variant_name,
                    "events": [event_id],
                    "inclusions": ["event", "structure"],
                    "rebuild": True,
                }
            ],
            "platforms": ["Windows"],
            "skipLanguages": True,
            "writeToDisk": True,
        },
    )
    logs = generation_result.get("logs", []) or []
    bad_logs = [
        item
        for item in logs
        if any(
            token in str(item.get("severity", "")).casefold()
            for token in ("warning", "error", "fatal")
        )
    ]

    bank_path = (
        project_path.parent
        / "GeneratedSoundBanks"
        / "Windows"
        / (variant_name + ".bnk")
    ).resolve()
    try:
        bank_path.relative_to(project_path.parent.resolve())
    except ValueError:
        raise RuntimeError(
            "Generated SoundBank path escaped the disposable project: '{0}'.".format(bank_path)
        )
    if not bank_path.is_file():
        raise SmokeAssertionError(
            "SoundBank generation did not write '{0}'.".format(bank_path)
        )

    bank_bytes = bank_path.read_bytes()
    block_offsets = byte_match_offsets(bank_bytes, expected_block)
    matched_block = (
        bank_bytes[
            block_offsets[0] : block_offsets[0] + SOURCE_BANK_PARAMETER_BYTES
        ]
        if len(block_offsets) == 1
        else None
    )
    assertions = {
        "readAll69SourceProperties": len(property_values) == SOURCE_BANK_PROPERTY_COUNT,
        "expectedBlockIs273Bytes": len(expected_block) == SOURCE_BANK_PARAMETER_BYTES,
        "expectedBlockMatchesExactlyOnce": len(block_offsets) == 1,
        "geometryByteMatchesVariant": (
            matched_block is not None
            and matched_block[GEOMETRY_ENABLED_BANK_OFFSET]
            == (1 if geometry_enabled else 0)
        ),
        "generationLogsHaveNoWarningErrorOrFatal": not bad_logs,
    }
    variant_report = {
        "name": variant_name,
        "geometryEnabled": bool(geometry_enabled),
        "sourcePropertyCount": len(property_values),
        "bankPath": str(bank_path),
        "bankBytes": len(bank_bytes),
        "bankSha256": hashlib.sha256(bank_bytes).hexdigest(),
        "expectedBlockBytes": len(expected_block),
        "expectedBlockSha256": hashlib.sha256(expected_block).hexdigest(),
        "parameterBlockSha256": (
            hashlib.sha256(matched_block).hexdigest()
            if matched_block is not None
            else None
        ),
        "parameterBlockOffsets": block_offsets,
        "parameterBlockOffset": block_offsets[0] if len(block_offsets) == 1 else None,
        "geometryByteBlockOffset": GEOMETRY_ENABLED_BANK_OFFSET,
        "geometryByteBankOffset": (
            block_offsets[0] + GEOMETRY_ENABLED_BANK_OFFSET
            if len(block_offsets) == 1
            else None
        ),
        "generationLogCount": len(logs),
        "generationLogSeverities": [item.get("severity") for item in logs],
        "rejectedGenerationLogs": bad_logs,
        "assertions": assertions,
    }
    if not all(assertions.values()):
        raise SmokeAssertionError(
            "SoundBank serialization assertions failed for {0}: {1}.".format(
                variant_name,
                [name for name, passed in assertions.items() if not passed],
            )
        )
    return variant_report, matched_block


def run_soundbank_serialization_smoke(
    client, report, project_path, sound_id, source_id
):
    bank_report = {
        "parameterPropertyCount": SOURCE_BANK_PROPERTY_COUNT,
        "parameterBlockBytes": SOURCE_BANK_PARAMETER_BYTES,
        "geometryByteBlockOffset": GEOMETRY_ENABLED_BANK_OFFSET,
        "eventId": None,
        "variants": {},
        "assertions": {},
        "cleanup": {},
    }
    report["soundBankSerialization"] = bank_report
    event_id = None
    operation_error = None

    try:
        event_id = create_soundbank_serialization_event(client, report, sound_id)
        bank_report["eventId"] = event_id
        true_report, true_block = generate_soundbank_serialization_variant(
            client, report, project_path, source_id, event_id, True
        )
        bank_report["variants"]["geometryTrue"] = true_report
        false_report, false_block = generate_soundbank_serialization_variant(
            client, report, project_path, source_id, event_id, False
        )
        bank_report["variants"]["geometryFalse"] = false_report

        differing_offsets = [
            index
            for index, (true_byte, false_byte) in enumerate(zip(true_block, false_block))
            if true_byte != false_byte
        ]
        bank_report["parameterBlockDifferingOffsets"] = differing_offsets
        bank_report["assertions"].update(
            {
                "trueVariantPassed": all(true_report["assertions"].values()),
                "falseVariantPassed": all(false_report["assertions"].values()),
                "geometryTrueByteIsOne": (
                    true_block[GEOMETRY_ENABLED_BANK_OFFSET] == 1
                ),
                "geometryFalseByteIsZero": (
                    false_block[GEOMETRY_ENABLED_BANK_OFFSET] == 0
                ),
                "parameterBlocksDifferOnlyAtGeometryByte": (
                    differing_offsets == [GEOMETRY_ENABLED_BANK_OFFSET]
                ),
            }
        )
        if not all(bank_report["assertions"].values()):
            raise SmokeAssertionError(
                "Cross-variant SoundBank serialization assertions failed: {0}.".format(
                    [
                        name
                        for name, passed in bank_report["assertions"].items()
                        if not passed
                    ]
                )
            )
    except Exception as error:
        operation_error = error
    finally:
        cleanup_errors = []
        if event_id is not None:
            try:
                call(
                    client,
                    report,
                    "ak.wwise.core.object.delete",
                    {
                        "object": event_id,
                        "autoCheckOutToSourceControl": False,
                    },
                )
                bank_report["cleanup"]["temporaryEventDeleted"] = True
            except Exception as error:
                cleanup_errors.append("temporary event: {0}".format(error))
                bank_report["cleanup"]["temporaryEventDeleted"] = False
        try:
            set_source_property(client, report, source_id, "GeometryEnabled", True)
            geometry_restored = bool(
                wait_for_property(
                    client,
                    source_id,
                    "GeometryEnabled",
                    lambda value: bool(value),
                )
            )
            bank_report["cleanup"]["geometryRestoredTrue"] = geometry_restored
            bank_report["assertions"]["geometryRestoredTrue"] = geometry_restored
        except Exception as error:
            cleanup_errors.append("GeometryEnabled restore: {0}".format(error))
            bank_report["cleanup"]["geometryRestoredTrue"] = False
            bank_report["assertions"]["geometryRestoredTrue"] = False
        bank_report["cleanup"]["errors"] = cleanup_errors

        if cleanup_errors:
            cleanup_error = RuntimeError(
                "SoundBank serialization cleanup failed: {0}.".format(
                    "; ".join(cleanup_errors)
                )
            )
            if operation_error is None:
                operation_error = cleanup_error
            else:
                operation_error = RuntimeError(
                    "SoundBank serialization failed ({0}); cleanup also failed ({1}).".format(
                        operation_error, cleanup_error
                    )
                )

    if operation_error is not None:
        record_step(report, "soundBankSerialization", "failed", str(operation_error))
        raise operation_error
    record_step(report, "soundBankSerialization", "passed", bank_report["assertions"])
    return all(bank_report["assertions"].values())


def perform_undo_gate(client, report, source_id, action, expected_values, changed_values):
    changed_assertions = {
        name: not property_values_match(changed_values[name], expected_values[name])
        for name in expected_values
    }
    mutation_observed = any(changed_assertions.values())
    if not mutation_observed:
        raise SmokeAssertionError(
            "Undo gate '{0}' did not observe a property mutation before Undo.".format(action)
        )

    call(client, report, UNDO_URI)
    restored_values = wait_for_properties(client, source_id, expected_values)
    restored_assertions = {
        name: property_values_match(restored_values[name], expected_values[name])
        for name in expected_values
    }
    result = {
        "action": action,
        "undoUri": UNDO_URI,
        "expectedBeforeAction": expected_values,
        "valuesAfterAction": changed_values,
        "valuesAfterUndo": restored_values,
        "changedProperties": [
            name for name, changed in changed_assertions.items() if changed
        ],
        "assertions": {
            "mutationObservedBeforeUndo": mutation_observed,
            "allExpectedPropertiesRestored": all(restored_assertions.values()),
            "propertiesRestored": restored_assertions,
        },
    }
    if not result["assertions"]["allExpectedPropertiesRestored"]:
        raise SmokeAssertionError("Undo gate '{0}' did not restore all properties.".format(action))
    record_step(report, "authoringGui.undo.{0}".format(action), "passed", result)
    return result


def feature_property_names(feature_index):
    return [
        "Feature{0}{1}".format(feature_index, suffix)
        for suffix in FEATURE_PROPERTY_SUFFIXES
    ]


def read_preview_scene(client, source_id):
    feature_count = int(
        read_source_properties(client, source_id, ["FeatureCount"])["FeatureCount"]
    )
    if feature_count < 0 or feature_count > 8:
        raise SmokeAssertionError("FeatureCount is outside the supported [0, 8] range.")
    names = ["ListenerX", "ListenerZ"]
    for index in range(1, feature_count + 1):
        names.extend(
            [
                "Feature{0}X".format(index),
                "Feature{0}Z".format(index),
                "Feature{0}Radius".format(index),
            ]
        )
    values = read_source_properties(client, source_id, names)
    features = []
    for index in range(1, feature_count + 1):
        features.append(
            {
                "index": index,
                "x": float(values["Feature{0}X".format(index)]),
                "z": float(values["Feature{0}Z".format(index)]),
                "radius": float(values["Feature{0}Radius".format(index)]),
            }
        )
    return {
        "featureCount": feature_count,
        "listenerX": float(values["ListenerX"]),
        "listenerZ": float(values["ListenerZ"]),
        "features": features,
    }


def get_client_size(user32, hwnd):
    client = read_client_rect(user32, hwnd)
    width = client["width"]
    height = client["height"]
    if width <= 0 or height <= 0:
        raise RuntimeError(
            "RWWA preview canvas has an unusable client size: {0}x{1}.".format(width, height)
        )
    return width, height


def calculate_canvas_transform(scene, canvas_width, canvas_height):
    plot_rect = {
        "left": 36,
        "top": 18,
        "right": max(56, canvas_width - 18),
        "bottom": max(50, canvas_height - 30),
    }
    minimum_x = scene["listenerX"]
    maximum_x = scene["listenerX"]
    minimum_z = scene["listenerZ"]
    maximum_z = scene["listenerZ"]
    for feature in scene["features"]:
        radius = max(0.0, feature["radius"])
        minimum_x = min(minimum_x, feature["x"] - radius)
        maximum_x = max(maximum_x, feature["x"] + radius)
        minimum_z = min(minimum_z, feature["z"] - radius)
        maximum_z = max(maximum_z, feature["z"] + radius)

    span_x = max(20.0, maximum_x - minimum_x)
    span_z = max(20.0, maximum_z - minimum_z)
    available_width = float(max(1, plot_rect["right"] - plot_rect["left"]))
    available_height = float(max(1, plot_rect["bottom"] - plot_rect["top"]))
    pixels_per_meter = max(
        0.01,
        min(available_width / (span_x * 1.2), available_height / (span_z * 1.2)),
    )
    return {
        "plotRect": plot_rect,
        "worldCenterX": (minimum_x + maximum_x) * 0.5,
        "worldCenterZ": (minimum_z + maximum_z) * 0.5,
        "pixelsPerMeter": pixels_per_meter,
    }


def world_to_screen(transform, x_value, z_value):
    plot_rect = transform["plotRect"]
    center_x = (plot_rect["left"] + plot_rect["right"]) * 0.5
    center_y = (plot_rect["top"] + plot_rect["bottom"]) * 0.5
    return {
        "x": cpp_lround(
            center_x
            + (x_value - transform["worldCenterX"]) * transform["pixelsPerMeter"]
        ),
        "y": cpp_lround(
            center_y
            - (z_value - transform["worldCenterZ"]) * transform["pixelsPerMeter"]
        ),
    }


def send_canvas_drag(user32, canvas_hwnd, start, destination):
    send_message(
        user32,
        canvas_hwnd,
        WM_LBUTTONDOWN,
        MK_LBUTTON,
        make_mouse_lparam(start["x"], start["y"]),
    )
    send_message(
        user32,
        canvas_hwnd,
        WM_MOUSEMOVE,
        MK_LBUTTON,
        make_mouse_lparam(destination["x"], destination["y"]),
    )
    send_message(
        user32,
        canvas_hwnd,
        WM_LBUTTONUP,
        0,
        make_mouse_lparam(destination["x"], destination["y"]),
    )


def restore_weather_ring(client, source_id):
    stable_definition = smoke_source_definition(DEFAULT_SOURCE_CLASS_ID)
    properties = {
        name[1:]: value
        for name, value in stable_definition.items()
        if name.startswith("@") and name != "@FeatureCount"
    }
    properties["FeatureCount"] = stable_definition["@FeatureCount"]
    for name, value in properties.items():
        result = client.call(
            "ak.wwise.core.object.setProperty",
            {"object": source_id, "property": name, "value": value},
        )
        if result is None:
            raise RuntimeError("setProperty returned no result while restoring '{0}'.".format(name))

    values = read_source_properties(client, source_id, list(properties))
    property_assertions = {
        name: property_values_match(values[name], expected_value)
        for name, expected_value in properties.items()
    }
    assertions = {
        "allStablePropertiesMatch": all(property_assertions.values()),
        "propertiesMatch": property_assertions,
    }
    if not all(assertions.values()):
        raise SmokeAssertionError(
            "Stable weather-ring restoration failed: {0}".format(
                [name for name, passed in property_assertions.items() if not passed]
            )
        )
    return {"properties": values, "assertions": assertions}


def run_authoring_gui_smoke(client, report, source_id, expected_pid, timeout_seconds):
    gui_report = {
        "expectedProcessId": expected_pid,
        "command": "ShowSourceEditor",
        "controls": None,
        "interactions": [],
        "undoGates": [],
        "assertions": {},
        "restore": None,
    }
    report["authoringGui"] = gui_report
    operation_error = None

    try:
        call(
            client,
            report,
            "ak.wwise.ui.commands.execute",
            {"command": "ShowSourceEditor", "objects": [source_id]},
        )
        user32, controls = find_preview_controls(expected_pid, timeout_seconds)
        control_states, control_assertions = assert_preview_control_state(
            user32, controls, expected_pid, timeout_seconds
        )
        controls["states"] = control_states
        gui_report["controls"] = controls
        gui_report["assertions"].update(control_assertions)
        record_step(report, "authoringGui.findControls", "passed", controls)

        canvas = controls["canvasHandle"]
        geometry_checkbox = controls["geometryCheckboxHandle"]
        add_button = controls["addButtonHandle"]
        delete_button = controls["deleteButtonHandle"]
        initial_count = int(
            read_source_properties(client, source_id, ["FeatureCount"])["FeatureCount"]
        )
        gui_report["assertions"]["initialFeatureCountIsFour"] = initial_count == 4
        if initial_count != 4:
            raise SmokeAssertionError(
                "GUI smoke requires the stable four-feature ring; got FeatureCount={0}.".format(
                    initial_count
                )
            )

        before_geometry_values = read_source_properties(
            client, source_id, ["GeometryEnabled"]
        )
        send_message(user32, geometry_checkbox, BM_CLICK)
        after_geometry_enabled = bool(
            wait_for_property(
                client,
                source_id,
                "GeometryEnabled",
                lambda value: value is False,
            )
        )
        wait_for_checkbox_state(
            user32, geometry_checkbox, False, timeout_seconds
        )
        geometry_interaction = {
            "action": "geometryCheckbox",
            "controlHandle": geometry_checkbox,
            "beforeGeometryEnabled": bool(
                before_geometry_values["GeometryEnabled"]
            ),
            "afterGeometryEnabled": after_geometry_enabled,
            "assertion": (
                bool(before_geometry_values["GeometryEnabled"])
                and not after_geometry_enabled
            ),
        }
        gui_report["interactions"].append(geometry_interaction)
        gui_report["assertions"]["geometryCheckboxCommitsFalse"] = (
            geometry_interaction["assertion"]
        )
        record_step(
            report,
            "authoringGui.geometryCheckbox",
            "passed",
            geometry_interaction,
        )
        geometry_undo = perform_undo_gate(
            client,
            report,
            source_id,
            "geometryCheckbox",
            before_geometry_values,
            {"GeometryEnabled": after_geometry_enabled},
        )
        wait_for_checkbox_state(
            user32, geometry_checkbox, True, timeout_seconds
        )
        gui_report["undoGates"].append(geometry_undo)
        gui_report["assertions"]["geometryCheckboxUndoRestoresTrue"] = geometry_undo[
            "assertions"
        ]["allExpectedPropertiesRestored"]

        add_property_names = [
            "FeatureCount",
            "GeometryEnabled",
            "ListenerX",
            "ListenerY",
            "ListenerZ",
            "ListenerYawDegrees",
        ] + feature_property_names(5)
        before_add_values = read_source_properties(client, source_id, add_property_names)
        send_message(user32, add_button, BM_CLICK)
        after_add_count = int(
            wait_for_property(client, source_id, "FeatureCount", lambda value: int(value) == 5)
        )
        after_add_values = read_source_properties(client, source_id, add_property_names)
        listener_yaw_radians = math.radians(
            float(before_add_values["ListenerYawDegrees"])
        )
        expected_add_values = {
            "FeatureCount": 5,
            "GeometryEnabled": True,
            "Feature5X": float(before_add_values["ListenerX"])
            + math.sin(listener_yaw_radians) * 4.0,
            "Feature5Y": 0.0,
            "Feature5Z": float(before_add_values["ListenerZ"])
            + math.cos(listener_yaw_radians) * 4.0,
            "Feature5Radius": 2.0,
            "Feature5Profile": 0,
            "Feature5Mask": 3,
            "Feature5Priority": 1,
        }
        add_default_assertions = {
            name: property_values_match(after_add_values[name], expected_value)
            for name, expected_value in expected_add_values.items()
        }
        add_interaction = {
            "action": "addButton",
            "controlHandle": add_button,
            "beforeFeatureCount": initial_count,
            "afterFeatureCount": after_add_count,
            "expectedFeature5Defaults": expected_add_values,
            "actualFeature5Defaults": {
                name: after_add_values[name] for name in expected_add_values
            },
            "defaultAssertions": add_default_assertions,
            "assertion": after_add_count == 5 and all(add_default_assertions.values()),
        }
        gui_report["interactions"].append(add_interaction)
        gui_report["assertions"]["addButtonIncrementsFeatureCount"] = (
            after_add_count == 5
        )
        gui_report["assertions"]["addButtonInitializesFeature5Defaults"] = all(
            add_default_assertions.values()
        )
        if not add_interaction["assertion"]:
            raise SmokeAssertionError(
                "Add button did not initialize Feature 5 defaults: {0}.".format(
                    [
                        name
                        for name, passed in add_default_assertions.items()
                        if not passed
                    ]
                )
            )
        record_step(report, "authoringGui.addButton", "passed", add_interaction)
        add_undo = perform_undo_gate(
            client,
            report,
            source_id,
            "addButton",
            before_add_values,
            after_add_values,
        )
        gui_report["undoGates"].append(add_undo)
        gui_report["assertions"]["addButtonUndoRestoresProperties"] = add_undo[
            "assertions"
        ]["allExpectedPropertiesRestored"]

        send_message(user32, add_button, BM_CLICK)
        wait_for_property(client, source_id, "FeatureCount", lambda value: int(value) == 5)

        scene_before_move = read_preview_scene(client, source_id)
        canvas_width, canvas_height = get_client_size(user32, canvas)
        move_transform = calculate_canvas_transform(
            scene_before_move, canvas_width, canvas_height
        )
        feature_before_move = scene_before_move["features"][4]
        move_start = world_to_screen(
            move_transform, feature_before_move["x"], feature_before_move["z"]
        )
        move_target_world = {
            "x": feature_before_move["x"] + 1.5,
            "z": feature_before_move["z"] + 0.75,
        }
        move_destination = world_to_screen(
            move_transform, move_target_world["x"], move_target_world["z"]
        )
        send_canvas_drag(user32, canvas, move_start, move_destination)
        moved_values = read_source_properties(
            client, source_id, ["Feature5X", "Feature5Z"]
        )
        moved_x = float(moved_values["Feature5X"])
        moved_z = float(moved_values["Feature5Z"])
        move_assertions = {
            "xChanged": abs(moved_x - feature_before_move["x"]) > 0.25,
            "zChanged": abs(moved_z - feature_before_move["z"]) > 0.25,
            "xNearRequestedTarget": abs(moved_x - move_target_world["x"]) <= 0.15,
            "zNearRequestedTarget": abs(moved_z - move_target_world["z"]) <= 0.15,
        }
        move_interaction = {
            "action": "dragFeature5Center",
            "canvasHandle": canvas,
            "canvasClientSize": {"width": canvas_width, "height": canvas_height},
            "transform": move_transform,
            "startHandle": move_start,
            "destinationHandle": move_destination,
            "before": {
                "x": feature_before_move["x"],
                "z": feature_before_move["z"],
            },
            "requestedTarget": move_target_world,
            "after": {"x": moved_x, "z": moved_z},
            "assertions": move_assertions,
        }
        gui_report["interactions"].append(move_interaction)
        gui_report["assertions"]["featureCenterDragChangesPosition"] = all(
            move_assertions.values()
        )
        if not all(move_assertions.values()):
            raise SmokeAssertionError("Feature 5 center drag did not update X/Z as expected.")
        record_step(report, "authoringGui.dragFeatureCenter", "passed", move_interaction)
        move_undo = perform_undo_gate(
            client,
            report,
            source_id,
            "dragFeatureCenter",
            {
                "Feature5X": feature_before_move["x"],
                "Feature5Z": feature_before_move["z"],
            },
            {"Feature5X": moved_x, "Feature5Z": moved_z},
        )
        gui_report["undoGates"].append(move_undo)
        gui_report["assertions"]["featureCenterUndoRestoresPosition"] = move_undo[
            "assertions"
        ]["allExpectedPropertiesRestored"]

        scene_before_resize = read_preview_scene(client, source_id)
        resize_transform = calculate_canvas_transform(
            scene_before_resize, canvas_width, canvas_height
        )
        feature_before_resize = scene_before_resize["features"][4]
        feature_center = world_to_screen(
            resize_transform, feature_before_resize["x"], feature_before_resize["z"]
        )
        radius_pixels = max(
            5, cpp_lround(feature_before_resize["radius"] * resize_transform["pixelsPerMeter"])
        )
        radius_handle = {
            "x": feature_center["x"] + radius_pixels,
            "y": feature_center["y"],
        }
        resize_target_world = {
            "x": feature_before_resize["x"] + feature_before_resize["radius"] + 1.5,
            "z": feature_before_resize["z"],
        }
        resize_destination = world_to_screen(
            resize_transform, resize_target_world["x"], resize_target_world["z"]
        )
        send_canvas_drag(user32, canvas, radius_handle, resize_destination)
        resized_radius = float(
            read_source_properties(client, source_id, ["Feature5Radius"])["Feature5Radius"]
        )
        resize_assertions = {
            "radiusIncreased": resized_radius > feature_before_resize["radius"] + 0.5,
            "radiusNearRequestedTarget": abs(
                resized_radius - (feature_before_resize["radius"] + 1.5)
            )
            <= 0.15,
        }
        resize_interaction = {
            "action": "dragFeature5RadiusHandle",
            "canvasHandle": canvas,
            "transform": resize_transform,
            "featureCenterHandle": feature_center,
            "radiusHandle": radius_handle,
            "destinationHandle": resize_destination,
            "beforeRadius": feature_before_resize["radius"],
            "requestedRadius": feature_before_resize["radius"] + 1.5,
            "afterRadius": resized_radius,
            "assertions": resize_assertions,
        }
        gui_report["interactions"].append(resize_interaction)
        gui_report["assertions"]["radiusHandleDragIncreasesRadius"] = all(
            resize_assertions.values()
        )
        if not all(resize_assertions.values()):
            raise SmokeAssertionError("Feature 5 radius-handle drag did not increase Radius.")
        record_step(report, "authoringGui.dragRadiusHandle", "passed", resize_interaction)
        resize_undo = perform_undo_gate(
            client,
            report,
            source_id,
            "dragRadiusHandle",
            {"Feature5Radius": feature_before_resize["radius"]},
            {"Feature5Radius": resized_radius},
        )
        gui_report["undoGates"].append(resize_undo)
        gui_report["assertions"]["radiusHandleUndoRestoresRadius"] = resize_undo[
            "assertions"
        ]["allExpectedPropertiesRestored"]

        delete_property_names = ["FeatureCount"] + feature_property_names(5)
        before_delete_values = read_source_properties(
            client, source_id, delete_property_names
        )
        send_message(user32, delete_button, BM_CLICK)
        after_button_delete = int(
            wait_for_property(client, source_id, "FeatureCount", lambda value: int(value) == 4)
        )
        after_delete_values = read_source_properties(
            client, source_id, delete_property_names
        )
        delete_interaction = {
            "action": "deleteButton",
            "controlHandle": delete_button,
            "beforeFeatureCount": 5,
            "afterFeatureCount": after_button_delete,
            "assertion": after_button_delete == 4,
        }
        gui_report["interactions"].append(delete_interaction)
        gui_report["assertions"]["deleteButtonDecrementsFeatureCount"] = delete_interaction[
            "assertion"
        ]
        record_step(report, "authoringGui.deleteButton", "passed", delete_interaction)
        delete_undo = perform_undo_gate(
            client,
            report,
            source_id,
            "deleteButton",
            before_delete_values,
            after_delete_values,
        )
        gui_report["undoGates"].append(delete_undo)
        gui_report["assertions"]["deleteButtonUndoRestoresProperties"] = delete_undo[
            "assertions"
        ]["allExpectedPropertiesRestored"]

        keyboard_delete_property_names = ["FeatureCount"]
        for feature_index in range(1, 6):
            keyboard_delete_property_names.extend(feature_property_names(feature_index))
        before_keyboard_delete_values = read_source_properties(
            client, source_id, keyboard_delete_property_names
        )
        before_keyboard_delete = int(before_keyboard_delete_values["FeatureCount"])
        send_message(user32, canvas, WM_KEYDOWN, VK_DELETE)
        after_keyboard_delete = int(
            wait_for_property(client, source_id, "FeatureCount", lambda value: int(value) == 4)
        )
        after_keyboard_delete_values = read_source_properties(
            client, source_id, keyboard_delete_property_names
        )
        keyboard_interaction = {
            "action": "canvasDeleteKey",
            "canvasHandle": canvas,
            "beforeFeatureCount": before_keyboard_delete,
            "virtualKey": VK_DELETE,
            "afterFeatureCount": after_keyboard_delete,
            "assertion": before_keyboard_delete == 5 and after_keyboard_delete == 4,
        }
        gui_report["interactions"].append(keyboard_interaction)
        gui_report["assertions"]["canvasDeleteKeyDecrementsFeatureCount"] = (
            keyboard_interaction["assertion"]
        )
        record_step(report, "authoringGui.canvasDeleteKey", "passed", keyboard_interaction)
        keyboard_delete_undo = perform_undo_gate(
            client,
            report,
            source_id,
            "canvasDeleteKey",
            before_keyboard_delete_values,
            after_keyboard_delete_values,
        )
        gui_report["undoGates"].append(keyboard_delete_undo)
        gui_report["assertions"]["canvasDeleteKeyUndoRestoresProperties"] = (
            keyboard_delete_undo["assertions"]["allExpectedPropertiesRestored"]
        )

        if not all(gui_report["assertions"].values()):
            raise SmokeAssertionError(
                "Authoring GUI assertions failed: {0}".format(
                    [
                        name
                        for name, passed in gui_report["assertions"].items()
                        if not passed
                    ]
                )
            )
    except Exception as error:
        operation_error = error
        gui_report["error"] = {
            "type": type(error).__name__,
            "message": str(error),
        }
        record_step(report, "authoringGui.interactions", "failed", gui_report["error"])
    finally:
        try:
            gui_report["restore"] = restore_weather_ring(client, source_id)
            record_step(
                report,
                "authoringGui.restoreWeatherRing",
                "passed",
                gui_report["restore"],
            )
        except Exception as restore_error:
            gui_report["restore"] = {
                "error": "{0}: {1}".format(type(restore_error).__name__, restore_error)
            }
            record_step(
                report,
                "authoringGui.restoreWeatherRing",
                "failed",
                gui_report["restore"],
            )
            if operation_error is None:
                operation_error = restore_error
            else:
                operation_error = RuntimeError(
                    "GUI interaction failed ({0}); stable weather-ring restoration also failed ({1}).".format(
                        operation_error, restore_error
                    )
                )

    if operation_error is not None:
        raise operation_error
    return True


def counter_value(counters, counter_id):
    matches = [item for item in counters if item.get("id") == counter_id]
    return matches[-1].get("value") if matches else None


def contains_smoke_identity(value, sound_id, source_id):
    identities = {SMOKE_NAME.lower(), SOURCE_NAME.lower(), sound_id.lower(), source_id.lower()}
    serialized = json.dumps(value, sort_keys=True).lower()
    return any(identity in serialized for identity in identities)


def run_smoke(args, report):
    project_path = Path(args.project).resolve(strict=True)
    client = None
    transport_id = None
    capture_started = False
    capture_artifact_valid = False
    core_assertions_passed = False
    gui_interactions_passed = False
    soundbank_serialization_passed = False
    mandatory_cleanup_errors = []
    cleanup_warnings = []

    try:
        client = connect_with_retry(args.waapi_url, args.connect_timeout, report)

        project_info = call(client, report, PROJECT_INFO_URI)
        actual_project_path = project_info.get("path")
        report["project"] = {
            "expectedPath": str(project_path),
            "actualPath": actual_project_path,
            "name": project_info.get("name"),
            "id": project_info.get("id"),
            "queryUri": PROJECT_INFO_URI,
        }
        if not actual_project_path or canonical_path(actual_project_path) != canonical_path(str(project_path)):
            raise RuntimeError(
                "Refusing to modify the project returned by WAAPI. Expected '{0}', got '{1}'.".format(
                    project_path, actual_project_path
                )
            )

        sound_id, source_id = create_smoke_object(client, report, args.source_class_id)
        gui_interactions_passed = run_authoring_gui_smoke(
            client,
            report,
            source_id,
            args.wwise_pid,
            args.gui_timeout,
        )
        soundbank_serialization_passed = run_soundbank_serialization_smoke(
            client,
            report,
            project_path,
            sound_id,
            source_id,
        )
        call(
            client,
            report,
            "ak.wwise.core.project.save",
            {"autoCheckOutToSourceControl": False},
        )

        call(
            client,
            report,
            "ak.wwise.core.profiler.enableProfilerData",
            {
                "dataTypes": [
                    {"dataType": "voices", "enable": True},
                    {"dataType": "cpu", "enable": True},
                ]
            },
        )
        capture_result = call(client, report, "ak.wwise.core.profiler.startCapture")
        capture_started = True
        report["profiler"] = {"captureStartTimeMs": capture_result.get("return")}

        transport_result = call(
            client, report, "ak.wwise.core.transport.create", {"object": sound_id}
        )
        transport_id = transport_result.get("transport")
        if transport_id is None:
            raise RuntimeError("transport.create did not return a transport ID")
        call(
            client,
            report,
            "ak.wwise.core.transport.executeAction",
            {"transport": transport_id, "action": "play"},
        )
        time.sleep(args.playback_seconds)

        transport_state = call(
            client,
            report,
            "ak.wwise.core.transport.getState",
            {"transport": transport_id},
        ).get("state")
        voices = call(
            client,
            report,
            "ak.wwise.core.profiler.getVoices",
            {"time": "capture"},
            {
                "return": [
                    "pipelineID",
                    "playingID",
                    "objectGUID",
                    "objectName",
                    "playTargetGUID",
                    "playTargetName",
                    "isStarted",
                    "isVirtual",
                ]
            },
        ).get("return", [])
        cpu_usage = call(
            client,
            report,
            "ak.wwise.core.profiler.getCpuUsage",
            {"time": "capture"},
        ).get("return", [])
        counters = call(
            client,
            report,
            "ak.wwise.core.profiler.getPerformanceMonitor",
            {"time": "capture"},
        ).get("return", [])

        smoke_voices = [
            voice for voice in voices if contains_smoke_identity(voice, sound_id, source_id)
        ]
        source_cpu = [
            item
            for item in cpu_usage
            if item.get("id") == args.source_class_id
            or "realworld weather" in str(item.get("elementName", "")).lower()
            or "realworldweather" in str(item.get("elementName", "")).lower()
        ]
        voices_physical = counter_value(counters, "VoicesPhysical")
        voices_total = counter_value(counters, "VoicesTotal")
        output_peak = counter_value(counters, "OutputPeak")
        output_peak_finite = isinstance(output_peak, (int, float)) and math.isfinite(output_peak)
        output_peak_audible = output_peak_finite and output_peak > args.silence_floor_db

        # Keep isolated transport evidence in the report for diagnosis, but do not allow it
        # to satisfy plug-in identity: passing requires the smoke voice or Source CPU entry.
        isolated_transport_evidence = (
            transport_state == "playing"
            and bool(voices)
            and isinstance(voices_total, (int, float))
            and voices_total >= 1
        )
        voice_or_cpu_evidence = bool(smoke_voices or source_cpu)

        report["profiler"].update(
            {
                "transportId": transport_id,
                "transportState": transport_state,
                "voices": voices,
                "smokeVoices": smoke_voices,
                "cpuUsage": cpu_usage,
                "sourceCpuEvidence": source_cpu,
                "performanceMonitor": counters,
                "voicesPhysical": voices_physical,
                "voicesTotal": voices_total,
                "outputPeakDb": output_peak,
                "silenceFloorDb": args.silence_floor_db,
                "isolatedTransportEvidence": isolated_transport_evidence,
            }
        )
        report["assertions"] = {
            "transportPlaying": transport_state == "playing",
            "smokeVoiceOrSourceCpuEvidence": voice_or_cpu_evidence,
            "outputPeakFinite": output_peak_finite,
            "outputPeakAboveSilenceFloor": output_peak_audible,
            "authoringGuiInteractions": gui_interactions_passed,
            "soundBankParameterSerialization": soundbank_serialization_passed,
        }
        core_assertions_passed = all(report["assertions"].values())
        if not core_assertions_passed:
            failed = [name for name, passed in report["assertions"].items() if not passed]
            raise SmokeAssertionError("Smoke assertions failed: {0}".format(", ".join(failed)))
    finally:
        if client is not None and transport_id is not None:
            try:
                call(
                    client,
                    report,
                    "ak.wwise.core.transport.executeAction",
                    {"transport": transport_id, "action": "stop"},
                )
            except Exception as error:  # cleanup must continue through every operation
                mandatory_cleanup_errors.append("transport.stop: {0}".format(error))
            try:
                call(
                    client,
                    report,
                    "ak.wwise.core.transport.destroy",
                    {"transport": transport_id},
                )
            except Exception as error:
                mandatory_cleanup_errors.append("transport.destroy: {0}".format(error))

        if client is not None and capture_started:
            try:
                stop_result = call(client, report, "ak.wwise.core.profiler.stopCapture")
                report.setdefault("profiler", {})["captureStopTimeMs"] = stop_result.get("return")
            except Exception as error:
                mandatory_cleanup_errors.append("profiler.stopCapture: {0}".format(error))
            try:
                capture_path = Path(args.capture_file).resolve()
                call(
                    client,
                    report,
                    "ak.wwise.core.profiler.saveCapture",
                    {"file": str(capture_path)},
                )
                for _ in range(20):
                    if capture_path.is_file() and capture_path.stat().st_size > 0:
                        capture_artifact_valid = True
                        break
                    time.sleep(0.05)
                if not capture_artifact_valid:
                    raise SmokeAssertionError(
                        "Profiler capture was not written as a non-empty file: '{0}'.".format(
                            capture_path
                        )
                    )
                report.setdefault("profiler", {}).update(
                    {
                        "captureFile": str(capture_path),
                        "captureFileBytes": capture_path.stat().st_size,
                    }
                )
            except Exception as error:
                mandatory_cleanup_errors.append("profiler.saveCapture: {0}".format(error))

        if client is not None:
            try:
                client.disconnect()
            except Exception as error:
                mandatory_cleanup_errors.append("waapi.disconnect: {0}".format(error))

        report["cleanup"] = {
            "mandatoryErrors": mandatory_cleanup_errors,
            "warnings": cleanup_warnings,
        }
        report.setdefault("assertions", {})["profilerCaptureSaved"] = capture_artifact_valid
        report["success"] = (
            core_assertions_passed
            and capture_artifact_valid
            and not mandatory_cleanup_errors
        )


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True, help="Absolute path to the isolated smoke .wproj")
    parser.add_argument("--report", required=True, help="JSON report path")
    parser.add_argument("--capture-file", required=True, help="Required non-empty .prof output path")
    parser.add_argument("--waapi-url", default="ws://127.0.0.1:8080/waapi")
    parser.add_argument("--connect-timeout", type=float, default=30.0)
    parser.add_argument("--playback-seconds", type=float, default=1.5)
    parser.add_argument("--silence-floor-db", type=float, default=-90.0)
    parser.add_argument("--source-class-id", type=int, default=DEFAULT_SOURCE_CLASS_ID)
    parser.add_argument(
        "--wwise-pid",
        type=int,
        required=True,
        help="PID of the wrapper-owned Wwise process used to scope Win32 control discovery",
    )
    parser.add_argument("--gui-timeout", type=float, default=20.0)
    args = parser.parse_args(argv)
    if args.connect_timeout <= 0:
        parser.error("--connect-timeout must be positive")
    if args.playback_seconds <= 0:
        parser.error("--playback-seconds must be positive")
    if args.wwise_pid <= 0:
        parser.error("--wwise-pid must be positive")
    if args.gui_timeout <= 0:
        parser.error("--gui-timeout must be positive")
    if not args.capture_file.lower().endswith(".prof"):
        parser.error("--capture-file must end in .prof")
    return args


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    report = {
        "schemaVersion": 1,
        "tool": "RealWorldWeatherAcoustics.WwiseAuthoringSmokeClient",
        "startedAtUtc": utc_now(),
        "finishedAtUtc": None,
        "success": False,
        "inputs": {
            "projectPath": str(Path(args.project).resolve()),
            "waapiUrl": args.waapi_url,
            "sourceClassId": args.source_class_id,
            "playbackSeconds": args.playback_seconds,
            "silenceFloorDb": args.silence_floor_db,
            "wwiseProcessId": args.wwise_pid,
            "guiTimeoutSeconds": args.gui_timeout,
        },
        "waapiCompatibility": {
            "wwiseVersion": "2023.1.19.8928",
            "projectInfoUri": PROJECT_INFO_URI,
            "undoUri": UNDO_URI,
            "note": (
                "Wwise 2023.1 exposes ak.wwise.core.getProjectInfo; "
                "ak.wwise.core.project.getInfo is not present in its installed schema."
            ),
        },
        "steps": [],
        "assertions": {},
        "cleanup": {},
        "error": None,
    }
    exit_code = 1
    try:
        run_smoke(args, report)
        exit_code = 0 if report.get("success") else 1
    except Exception as error:
        report["success"] = False
        report["error"] = {
            "type": type(error).__name__,
            "message": str(error),
            "traceback": traceback.format_exc(),
        }
        record_step(report, "smoke", "failed", str(error))
    finally:
        report["finishedAtUtc"] = utc_now()
        write_report(args.report, report)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
