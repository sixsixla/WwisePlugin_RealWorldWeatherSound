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
import shutil
import struct
import sys
import time
import traceback
import xml.etree.ElementTree as ElementTree
import wave
from ctypes import wintypes
from datetime import datetime, timezone
from pathlib import Path

from waapi import CannotConnectToWaapiException, WaapiClient


SMOKE_NAME = "RWWA_Smoke"
SOURCE_NAME = "RWWA_Smoke_Source"
EFFECT_SOUND_NAME = "RWWA_Smoke_Effect_Sound"
EFFECT_AUDIO_SOURCE_NAME = "RWWA_Smoke_Audio_File_Source"
EFFECT_NAME = "RWWA_Smoke_Geometry_Effect"
RETAINED_EFFECT_SOUND_NAME = "RWWA_Demo_Heavy_Rain_Puddles"
RETAINED_EFFECT_AUDIO_SOURCE_NAME = "RWWA_Demo_Heavy_Rain_Puddles_Audio"
RETAINED_EFFECT_NAME = "RWWA_Demo_Weather_Geometry_Effect"
DEFAULT_SOURCE_CLASS_ID = 2031682562
DEFAULT_EFFECT_CLASS_ID = 2031748099
DEFAULT_EFFECT_INPUT_WAV = str(
    (
        Path(__file__).resolve().parents[2]
        / "WwiseSmoke"
        / "RealWorldWeatherAcousticsSmoke"
        / "Originals"
        / "SFX"
        / "RWWA_Heavy_Rain_Puddles_30s.wav"
    ).resolve()
)
DEFAULT_NATIVE_HOST_FIXTURE_DIR = str(
    (Path(__file__).resolve().parents[2] / "Build" / "NativeHost" / "Fixture").resolve()
)
RETAINED_RAIN_WAV_NAME = "RWWA_Heavy_Rain_Puddles_30s.wav"
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
EFFECT_BANK_PARAMETER_BYTES = 281
EFFECT_BANK_PROPERTY_COUNT = 71
EFFECT_INPUT_ROLE_BANK_OFFSET = 0
EFFECT_WET_MIX_BANK_OFFSET = 4
EFFECT_GEOMETRY_ENABLED_BANK_OFFSET = 36
SOUNDBANK_EVENT_NAME = "RWWA_Smoke_Bank_Event"
SOUNDBANK_TRUE_NAME = "RWWA_GeometryTrue"
SOUNDBANK_FALSE_NAME = "RWWA_GeometryFalse"
EFFECT_SOUNDBANK_EVENT_NAME = "RWWA_Smoke_Effect_Bank_Event"
EFFECT_SOUNDBANK_BASELINE_NAME = "RWWA_Effect_Baseline"
EFFECT_SOUNDBANK_VARIANT_NAME = "RWWA_Effect_InputRoleWetGeometry"
EFFECT_SOUNDBANK_WET_ZERO_NAME = "RWWA_Effect_WetZero"

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
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
GA_ROOT = 2


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
    user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    user32.ClientToScreen.restype = wintypes.BOOL
    user32.GetCursorPos.argtypes = [ctypes.POINTER(wintypes.POINT)]
    user32.GetCursorPos.restype = wintypes.BOOL
    user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
    user32.SetCursorPos.restype = wintypes.BOOL
    user32.GetAncestor.argtypes = [wintypes.HWND, wintypes.UINT]
    user32.GetAncestor.restype = wintypes.HWND
    user32.SetForegroundWindow.argtypes = [wintypes.HWND]
    user32.SetForegroundWindow.restype = wintypes.BOOL
    user32.mouse_event.argtypes = [
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.DWORD,
        ctypes.c_size_t,
    ]
    user32.mouse_event.restype = None
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
            "duration": ("60",),
            "rainIntensity": ("0.75",),
            "seed": ("24681357",),
            "listenerYaw": ("20", "20.0"),
            "featureCount": ("4 / 8",),
            "windSpeed": ("14", "14.0"),
            "windDirection": ("35", "35.0"),
            "windGustiness": ("0.65",),
            "featureX": ("0",),
            "featureZ": ("6",),
            "featureRadius": ("2.5", "2.50"),
            "featureMask": ("3", "Rain + Wind"),
            "featurePriority": ("10",),
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
                states[name]["text"] in expected
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


def wait_for_control_text(user32, control_handle, expected_text, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    last_text = None
    while True:
        last_text = window_text(user32, control_handle)
        if last_text == expected_text:
            return last_text
        if time.monotonic() >= deadline:
            raise SmokeAssertionError(
                "Control {0} did not display '{1}' within {2:.1f}s; actual='{3}'.".format(
                    control_handle, expected_text, timeout_seconds, last_text
                )
            )
        time.sleep(0.1)


def utc_now():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def inspect_effect_input_wav(path):
    path = Path(path).resolve(strict=True)
    with wave.open(str(path), "rb") as reader:
        channels = reader.getnchannels()
        sample_width_bytes = reader.getsampwidth()
        sample_rate = reader.getframerate()
        frame_count = reader.getnframes()
        compression_type = reader.getcomptype()
    duration_seconds = frame_count / float(sample_rate)
    is_retained_rain_asset = path.name.casefold() == RETAINED_RAIN_WAV_NAME.casefold()
    required_assertions = {
        "isPcm": compression_type == "NONE",
        "hasSupportedChannelCount": 1 <= channels <= 8,
        "hasSupportedSampleRate": 8000 <= sample_rate <= 192000,
        "hasSupportedBitDepth": sample_width_bytes in (1, 2, 3, 4),
        "hasPositiveDuration": duration_seconds > 0.0,
        "isNonEmpty": path.stat().st_size > 44,
    }
    retained_rain_assertions = {
        "isStereo": channels == 2,
        "is48Khz": sample_rate == 48000,
        "is24Bit": sample_width_bytes == 3,
        "isExactlyThirtySeconds": abs(duration_seconds - 30.0) <= (1.0 / sample_rate),
    }
    if is_retained_rain_asset:
        required_assertions.update(retained_rain_assertions)
    return {
        "path": str(path),
        "fileName": path.name,
        "bytes": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "channels": channels,
        "sampleRate": sample_rate,
        "bitsPerSample": sample_width_bytes * 8,
        "frameCount": frame_count,
        "durationSeconds": duration_seconds,
        "isRetainedRainAsset": is_retained_rain_asset,
        "requiredAssertions": required_assertions,
        "retainedRainProfile": retained_rain_assertions,
    }


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


def call_when_project_ready(
    client, report, uri, args=None, options=None, timeout_seconds=30.0
):
    deadline = time.monotonic() + timeout_seconds
    attempts = 0
    last_error = None
    while time.monotonic() < deadline:
        attempts += 1
        try:
            result = call(client, report, uri, args=args, options=options)
            record_step(
                report,
                "waitForProjectReady",
                "passed",
                {"attempts": attempts, "uri": uri},
            )
            return result
        except Exception as error:
            last_error = error
            serialized = str(error).casefold()
            if "ak.wwise.locked" not in serialized and "loading project" not in serialized:
                raise
            time.sleep(0.25)
    raise RuntimeError(
        "Wwise project did not become WAAPI-ready within {0:.1f} seconds: {1}".format(
            timeout_seconds, last_error
        )
    )


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


def effect_smoke_properties():
    properties = {
        "InputRole": 0,
        "WetMix": 1.0,
        "ResponseGainDb": 10.0,
        "TransientSensitivity": 0.85,
        "RainIntensity": 0.9,
        "WindSpeed": 0.0,
        "WindDirectionDegrees": 0.0,
        "WindGustiness": 0.0,
        "Seed": 97531,
        "GeometryEnabled": True,
        "ListenerX": 0.0,
        "ListenerY": 0.0,
        "ListenerZ": 0.0,
        "ListenerYawDegrees": 0.0,
        "FeatureCount": 4,
    }
    features = (
        (0.0, 0.0, 5.5, 3.2, 3, 1, 10),
        (5.5, 0.0, 0.0, 3.2, 4, 1, 10),
        (0.0, 0.0, -5.5, 3.2, 0, 1, 10),
        (-5.5, 0.0, 0.0, 3.2, 1, 1, 10),
        (0.0, 0.0, 0.0, 2.0, 0, 3, 1),
        (0.0, 0.0, 0.0, 2.0, 1, 3, 1),
        (0.0, 0.0, 0.0, 2.0, 2, 3, 1),
        (0.0, 0.0, 0.0, 2.0, 3, 3, 1),
    )
    for index, feature in enumerate(features, start=1):
        x_value, y_value, z_value, radius, profile, mask, priority = feature
        properties["Feature{0}X".format(index)] = x_value
        properties["Feature{0}Y".format(index)] = y_value
        properties["Feature{0}Z".format(index)] = z_value
        properties["Feature{0}Radius".format(index)] = radius
        properties["Feature{0}Profile".format(index)] = profile
        properties["Feature{0}Mask".format(index)] = mask
        properties["Feature{0}Priority".format(index)] = priority
    return properties


def effect_smoke_definition(effect_class_id):
    effect = {
        "type": "Effect",
        "name": EFFECT_NAME,
        "classId": effect_class_id,
    }
    effect.update(
        {"@" + name: value for name, value in effect_smoke_properties().items()}
    )
    return effect


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


def read_wwise_object(client, object_id, return_names):
    result = client.call(
        "ak.wwise.core.object.get",
        {"from": {"id": [object_id]}},
        options={"return": list(return_names)},
    )
    objects = result.get("return", []) if result else []
    if len(objects) != 1:
        raise RuntimeError(
            "object.get returned {0} objects while reading {1}.".format(
                len(objects), object_id
            )
        )
    return objects[0]


def read_plugin_properties(client, object_id, property_names):
    result = client.call(
        "ak.wwise.core.object.get",
        {"from": {"id": [object_id]}},
        options={"return": ["id"] + ["@" + name for name in property_names]},
    )
    objects = result.get("return", []) if result else []
    if len(objects) != 1:
        raise RuntimeError(
            "object.get returned {0} objects while reading plug-in object {1}.".format(
                len(objects), object_id
            )
        )
    plugin = objects[0]
    values = {}
    for name in property_names:
        key = "@" + name
        if key not in plugin:
            raise RuntimeError("object.get omitted plug-in property '{0}'.".format(key))
        values[name] = plugin[key]
    return values


def read_source_properties(client, source_id, property_names):
    return read_plugin_properties(client, source_id, property_names)


def validate_effect_smoke_object(
    client,
    report,
    effect_class_id,
    input_wav,
    sound_id,
    audio_source_id,
    effect_id,
    mode,
    sound_name,
    audio_source_name,
    effect_name,
    effect_slot_id=None,
):
    input_wav = Path(input_wav).resolve(strict=True)
    sound_readback = read_wwise_object(
        client,
        sound_id,
        ["id", "name", "type", "@IsLoopingEnabled", "@IsStreamingEnabled"],
    )
    audio_source_readback = read_wwise_object(
        client,
        audio_source_id,
        ["id", "name", "type", "originalFilePath"],
    )
    effect_readback = read_wwise_object(
        client,
        effect_id,
        ["id", "name", "type", "classId"],
    )
    expected_properties = effect_smoke_properties()
    actual_properties = read_plugin_properties(
        client, effect_id, list(expected_properties)
    )
    property_assertions = {
        name: property_values_match(actual_properties[name], expected_value)
        for name, expected_value in expected_properties.items()
    }
    core_property_names = (
        "InputRole",
        "WetMix",
        "ResponseGainDb",
        "TransientSensitivity",
        "GeometryEnabled",
    )
    original_file_path = audio_source_readback.get("originalFilePath")
    assertions = {
        "soundCreatedAndReadable": (
            sound_readback.get("id") == sound_id
            and sound_readback.get("name") == sound_name
            and sound_readback.get("type") == "Sound"
        ),
        "audioFileSourceCreatedAndReadable": (
            audio_source_readback.get("id") == audio_source_id
            and audio_source_readback.get("name") == audio_source_name
            and audio_source_readback.get("type") == "AudioFileSource"
        ),
        "audioFileSourceReferencesImportedWav": (
            bool(original_file_path)
            and Path(str(original_file_path)).name.casefold() == input_wav.name.casefold()
        ),
        "loopingEnabled": sound_readback.get("@IsLoopingEnabled") is True,
        "streamingEnabled": sound_readback.get("@IsStreamingEnabled") is True,
        "effectCreatedAndReadable": (
            effect_readback.get("id") == effect_id
            and effect_readback.get("name") == effect_name
            and effect_readback.get("type") == "Effect"
        ),
        "effectClassIdMatches": property_values_match(
            effect_readback.get("classId"), effect_class_id
        ),
        "readAll71EffectProperties": len(actual_properties)
        == EFFECT_BANK_PROPERTY_COUNT,
        "allEffectPropertiesMatchStablePreset": all(property_assertions.values()),
        "coreEffectPropertiesMatchStablePreset": all(
            property_assertions[name] for name in core_property_names
        ),
    }
    object_report = {
        "mode": mode,
        "parent": "\\Actor-Mixer Hierarchy\\Default Work Unit",
        "soundName": sound_name,
        "soundId": sound_id,
        "audioSourceName": audio_source_name,
        "audioSourceId": audio_source_id,
        "effectSlotId": effect_slot_id,
        "effectName": effect_name,
        "effectId": effect_id,
        "effectClassId": effect_class_id,
        "inputWav": str(input_wav),
        "soundReadback": sound_readback,
        "audioSourceReadback": audio_source_readback,
        "effectReadback": effect_readback,
        "effectProperties": actual_properties,
        "effectPropertyAssertions": property_assertions,
        "assertions": assertions,
    }
    report["effectObjectSet"] = object_report
    if not all(assertions.values()):
        raise SmokeAssertionError(
            "Effect object.set/readback assertions failed: {0}.".format(
                [name for name, passed in assertions.items() if not passed]
            )
        )
    record_step(report, "effectObjectSet", "passed", assertions)
    return sound_id, audio_source_id, effect_id, all(assertions.values())


def create_effect_smoke_object(client, report, effect_class_id, input_wav):
    input_wav = Path(input_wav).resolve(strict=True)
    sound_definition = {
        "type": "Sound",
        "name": EFFECT_SOUND_NAME,
        "notes": "Owned by the automated RealWorld Weather Acoustics Effect smoke test.",
        "@IsLoopingEnabled": True,
        "@IsLoopingInfinite": True,
        "@IsStreamingEnabled": True,
        "children": [
            {
                "type": "AudioFileSource",
                "name": EFFECT_AUDIO_SOURCE_NAME,
                "import": {"files": [{"audioFile": str(input_wav)}]},
            }
        ],
        "@Effects": [
            {
                "type": "EffectSlot",
                "name": "",
                "@Effect": effect_smoke_definition(effect_class_id),
            }
        ],
    }
    result = call(
        client,
        report,
        "ak.wwise.core.object.set",
        {
            "objects": [
                {
                    "object": "\\Actor-Mixer Hierarchy\\Default Work Unit",
                    "children": [sound_definition],
                }
            ],
            "onNameConflict": "replace",
            "listMode": "replaceAll",
            "autoAddToSourceControl": False,
        },
        {"return": ["id", "name", "type", "classId"]},
    )
    try:
        sound = result["objects"][0]["children"][0]
        audio_source = sound["children"][0]
        effect_slot = sound["@Effects"][0]
        effect = effect_slot["@Effect"]
        sound_id = sound["id"]
        audio_source_id = audio_source["id"]
        effect_slot_id = effect_slot["id"]
        effect_id = effect["id"]
    except (KeyError, IndexError, TypeError) as error:
        raise RuntimeError(
            "object.set did not return the Effect Sound, AudioFileSource, EffectSlot, "
            "and Effect IDs: {0}".format(error)
        )
    return validate_effect_smoke_object(
        client,
        report,
        effect_class_id,
        input_wav,
        sound_id,
        audio_source_id,
        effect_id,
        "replace",
        EFFECT_SOUND_NAME,
        EFFECT_AUDIO_SOURCE_NAME,
        EFFECT_NAME,
        effect_slot_id,
    )


def read_retained_effect_smoke_object(
    client,
    report,
    effect_class_id,
    input_wav,
    sound_id,
    audio_source_id,
    effect_id,
):
    return validate_effect_smoke_object(
        client,
        report,
        effect_class_id,
        input_wav,
        sound_id,
        audio_source_id,
        effect_id,
        "existing-template",
        RETAINED_EFFECT_SOUND_NAME,
        RETAINED_EFFECT_AUDIO_SOURCE_NAME,
        RETAINED_EFFECT_NAME,
    )


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


def poll_properties(client, source_id, property_names, predicate, timeout_seconds=2.0):
    deadline = time.monotonic() + timeout_seconds
    last_values = None
    while time.monotonic() < deadline:
        last_values = read_source_properties(client, source_id, property_names)
        if predicate(last_values):
            return last_values, True
        time.sleep(0.05)
    return last_values, False


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


def effect_bank_property_layout():
    layout = [
        ("InputRole", "i"),
        ("WetMix", "f"),
        ("ResponseGainDb", "f"),
        ("TransientSensitivity", "f"),
        ("RainIntensity", "f"),
        ("WindSpeed", "f"),
        ("WindDirectionDegrees", "f"),
        ("WindGustiness", "f"),
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
    if len(layout) != EFFECT_BANK_PROPERTY_COUNT:
        raise RuntimeError(
            "Effect bank property layout contains {0} entries instead of {1}.".format(
                len(layout), EFFECT_BANK_PROPERTY_COUNT
            )
        )
    return layout


def pack_effect_bank_parameters(property_values):
    chunks = []
    for property_name, value_format in effect_bank_property_layout():
        if property_name not in property_values:
            raise RuntimeError(
                "Cannot pack Effect bank parameters without '{0}'.".format(property_name)
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
    if len(block) != EFFECT_BANK_PARAMETER_BYTES:
        raise RuntimeError(
            "Packed Effect parameter block is {0} bytes instead of {1}.".format(
                len(block), EFFECT_BANK_PARAMETER_BYTES
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


def set_plugin_property(client, report, object_id, property_name, value):
    call(
        client,
        report,
        "ak.wwise.core.object.setProperty",
        {"object": object_id, "property": property_name, "value": value},
    )


def set_source_property(client, report, source_id, property_name, value):
    set_plugin_property(client, report, source_id, property_name, value)


def create_soundbank_serialization_event(
    client, report, sound_id, event_name=SOUNDBANK_EVENT_NAME
):
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
                            "name": event_name,
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


def retain_effect_bank_immediately(bank_path, destination_root):
    bank_path = Path(bank_path).resolve(strict=True)
    product_root = Path(__file__).resolve().parents[2]
    destination_root = Path(destination_root).resolve()
    try:
        destination_root.relative_to(product_root)
    except ValueError:
        raise RuntimeError(
            "Native Host fixture destination must remain under the repository root "
            "'{0}': '{1}'.".format(product_root, destination_root)
        )
    destination_root.mkdir(parents=True, exist_ok=True)
    retained_path = (destination_root / bank_path.name).resolve()
    try:
        retained_path.relative_to(destination_root)
    except ValueError:
        raise RuntimeError(
            "Refusing to retain Effect SoundBank outside '{0}': '{1}'.".format(
                destination_root, retained_path
            )
        )
    shutil.copy2(str(bank_path), str(retained_path))
    return retained_path


def generate_effect_soundbank_serialization_variant(
    client,
    report,
    project_path,
    effect_id,
    event_id,
    variant_name,
    variant_properties,
    native_host_fixture_dir,
):
    for property_name, value in variant_properties.items():
        set_plugin_property(client, report, effect_id, property_name, value)
    wait_for_properties(client, effect_id, variant_properties)

    property_layout = effect_bank_property_layout()
    property_names = [name for name, _ in property_layout]
    property_values = read_plugin_properties(client, effect_id, property_names)
    expected_block = pack_effect_bank_parameters(property_values)
    generation_result = call(
        client,
        report,
        "ak.wwise.core.soundbank.generate",
        {
            "soundbanks": [
                {
                    "name": variant_name,
                    "events": [event_id],
                    "inclusions": ["event", "structure", "media"],
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
            "Generated Effect SoundBank path escaped the disposable project: "
            "'{0}'.".format(bank_path)
        )
    if not bank_path.is_file():
        raise SmokeAssertionError(
            "Effect SoundBank generation did not write '{0}'.".format(bank_path)
        )

    bank_bytes = bank_path.read_bytes()
    block_offsets = byte_match_offsets(bank_bytes, expected_block)
    matched_block = (
        bank_bytes[
            block_offsets[0] : block_offsets[0] + EFFECT_BANK_PARAMETER_BYTES
        ]
        if len(block_offsets) == 1
        else None
    )
    assertions = {
        "readAll71EffectProperties": len(property_values)
        == EFFECT_BANK_PROPERTY_COUNT,
        "expectedBlockIs281Bytes": len(expected_block)
        == EFFECT_BANK_PARAMETER_BYTES,
        "expectedBlockMatchesExactlyOnce": len(block_offsets) == 1,
        "inputRoleBytesMatchVariant": (
            matched_block is not None
            and matched_block[
                EFFECT_INPUT_ROLE_BANK_OFFSET : EFFECT_INPUT_ROLE_BANK_OFFSET + 4
            ]
            == struct.pack("<i", int(property_values["InputRole"]))
        ),
        "wetMixBytesMatchVariant": (
            matched_block is not None
            and matched_block[
                EFFECT_WET_MIX_BANK_OFFSET : EFFECT_WET_MIX_BANK_OFFSET + 4
            ]
            == struct.pack("<f", float(property_values["WetMix"]))
        ),
        "geometryByteMatchesVariant": (
            matched_block is not None
            and matched_block[EFFECT_GEOMETRY_ENABLED_BANK_OFFSET]
            == (1 if property_values["GeometryEnabled"] else 0)
        ),
        "generationLogsHaveNoWarningErrorOrFatal": not bad_logs,
    }
    variant_report = {
        "name": variant_name,
        "variantProperties": dict(variant_properties),
        "effectPropertyCount": len(property_values),
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
        "inputRoleBlockOffset": EFFECT_INPUT_ROLE_BANK_OFFSET,
        "wetMixBlockOffset": EFFECT_WET_MIX_BANK_OFFSET,
        "geometryByteBlockOffset": EFFECT_GEOMETRY_ENABLED_BANK_OFFSET,
        "generationLogCount": len(logs),
        "generationLogSeverities": [item.get("severity") for item in logs],
        "rejectedGenerationLogs": bad_logs,
        "assertions": assertions,
    }
    if not all(assertions.values()):
        raise SmokeAssertionError(
            "Effect SoundBank serialization assertions failed for {0}: {1}.".format(
                variant_name,
                [name for name, passed in assertions.items() if not passed],
            )
        )
    retained_bank_path = retain_effect_bank_immediately(
        bank_path, native_host_fixture_dir
    )
    retained_bank_bytes = retained_bank_path.read_bytes()
    variant_report["retainedBankPath"] = str(retained_bank_path)
    variant_report["retainedBankBytes"] = len(retained_bank_bytes)
    variant_report["retainedBankSha256"] = hashlib.sha256(
        retained_bank_bytes
    ).hexdigest()
    variant_report["assertions"]["retainedBankMatchesGeneratedBank"] = (
        retained_bank_bytes == bank_bytes
    )
    if not variant_report["assertions"]["retainedBankMatchesGeneratedBank"]:
        raise SmokeAssertionError(
            "Retained Effect SoundBank differs from generated bank '{0}'.".format(
                bank_path
            )
        )
    return variant_report, matched_block


def retain_native_host_fixture(
    project_path,
    destination_root,
    effect_class_id,
    input_wav,
    sound_id,
    audio_source_id,
    effect_id,
    bank_report,
):
    generated_root = (project_path.parent / "GeneratedSoundBanks" / "Windows").resolve()
    if not generated_root.is_dir():
        raise SmokeAssertionError(
            "Generated SoundBanks root is missing before Native Host fixture retention: "
            "'{0}'.".format(generated_root)
        )
    product_root = Path(__file__).resolve().parents[2]
    destination_root = Path(destination_root).resolve()
    try:
        destination_root.relative_to(product_root)
    except ValueError:
        raise RuntimeError(
            "Native Host fixture destination must remain under the repository root "
            "'{0}': '{1}'.".format(product_root, destination_root)
        )

    effect_variants = [
        bank_report["variants"]["baseline"],
        bank_report["variants"]["inputRoleWetGeometry"],
        bank_report["variants"]["wetZero"],
    ]
    effect_bank_paths = [
        Path(variant["retainedBankPath"]).resolve(strict=True)
        for variant in effect_variants
    ]
    init_bank_path = (generated_root / "Init.bnk").resolve()
    metadata_relative_paths = [
        Path("Init.txt"),
        Path("PlatformInfo.xml"),
        Path("PluginInfo.xml"),
        Path(EFFECT_SOUNDBANK_WET_ZERO_NAME + ".txt"),
        Path("SoundbanksInfo.xml"),
    ]
    metadata_paths = [
        (generated_root / relative_path).resolve()
        for relative_path in metadata_relative_paths
    ]
    soundbanks_info_path = (generated_root / "SoundbanksInfo.xml").resolve()
    wet_zero_soundbanks = []
    wet_zero_media_entries = []
    wet_zero_event_media_ids = set()
    if soundbanks_info_path.is_file():
        try:
            soundbanks_info_root = ElementTree.parse(
                str(soundbanks_info_path)
            ).getroot()
        except ElementTree.ParseError as error:
            raise SmokeAssertionError(
                "Generated SoundbanksInfo.xml is not valid XML: {0}.".format(error)
            )
        wet_zero_soundbanks = [
            soundbank
            for soundbank in soundbanks_info_root.findall(".//SoundBank")
            if soundbank.findtext("ShortName") == EFFECT_SOUNDBANK_WET_ZERO_NAME
        ]
        if len(wet_zero_soundbanks) == 1:
            wet_zero_media_entries = wet_zero_soundbanks[0].findall("./Media/File")
            matching_events = [
                event
                for event in wet_zero_soundbanks[0].findall("./Events/Event")
                if event.get("Name") == EFFECT_SOUNDBANK_EVENT_NAME
            ]
            if len(matching_events) == 1:
                wet_zero_event_media_ids = {
                    media_ref.get("Id")
                    for media_ref in matching_events[0].findall("./MediaRefs/MediaRef")
                }

    wem_paths = []
    for media_entry in wet_zero_media_entries:
        media_relative_path = media_entry.findtext("Path")
        if not media_relative_path:
            continue
        media_path = (generated_root / Path(media_relative_path)).resolve()
        try:
            media_path.relative_to(generated_root)
        except ValueError:
            raise RuntimeError(
                "SoundbanksInfo.xml linked Effect media outside the generated root: "
                "'{0}'.".format(media_path)
            )
        wem_paths.append(media_path)
    wem_paths = sorted(
        {path for path in wem_paths}, key=lambda path: str(path).casefold()
    )
    wet_zero_media_ids = {entry.get("Id") for entry in wet_zero_media_entries}
    fixture_linkage = {
        "soundBankName": EFFECT_SOUNDBANK_WET_ZERO_NAME,
        "eventName": EFFECT_SOUNDBANK_EVENT_NAME,
        "inputWavName": Path(input_wav).name,
        "wemRelativePaths": [
            path.relative_to(generated_root).as_posix() for path in wem_paths
        ],
        "metadataRelativePaths": [path.as_posix() for path in metadata_relative_paths],
    }
    assertions = {
        "threeEffectSoundBanksRetained": (
            len(effect_bank_paths) == 3
            and len({path.name for path in effect_bank_paths}) == 3
            and all(
                path.name == variant["name"] + ".bnk"
                for path, variant in zip(effect_bank_paths, effect_variants)
            )
            and all(
                hashlib.sha256(path.read_bytes()).hexdigest()
                == variant["bankSha256"]
                for path, variant in zip(effect_bank_paths, effect_variants)
            )
        ),
        "initSoundBankPresent": init_bank_path.is_file(),
        "wetZeroMetadataPresent": all(path.is_file() for path in metadata_paths),
        "wetZeroSoundBankMetadataMatched": (
            len(wet_zero_soundbanks) == 1
            and wet_zero_soundbanks[0].findtext("Path")
            == EFFECT_SOUNDBANK_WET_ZERO_NAME + ".bnk"
        ),
        "wetZeroStreamedWemLinked": (
            bool(wem_paths)
            and all(path.is_file() and path.suffix.casefold() == ".wem" for path in wem_paths)
            and all(
                entry.get("Streaming", "").casefold() == "true"
                and entry.get("Location", "").casefold() == "loose"
                and entry.findtext("ShortName") == Path(input_wav).name
                for entry in wet_zero_media_entries
            )
        ),
        "wetZeroEventMediaReferencesMatch": (
            bool(wet_zero_media_ids)
            and wet_zero_event_media_ids == wet_zero_media_ids
        ),
    }
    if not all(assertions.values()):
        raise SmokeAssertionError(
            "Generated Effect fixture is not executable by the Native Host: {0}.".format(
                [name for name, passed in assertions.items() if not passed]
            )
        )

    selected_paths = {}
    for path in [init_bank_path] + wem_paths + metadata_paths:
        try:
            path.relative_to(generated_root)
        except ValueError:
            raise RuntimeError(
                "Native Host fixture source escaped the generated output root: "
                "'{0}'.".format(path)
            )
        selected_paths[canonical_path(str(path))] = path

    destination_root.mkdir(parents=True, exist_ok=True)
    copied_files = []
    for retained_path, variant in zip(effect_bank_paths, effect_variants):
        retained_bytes = retained_path.read_bytes()
        retained_sha256 = hashlib.sha256(retained_bytes).hexdigest()
        copy_matches_source = (
            len(retained_bytes) == variant["bankBytes"]
            and retained_sha256 == variant["bankSha256"]
        )
        copied_files.append(
            {
                "kind": "SoundBank",
                "relativePath": retained_path.name,
                "sourcePath": variant["bankPath"],
                "path": str(retained_path),
                "sourceSizeBytes": variant["bankBytes"],
                "sourceSha256": variant["bankSha256"],
                "destinationSizeBytes": len(retained_bytes),
                "destinationSha256": retained_sha256,
                "copyMatchesSource": copy_matches_source,
                "bytes": len(retained_bytes),
                "sha256": retained_sha256,
            }
        )
    for source_path in sorted(selected_paths.values(), key=lambda path: str(path).casefold()):
        relative_path = source_path.relative_to(generated_root)
        destination_path = (destination_root / relative_path).resolve()
        try:
            destination_path.relative_to(destination_root)
        except ValueError:
            raise RuntimeError(
                "Refusing to copy Native Host fixture artifact outside '{0}': "
                "'{1}'.".format(destination_root, destination_path)
            )
        destination_path.parent.mkdir(parents=True, exist_ok=True)
        source_bytes = source_path.read_bytes()
        source_sha256 = hashlib.sha256(source_bytes).hexdigest()
        shutil.copy2(str(source_path), str(destination_path))
        suffix = source_path.suffix.casefold()
        kind = "SoundBank" if suffix == ".bnk" else "Wem" if suffix == ".wem" else "Metadata"
        destination_bytes = destination_path.read_bytes()
        destination_sha256 = hashlib.sha256(destination_bytes).hexdigest()
        copy_matches_source = (
            len(destination_bytes) == len(source_bytes)
            and destination_sha256 == source_sha256
        )
        copied_files.append(
            {
                "kind": kind,
                "relativePath": relative_path.as_posix(),
                "sourcePath": str(source_path),
                "path": str(destination_path),
                "sourceSizeBytes": len(source_bytes),
                "sourceSha256": source_sha256,
                "destinationSizeBytes": len(destination_bytes),
                "destinationSha256": destination_sha256,
                "copyMatchesSource": copy_matches_source,
                "bytes": len(destination_bytes),
                "sha256": destination_sha256,
            }
        )

    assertions["allSelectedArtifactsCopied"] = all(
        Path(item["path"]).is_file()
        and item["sourceSizeBytes"] == item["destinationSizeBytes"]
        and item["sourceSha256"] == item["destinationSha256"]
        and item["copyMatchesSource"]
        and Path(item["path"]).stat().st_size == item["destinationSizeBytes"]
        and hashlib.sha256(Path(item["path"]).read_bytes()).hexdigest()
        == item["destinationSha256"]
        for item in copied_files
    )

    manifest_path = (destination_root / "RWWA_Effect_Fixture.json").resolve()
    manifest = {
        "schemaVersion": 1,
        "generatedAtUtc": utc_now(),
        "effect": {
            "name": EFFECT_NAME,
            "classId": effect_class_id,
            "pluginId": 31002,
            "soundId": sound_id,
            "audioFileSourceId": audio_source_id,
            "effectId": effect_id,
            "inputWav": str(Path(input_wav).resolve(strict=True)),
        },
        "serialization": {
            "parameterPropertyCount": bank_report["parameterPropertyCount"],
            "parameterBlockBytes": bank_report["parameterBlockBytes"],
            "parameterBlockDifferingOffsets": bank_report.get(
                "parameterBlockDifferingOffsets", []
            ),
            "wetZeroParameterBlockDifferingOffsets": bank_report.get(
                "wetZeroParameterBlockDifferingOffsets", []
            ),
            "variants": bank_report["variants"],
        },
        "linkage": fixture_linkage,
        "files": copied_files,
        "assertions": dict(assertions),
    }
    temporary_manifest_path = manifest_path.with_suffix(manifest_path.suffix + ".tmp")
    temporary_manifest_path.write_text(
        json.dumps(json_safe(manifest), indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    os.replace(str(temporary_manifest_path), str(manifest_path))
    manifest_bytes = manifest_path.read_bytes()
    copied_files.append(
        {
            "kind": "Metadata",
            "relativePath": manifest_path.name,
            "sourcePath": None,
            "path": str(manifest_path),
            "sourceSizeBytes": None,
            "sourceSha256": None,
            "destinationSizeBytes": len(manifest_bytes),
            "destinationSha256": hashlib.sha256(manifest_bytes).hexdigest(),
            "copyMatchesSource": None,
            "bytes": len(manifest_bytes),
            "sha256": hashlib.sha256(manifest_bytes).hexdigest(),
        }
    )
    assertions.update(
        {
            "fixtureManifestWritten": manifest_path.is_file()
            and manifest_path.stat().st_size > 0,
        }
    )
    return {
        "root": str(destination_root),
        "manifestPath": str(manifest_path),
        "linkage": fixture_linkage,
        "files": copied_files,
        "assertions": assertions,
    }


def run_effect_soundbank_serialization_smoke(
    client,
    report,
    project_path,
    sound_id,
    audio_source_id,
    effect_id,
    effect_class_id,
    input_wav,
    native_host_fixture_dir,
):
    stable_properties = effect_smoke_properties()
    baseline_properties = {
        "InputRole": stable_properties["InputRole"],
        "WetMix": stable_properties["WetMix"],
        "GeometryEnabled": stable_properties["GeometryEnabled"],
    }
    variant_properties = {
        "InputRole": 1,
        "WetMix": 0.2,
        "GeometryEnabled": False,
    }
    wet_zero_properties = {
        "InputRole": 2,
        "WetMix": 0.0,
        "GeometryEnabled": True,
    }
    bank_report = {
        "parameterPropertyCount": EFFECT_BANK_PROPERTY_COUNT,
        "parameterBlockBytes": EFFECT_BANK_PARAMETER_BYTES,
        "inputRoleBlockOffset": EFFECT_INPUT_ROLE_BANK_OFFSET,
        "wetMixBlockOffset": EFFECT_WET_MIX_BANK_OFFSET,
        "geometryByteBlockOffset": EFFECT_GEOMETRY_ENABLED_BANK_OFFSET,
        "eventId": None,
        "variants": {},
        "assertions": {},
        "cleanup": {},
    }
    report["effectSoundBankSerialization"] = bank_report
    event_id = None
    operation_error = None

    try:
        event_id = create_soundbank_serialization_event(
            client, report, sound_id, EFFECT_SOUNDBANK_EVENT_NAME
        )
        bank_report["eventId"] = event_id
        baseline_report, baseline_block = generate_effect_soundbank_serialization_variant(
            client,
            report,
            project_path,
            effect_id,
            event_id,
            EFFECT_SOUNDBANK_BASELINE_NAME,
            baseline_properties,
            native_host_fixture_dir,
        )
        bank_report["variants"]["baseline"] = baseline_report
        variant_report, variant_block = generate_effect_soundbank_serialization_variant(
            client,
            report,
            project_path,
            effect_id,
            event_id,
            EFFECT_SOUNDBANK_VARIANT_NAME,
            variant_properties,
            native_host_fixture_dir,
        )
        bank_report["variants"]["inputRoleWetGeometry"] = variant_report
        wet_zero_report, wet_zero_block = generate_effect_soundbank_serialization_variant(
            client,
            report,
            project_path,
            effect_id,
            event_id,
            EFFECT_SOUNDBANK_WET_ZERO_NAME,
            wet_zero_properties,
            native_host_fixture_dir,
        )
        bank_report["variants"]["wetZero"] = wet_zero_report

        differing_offsets = [
            index
            for index, (baseline_byte, variant_byte) in enumerate(
                zip(baseline_block, variant_block)
            )
            if baseline_byte != variant_byte
        ]
        allowed_offsets = set(range(EFFECT_INPUT_ROLE_BANK_OFFSET, 8))
        allowed_offsets.add(EFFECT_GEOMETRY_ENABLED_BANK_OFFSET)
        wet_zero_differing_offsets = [
            index
            for index, (baseline_byte, wet_zero_byte) in enumerate(
                zip(baseline_block, wet_zero_block)
            )
            if baseline_byte != wet_zero_byte
        ]
        wet_zero_allowed_offsets = set(
            range(EFFECT_INPUT_ROLE_BANK_OFFSET, EFFECT_WET_MIX_BANK_OFFSET + 4)
        )
        bank_report["parameterBlockDifferingOffsets"] = differing_offsets
        bank_report["wetZeroParameterBlockDifferingOffsets"] = (
            wet_zero_differing_offsets
        )
        bank_report["assertions"].update(
            {
                "baselineVariantPassed": all(baseline_report["assertions"].values()),
                "inputRoleWetGeometryVariantPassed": all(
                    variant_report["assertions"].values()
                ),
                "inputRoleHasSerializedDifference": any(
                    EFFECT_INPUT_ROLE_BANK_OFFSET <= offset
                    < EFFECT_INPUT_ROLE_BANK_OFFSET + 4
                    for offset in differing_offsets
                ),
                "wetMixHasSerializedDifference": any(
                    EFFECT_WET_MIX_BANK_OFFSET <= offset
                    < EFFECT_WET_MIX_BANK_OFFSET + 4
                    for offset in differing_offsets
                ),
                "geometryHasSerializedDifference": (
                    EFFECT_GEOMETRY_ENABLED_BANK_OFFSET in differing_offsets
                ),
                "parameterBlocksDifferOnlyAtVariantFields": (
                    bool(differing_offsets)
                    and all(offset in allowed_offsets for offset in differing_offsets)
                ),
                "wetZeroVariantPassed": all(
                    wet_zero_report["assertions"].values()
                ),
                "wetZeroBlockIs281Bytes": len(wet_zero_block)
                == EFFECT_BANK_PARAMETER_BYTES,
                "wetZeroBlockMatchesExactlyOnce": wet_zero_report["assertions"][
                    "expectedBlockMatchesExactlyOnce"
                ],
                "wetZeroInputRoleIsGeneric": wet_zero_block[
                    EFFECT_INPUT_ROLE_BANK_OFFSET : EFFECT_INPUT_ROLE_BANK_OFFSET + 4
                ]
                == struct.pack("<i", 2),
                "wetZeroWetMixBytesAreZero": wet_zero_block[
                    EFFECT_WET_MIX_BANK_OFFSET : EFFECT_WET_MIX_BANK_OFFSET + 4
                ]
                == struct.pack("<f", 0.0),
                "wetZeroGeometryByteIsTrue": wet_zero_block[
                    EFFECT_GEOMETRY_ENABLED_BANK_OFFSET
                ]
                == 1,
                "wetZeroStableFieldsMatchBaseline": (
                    bool(wet_zero_differing_offsets)
                    and all(
                        offset in wet_zero_allowed_offsets
                        for offset in wet_zero_differing_offsets
                    )
                ),
            }
        )
        if not all(bank_report["assertions"].values()):
            raise SmokeAssertionError(
                "Cross-variant Effect SoundBank serialization assertions failed: "
                "{0}.".format(
                    [
                        name
                        for name, passed in bank_report["assertions"].items()
                        if not passed
                    ]
                )
            )
        fixture_report = retain_native_host_fixture(
            project_path,
            native_host_fixture_dir,
            effect_class_id,
            input_wav,
            sound_id,
            audio_source_id,
            effect_id,
            bank_report,
        )
        bank_report["nativeHostFixture"] = fixture_report
        report["nativeHostFixture"] = fixture_report
        bank_report["assertions"]["nativeHostFixtureRetained"] = all(
            fixture_report["assertions"].values()
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
                cleanup_errors.append("temporary Effect event: {0}".format(error))
                bank_report["cleanup"]["temporaryEventDeleted"] = False
        try:
            for property_name, value in baseline_properties.items():
                set_plugin_property(client, report, effect_id, property_name, value)
            restored_values = wait_for_properties(
                client, effect_id, baseline_properties
            )
            properties_restored = all(
                property_values_match(restored_values[name], value)
                for name, value in baseline_properties.items()
            )
            bank_report["cleanup"]["stablePropertiesRestored"] = properties_restored
            bank_report["assertions"]["stablePropertiesRestored"] = properties_restored
        except Exception as error:
            cleanup_errors.append("Effect stable property restore: {0}".format(error))
            bank_report["cleanup"]["stablePropertiesRestored"] = False
            bank_report["assertions"]["stablePropertiesRestored"] = False
        bank_report["cleanup"]["errors"] = cleanup_errors

        if cleanup_errors:
            cleanup_error = RuntimeError(
                "Effect SoundBank serialization cleanup failed: {0}.".format(
                    "; ".join(cleanup_errors)
                )
            )
            if operation_error is None:
                operation_error = cleanup_error
            else:
                operation_error = RuntimeError(
                    "Effect SoundBank serialization failed ({0}); cleanup also failed "
                    "({1}).".format(operation_error, cleanup_error)
                )

    if operation_error is not None:
        record_step(
            report, "effectSoundBankSerialization", "failed", str(operation_error)
        )
        raise operation_error
    record_step(
        report,
        "effectSoundBankSerialization",
        "passed",
        bank_report["assertions"],
    )
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
    time.sleep(0.05)
    midpoint = {
        "x": cpp_lround((start["x"] + destination["x"]) * 0.5),
        "y": cpp_lround((start["y"] + destination["y"]) * 0.5),
    }
    send_message(
        user32,
        canvas_hwnd,
        WM_MOUSEMOVE,
        MK_LBUTTON,
        make_mouse_lparam(midpoint["x"], midpoint["y"]),
    )
    time.sleep(0.05)
    send_message(
        user32,
        canvas_hwnd,
        WM_MOUSEMOVE,
        MK_LBUTTON,
        make_mouse_lparam(destination["x"], destination["y"]),
    )
    time.sleep(0.05)
    send_message(
        user32,
        canvas_hwnd,
        WM_LBUTTONUP,
        0,
        make_mouse_lparam(destination["x"], destination["y"]),
    )


def send_physical_canvas_drag(user32, canvas_hwnd, start, destination):
    original_cursor = wintypes.POINT()
    if not user32.GetCursorPos(ctypes.byref(original_cursor)):
        raise ctypes.WinError(ctypes.get_last_error())

    start_screen = wintypes.POINT(start["x"], start["y"])
    destination_screen = wintypes.POINT(destination["x"], destination["y"])
    if not user32.ClientToScreen(canvas_hwnd, ctypes.byref(start_screen)):
        raise ctypes.WinError(ctypes.get_last_error())
    if not user32.ClientToScreen(canvas_hwnd, ctypes.byref(destination_screen)):
        raise ctypes.WinError(ctypes.get_last_error())

    root_window = user32.GetAncestor(canvas_hwnd, GA_ROOT)
    if root_window:
        user32.SetForegroundWindow(root_window)
        time.sleep(0.1)

    button_down = False
    try:
        if not user32.SetCursorPos(start_screen.x, start_screen.y):
            raise ctypes.WinError(ctypes.get_last_error())
        time.sleep(0.05)
        user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        button_down = True
        for step in range(1, 5):
            fraction = step / 4.0
            x_value = cpp_lround(
                start_screen.x + (destination_screen.x - start_screen.x) * fraction
            )
            y_value = cpp_lround(
                start_screen.y + (destination_screen.y - start_screen.y) * fraction
            )
            if not user32.SetCursorPos(x_value, y_value):
                raise ctypes.WinError(ctypes.get_last_error())
            time.sleep(0.05)
        user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        button_down = False
        time.sleep(0.05)
    finally:
        if button_down:
            user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        user32.SetCursorPos(original_cursor.x, original_cursor.y)


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

        before_priority_values = read_source_properties(
            client, source_id, ["Feature1Priority"]
        )
        set_source_property(
            client, report, source_id, "Feature1Priority", 107
        )
        priority_after_write = int(
            wait_for_property(
                client,
                source_id,
                "Feature1Priority",
                lambda value: int(value) == 107,
            )
        )
        visible_priority_after_write = wait_for_control_text(
            user32,
            controls["featurePriorityEditHandle"],
            "107",
            timeout_seconds,
        )
        priority_interaction = {
            "action": "setFeature1Priority107",
            "beforePriority": int(before_priority_values["Feature1Priority"]),
            "afterPriority": priority_after_write,
            "visibleText": visible_priority_after_write,
            "assertion": (
                int(before_priority_values["Feature1Priority"]) == 10
                and priority_after_write == 107
                and visible_priority_after_write == "107"
            ),
        }
        gui_report["interactions"].append(priority_interaction)
        gui_report["assertions"]["featurePriority107RoundTripAndVisible"] = (
            priority_interaction["assertion"]
        )
        if not priority_interaction["assertion"]:
            raise SmokeAssertionError(
                "Feature Priority 107 write/readback/visible-numeric gate failed: "
                "{0}.".format(priority_interaction)
            )
        priority_undo = perform_undo_gate(
            client,
            report,
            source_id,
            "featurePriority107",
            before_priority_values,
            {"Feature1Priority": priority_after_write},
        )
        visible_priority_after_undo = wait_for_control_text(
            user32,
            controls["featurePriorityEditHandle"],
            "10",
            timeout_seconds,
        )
        priority_undo["visibleTextAfterUndo"] = visible_priority_after_undo
        priority_undo["assertions"]["visibleNumericRestored"] = (
            visible_priority_after_undo == "10"
        )
        gui_report["undoGates"].append(priority_undo)
        gui_report["assertions"]["featurePriority107UndoRestores10"] = all(
            value
            for name, value in priority_undo["assertions"].items()
            if name != "propertiesRestored"
        ) and all(priority_undo["assertions"]["propertiesRestored"].values())

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
            "Feature5Profile": 4,
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
        move_input_mode = "window-message"
        send_canvas_drag(user32, canvas, move_start, move_destination)
        moved_values, move_readback_converged = poll_properties(
            client,
            source_id,
            ["Feature5X", "Feature5Z"],
            lambda values: (
                abs(float(values["Feature5X"]) - move_target_world["x"]) <= 0.15
                and abs(float(values["Feature5Z"]) - move_target_world["z"]) <= 0.15
            ),
        )
        if not move_readback_converged:
            move_input_mode = "physical-mouse-fallback"
            send_physical_canvas_drag(user32, canvas, move_start, move_destination)
            moved_values, move_readback_converged = poll_properties(
                client,
                source_id,
                ["Feature5X", "Feature5Z"],
                lambda values: (
                    abs(float(values["Feature5X"]) - move_target_world["x"]) <= 0.15
                    and abs(float(values["Feature5Z"]) - move_target_world["z"]) <= 0.15
                ),
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
            "inputMode": move_input_mode,
            "readbackConverged": move_readback_converged,
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
        resize_input_mode = "window-message"
        send_canvas_drag(user32, canvas, radius_handle, resize_destination)
        resized_values, resize_readback_converged = poll_properties(
            client,
            source_id,
            ["Feature5Radius"],
            lambda values: abs(
                float(values["Feature5Radius"])
                - (feature_before_resize["radius"] + 1.5)
            )
            <= 0.15,
        )
        if not resize_readback_converged:
            resize_input_mode = "physical-mouse-fallback"
            send_physical_canvas_drag(user32, canvas, radius_handle, resize_destination)
            resized_values, resize_readback_converged = poll_properties(
                client,
                source_id,
                ["Feature5Radius"],
                lambda values: abs(
                    float(values["Feature5Radius"])
                    - (feature_before_resize["radius"] + 1.5)
                )
                <= 0.15,
            )
        resized_radius = float(resized_values["Feature5Radius"])
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
            "inputMode": resize_input_mode,
            "readbackConverged": resize_readback_converged,
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


def contains_effect_identity(value, sound_id, audio_source_id, effect_id):
    identities = {
        EFFECT_SOUND_NAME.lower(),
        EFFECT_AUDIO_SOURCE_NAME.lower(),
        EFFECT_NAME.lower(),
        sound_id.lower(),
        audio_source_id.lower(),
        effect_id.lower(),
    }
    serialized = json.dumps(value, sort_keys=True).lower()
    return any(identity in serialized for identity in identities)


def is_effect_cpu_row(value, effect_class_id):
    row_id = value.get("id")
    if property_values_match(row_id, effect_class_id) or property_values_match(
        row_id, 31002
    ):
        return True
    serialized = json.dumps(value, sort_keys=True).casefold()
    return (
        (str(effect_class_id) in serialized or "31002" in serialized)
        and "effect" in serialized
    ) or (
        "effect" in str(value.get("elementName", "")).casefold()
        and (
            "realworld weather" in serialized
            or "realworldweather" in serialized
        )
    )


def run_smoke(args, report):
    project_path = Path(args.project).resolve(strict=True)
    effect_input_wav = Path(args.effect_input_wav).resolve(strict=True)
    input_wav_format = inspect_effect_input_wav(effect_input_wav)
    report["inputs"]["effectInputWavFormat"] = input_wav_format
    failed_input_assertions = [
        name
        for name, passed in input_wav_format["requiredAssertions"].items()
        if not passed
    ]
    if failed_input_assertions:
        raise RuntimeError(
            "Effect input WAV format assertions failed: {0}".format(
                failed_input_assertions
            )
        )
    client = None
    source_transport_id = None
    effect_transport_id = None
    capture_started = False
    capture_artifact_valid = False
    core_assertions_passed = False
    gui_interactions_passed = False
    soundbank_serialization_passed = False
    effect_object_passed = False
    effect_soundbank_serialization_passed = False
    mandatory_cleanup_errors = []
    cleanup_warnings = []

    try:
        client = connect_with_retry(args.waapi_url, args.connect_timeout, report)

        project_info = call_when_project_ready(
            client,
            report,
            PROJECT_INFO_URI,
            timeout_seconds=args.connect_timeout,
        )
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
        if args.retained_effect_sound_id is not None:
            (
                effect_sound_id,
                effect_audio_source_id,
                effect_id,
                effect_object_passed,
            ) = read_retained_effect_smoke_object(
                client,
                report,
                args.effect_class_id,
                effect_input_wav,
                args.retained_effect_sound_id,
                args.retained_effect_audio_source_id,
                args.retained_effect_id,
            )
        else:
            (
                effect_sound_id,
                effect_audio_source_id,
                effect_id,
                effect_object_passed,
            ) = create_effect_smoke_object(
                client,
                report,
                args.effect_class_id,
                effect_input_wav,
            )
        effect_soundbank_serialization_passed = (
            run_effect_soundbank_serialization_smoke(
                client,
                report,
                project_path,
                effect_sound_id,
                effect_audio_source_id,
                effect_id,
                args.effect_class_id,
                effect_input_wav,
                args.native_host_fixture_dir,
            )
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
        source_transport_id = transport_result.get("transport")
        if source_transport_id is None:
            raise RuntimeError("transport.create did not return a Source transport ID")
        call(
            client,
            report,
            "ak.wwise.core.transport.executeAction",
            {"transport": source_transport_id, "action": "play"},
        )
        time.sleep(args.playback_seconds)

        transport_state = call(
            client,
            report,
            "ak.wwise.core.transport.getState",
            {"transport": source_transport_id},
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
                "transportId": source_transport_id,
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

        call(
            client,
            report,
            "ak.wwise.core.transport.executeAction",
            {"transport": source_transport_id, "action": "stop"},
        )
        call(
            client,
            report,
            "ak.wwise.core.transport.destroy",
            {"transport": source_transport_id},
        )
        source_transport_id = None

        effect_transport_result = call(
            client,
            report,
            "ak.wwise.core.transport.create",
            {"object": effect_sound_id},
        )
        effect_transport_id = effect_transport_result.get("transport")
        if effect_transport_id is None:
            raise RuntimeError("transport.create did not return an Effect transport ID")
        call(
            client,
            report,
            "ak.wwise.core.transport.executeAction",
            {"transport": effect_transport_id, "action": "play"},
        )
        time.sleep(args.playback_seconds)

        effect_transport_state = call(
            client,
            report,
            "ak.wwise.core.transport.getState",
            {"transport": effect_transport_id},
        ).get("state")
        effect_voices = call(
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
        effect_cpu_usage = call(
            client,
            report,
            "ak.wwise.core.profiler.getCpuUsage",
            {"time": "capture"},
        ).get("return", [])
        effect_counters = call(
            client,
            report,
            "ak.wwise.core.profiler.getPerformanceMonitor",
            {"time": "capture"},
        ).get("return", [])
        effect_smoke_voices = [
            voice
            for voice in effect_voices
            if contains_effect_identity(
                voice, effect_sound_id, effect_audio_source_id, effect_id
            )
        ]
        effect_cpu_evidence = [
            item
            for item in effect_cpu_usage
            if is_effect_cpu_row(item, args.effect_class_id)
        ]
        effect_voices_total = counter_value(effect_counters, "VoicesTotal")
        effect_output_peak = counter_value(effect_counters, "OutputPeak")
        effect_output_peak_finite = isinstance(
            effect_output_peak, (int, float)
        ) and math.isfinite(effect_output_peak)
        effect_output_peak_audible = (
            effect_output_peak_finite
            and effect_output_peak > args.silence_floor_db
        )
        report["profiler"].update(
            {
                "effectTransportId": effect_transport_id,
                "effectTransportState": effect_transport_state,
                "effectVoices": effect_voices,
                "effectSmokeVoices": effect_smoke_voices,
                "effectCpuUsage": effect_cpu_usage,
                "effectCpuEvidence": effect_cpu_evidence,
                "effectPerformanceMonitor": effect_counters,
                "effectVoicesTotal": effect_voices_total,
                "effectOutputPeakDb": effect_output_peak,
                "effectSilenceFloorDb": args.silence_floor_db,
            }
        )

        source_assertions = {
            "transportPlaying": transport_state == "playing",
            "smokeVoiceOrSourceCpuEvidence": voice_or_cpu_evidence,
            "outputPeakFinite": output_peak_finite,
            "outputPeakAboveSilenceFloor": output_peak_audible,
            "authoringGuiInteractions": gui_interactions_passed,
            "soundBankParameterSerialization": soundbank_serialization_passed,
        }
        effect_assertions = {
            "effectObjectCreatedAndReadable": effect_object_passed,
            "effectSoundBankParameterSerialization": (
                effect_soundbank_serialization_passed
            ),
            "effectTransportPlaying": effect_transport_state == "playing",
            "effectAudioFileVoiceEvidence": bool(effect_smoke_voices),
            "effectCpuEvidence": bool(effect_cpu_evidence),
            "effectOutputPeakFinite": effect_output_peak_finite,
            "effectOutputPeakAboveSilenceFloor": effect_output_peak_audible,
        }
        report["assertionGroups"] = {
            "source": source_assertions,
            "effect": effect_assertions,
        }
        report["assertions"] = dict(source_assertions)
        report["assertions"].update(effect_assertions)
        core_assertions_passed = all(source_assertions.values()) and all(
            effect_assertions.values()
        )
        if not core_assertions_passed:
            failed = [name for name, passed in report["assertions"].items() if not passed]
            raise SmokeAssertionError("Smoke assertions failed: {0}".format(", ".join(failed)))
    finally:
        if client is not None:
            for transport_name, transport_id in (
                ("Source", source_transport_id),
                ("Effect", effect_transport_id),
            ):
                if transport_id is None:
                    continue
                try:
                    call(
                        client,
                        report,
                        "ak.wwise.core.transport.executeAction",
                        {"transport": transport_id, "action": "stop"},
                    )
                except Exception as error:  # cleanup must continue through every operation
                    mandatory_cleanup_errors.append(
                        "{0} transport.stop: {1}".format(transport_name, error)
                    )
                try:
                    call(
                        client,
                        report,
                        "ak.wwise.core.transport.destroy",
                        {"transport": transport_id},
                    )
                except Exception as error:
                    mandatory_cleanup_errors.append(
                        "{0} transport.destroy: {1}".format(transport_name, error)
                    )

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
        report.setdefault("assertionGroups", {})["shared"] = {
            "profilerCaptureSaved": capture_artifact_valid
        }
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
    parser.add_argument("--effect-class-id", type=int, default=DEFAULT_EFFECT_CLASS_ID)
    parser.add_argument(
        "--effect-input-wav",
        default=DEFAULT_EFFECT_INPUT_WAV,
        help="Absolute WAV path imported into the Effect smoke Sound",
    )
    parser.add_argument(
        "--native-host-fixture-dir",
        default=DEFAULT_NATIVE_HOST_FIXTURE_DIR,
        help="Repository directory that retains generated Effect banks, WEM, and metadata",
    )
    parser.add_argument("--retained-effect-sound-id")
    parser.add_argument("--retained-effect-audio-source-id")
    parser.add_argument("--retained-effect-id")
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
    if args.effect_class_id < 0 or args.effect_class_id > 0xFFFFFFFF:
        parser.error("--effect-class-id must be an unsigned 32-bit integer")
    retained_ids = (
        args.retained_effect_sound_id,
        args.retained_effect_audio_source_id,
        args.retained_effect_id,
    )
    if any(value is not None for value in retained_ids) and not all(
        value is not None for value in retained_ids
    ):
        parser.error(
            "--retained-effect-sound-id, --retained-effect-audio-source-id, and "
            "--retained-effect-id must be supplied together"
        )
    if not args.capture_file.lower().endswith(".prof"):
        parser.error("--capture-file must end in .prof")
    effect_input_wav = Path(args.effect_input_wav).resolve()
    if not effect_input_wav.is_file():
        parser.error(
            "--effect-input-wav must identify an existing file: '{0}'".format(
                effect_input_wav
            )
        )
    if effect_input_wav.suffix.casefold() != ".wav":
        parser.error("--effect-input-wav must end in .wav")
    args.effect_input_wav = str(effect_input_wav)
    args.native_host_fixture_dir = str(Path(args.native_host_fixture_dir).resolve())
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
            "effectClassId": args.effect_class_id,
            "effectInputWav": args.effect_input_wav,
            "retainedEffectSoundId": args.retained_effect_sound_id,
            "retainedEffectAudioSourceId": args.retained_effect_audio_source_id,
            "retainedEffectId": args.retained_effect_id,
            "nativeHostFixtureDir": args.native_host_fixture_dir,
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
        "assertionGroups": {"source": {}, "effect": {}, "shared": {}},
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
