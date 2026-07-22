/*******************************************************************************
The content of this file includes portions of the AUDIOKINETIC Wwise Technology
released in source code form as part of the SDK installer package.

Commercial License Usage

Licensees holding valid commercial licenses to the AUDIOKINETIC Wwise Technology
may use this file in accordance with the end user license agreement provided
with the software or, alternatively, in accordance with the terms contained in a
written agreement between you and Audiokinetic Inc.

Apache License Usage

Alternatively, this file may be used under the Apache License, Version 2.0 (the
"Apache License"); you may not use this file except in compliance with the
Apache License. You may obtain a copy of the Apache License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed
under the Apache License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
OR CONDITIONS OF ANY KIND, either express or implied. See the Apache License for
the specific language governing permissions and limitations under the License.

  Copyright (c) 2026 Audiokinetic Inc.
*******************************************************************************/

#include "RealWorldWeatherAcousticsPluginGUI.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <windowsx.h>

namespace
{
constexpr wchar_t kCanvasClassName[] = L"RealWorldWeatherAcousticsPreviewCanvas";
constexpr UINT kPropertyChangedMessage = WM_APP + 0x31;

constexpr UINT kCanvasId = 2000;
constexpr UINT kGlobalTitleId = 2001;
constexpr UINT kSelectedFeatureTitleId = 2002;
constexpr UINT kGlobalEditBaseId = 2100;
constexpr UINT kFeatureEditBaseId = 2200;
constexpr UINT kGlobalLabelBaseId = 2300;
constexpr UINT kFeatureLabelBaseId = 2400;
constexpr UINT kPresetButtonBaseId = 2500;
constexpr UINT kAddFeatureButtonId = 2600;
constexpr UINT kDeleteFeatureButtonId = 2601;

// Let Wwise own boolean property synchronization and its native undo behavior.
// The table is intentionally narrow: the remaining controls use custom parsing,
// range normalization, and grouped drag edits.
AK_WWISE_PLUGIN_GUI_WINDOWS_BEGIN_POPULATE_TABLE(kWwisePropertyBindings)
	AK_WWISE_PLUGIN_GUI_WINDOWS_POP_ITEM(kGlobalEditBaseId + 4, "GeometryEnabled")
AK_WWISE_PLUGIN_GUI_WINDOWS_END_POPULATE_TABLE()

constexpr int kMaximumFeatures = 8;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinimumFeatureRadius = 0.2f;
constexpr float kNewFeatureDistance = 4.0f;

enum class PropertyKind
{
	Real32,
	Int32,
	Bool,
};

struct PropertyBinding
{
	const wchar_t* label;
	const char* property;
	PropertyKind kind;
};

struct FeatureBinding
{
	const wchar_t* label;
	const char* suffix;
	PropertyKind kind;
};

constexpr std::array<PropertyBinding, 13> kGlobalBindings{{
	{L"Duration", "Duration", PropertyKind::Real32},
	{L"Master gain (dB)", "MasterGainDb", PropertyKind::Real32},
	{L"Rain intensity", "RainIntensity", PropertyKind::Real32},
	{L"Seed", "Seed", PropertyKind::Int32},
	{L"Geometry enabled", "GeometryEnabled", PropertyKind::Bool},
	{L"Listener X", "ListenerX", PropertyKind::Real32},
	{L"Listener Y", "ListenerY", PropertyKind::Real32},
	{L"Listener Z", "ListenerZ", PropertyKind::Real32},
	{L"Listener yaw", "ListenerYawDegrees", PropertyKind::Real32},
	{L"Features", "FeatureCount", PropertyKind::Int32},
	{L"Wind speed (m/s)", "WindSpeed", PropertyKind::Real32},
	{L"Wind direction", "WindDirectionDegrees", PropertyKind::Real32},
	{L"Wind gustiness", "WindGustiness", PropertyKind::Real32},
}};

constexpr std::array<FeatureBinding, 7> kFeatureBindings{{
	{L"X", "X", PropertyKind::Real32},
	{L"Y", "Y", PropertyKind::Real32},
	{L"Z", "Z", PropertyKind::Real32},
	{L"Radius", "Radius", PropertyKind::Real32},
	{L"Profile", "Profile", PropertyKind::Int32},
	{L"Mask", "Mask", PropertyKind::Int32},
	{L"Priority", "Priority", PropertyKind::Int32},
}};

constexpr std::array<COLORREF, kMaximumFeatures> kFeatureColors{{
	RGB(51, 153, 255),
	RGB(95, 190, 95),
	RGB(238, 155, 57),
	RGB(184, 112, 214),
	RGB(67, 190, 180),
	RGB(222, 102, 124),
	RGB(173, 170, 74),
	RGB(115, 145, 205),
}};

constexpr std::array<const wchar_t*, 3> kPresetNames{{
	L"Open Wind",
	L"Rain on Metal",
	L"Wind + Rain Ring",
}};

int ClampFeatureCount(int in_count)
{
	return std::clamp(in_count, 0, kMaximumFeatures);
}

void BuildFeaturePropertyName(int in_featureIndex, const char* in_suffix, char* out_name, size_t in_capacity)
{
	std::snprintf(out_name, in_capacity, "Feature%d%s", in_featureIndex + 1, in_suffix);
}

bool TryParseReal32(HWND in_control, float& out_value)
{
	wchar_t text[96]{};
	::GetWindowTextW(in_control, text, static_cast<int>(std::size(text)));

	wchar_t* end = nullptr;
	const double value = std::wcstod(text, &end);
	while (end && std::iswspace(*end))
		++end;

	if (end == text || (end && *end != L'\0') || !std::isfinite(value))
		return false;

	out_value = static_cast<float>(value);
	return std::isfinite(out_value);
}

bool TryParseInt32(HWND in_control, int32_t& out_value)
{
	wchar_t text[96]{};
	::GetWindowTextW(in_control, text, static_cast<int>(std::size(text)));

	wchar_t* end = nullptr;
	const long value = std::wcstol(text, &end, 10);
	while (end && std::iswspace(*end))
		++end;

	if (end == text || (end && *end != L'\0'))
		return false;

	out_value = static_cast<int32_t>(value);
	return static_cast<long>(out_value) == value;
}

float NormalizeYaw(float in_yawDegrees)
{
	float yaw = std::fmod(in_yawDegrees, 360.0f);
	if (yaw > 180.0f)
		yaw -= 360.0f;
	else if (yaw <= -180.0f)
		yaw += 360.0f;
	return yaw;
}

float NormalizeDirection(float in_directionDegrees)
{
	float direction = std::fmod(in_directionDegrees, 360.0f);
	if (direction < 0.0f)
		direction += 360.0f;
	return direction;
}

void SelectGdiObject(HDC in_dc, HGDIOBJ in_object, HGDIOBJ& out_previous)
{
	out_previous = ::SelectObject(in_dc, in_object);
}
}

RealWorldWeatherAcousticsPluginGUI::RealWorldWeatherAcousticsPluginGUI()
{
	WNDCLASSEXW canvasClass{};
	if (::GetClassInfoExW(GetResourceHandle(), kCanvasClassName, &canvasClass))
		return;

	canvasClass.cbSize = sizeof(canvasClass);
	canvasClass.style = CS_HREDRAW | CS_VREDRAW;
	canvasClass.lpfnWndProc = &RealWorldWeatherAcousticsPluginGUI::CanvasWindowProc;
	canvasClass.hInstance = GetResourceHandle();
	canvasClass.hCursor = ::LoadCursorW(nullptr, IDC_CROSS);
	canvasClass.hbrBackground = nullptr;
	canvasClass.lpszClassName = kCanvasClassName;
	::RegisterClassExW(&canvasClass);
}

void RealWorldWeatherAcousticsPluginGUI::NotifyPropertyChanged(
	const GUID& in_guidPlatform,
	const char* in_szPropertyName)
{
	(void)in_guidPlatform;
	(void)in_szPropertyName;

	if (m_hwndDialog)
		::PostMessageW(m_hwndDialog, kPropertyChangedMessage, 0, 0);
}

bool RealWorldWeatherAcousticsPluginGUI::GetDialog(
	AK::Wwise::Plugin::eDialog in_eDialog,
	UINT& out_uiDialogID,
	AK::Wwise::Plugin::PopulateTableItem*& out_pTable) const
{
	if (in_eDialog != AK::Wwise::Plugin::SettingsDialog)
		return false;

	// GUIWindows explicitly supports returning an empty host dialog. All controls are
	// created in WM_INITDIALOG so the generated project does not need a resource file.
	out_uiDialogID = 0;
	out_pTable = kWwisePropertyBindings;
	return true;
}

bool RealWorldWeatherAcousticsPluginGUI::WindowProc(
	AK::Wwise::Plugin::eDialog in_eDialog,
	HWND in_hWnd,
	UINT in_message,
	WPARAM in_wParam,
	LPARAM in_lParam,
	LRESULT& out_lResult)
{
	if (in_eDialog != AK::Wwise::Plugin::SettingsDialog)
		return false;

	switch (in_message)
	{
	case WM_INITDIALOG:
		CreateControls(in_hWnd);
		UpdateControls();
		LayoutControls();
		break;

	case WM_SIZE:
		LayoutControls();
		break;

	case WM_COMMAND:
		if (!m_updatingControls)
		{
			const UINT controlId = LOWORD(in_wParam);
			const UINT notification = HIWORD(in_wParam);
			if (notification == BN_CLICKED &&
				controlId >= kPresetButtonBaseId &&
				controlId < kPresetButtonBaseId + kPresetNames.size())
			{
				ApplyPreset(static_cast<int>(controlId - kPresetButtonBaseId));
			}
			else if (notification == BN_CLICKED && controlId == kAddFeatureButtonId)
			{
				AddFeature();
			}
			else if (notification == BN_CLICKED && controlId == kDeleteFeatureButtonId)
			{
				DeleteSelectedFeature();
			}
			else if (notification == EN_KILLFOCUS || notification == BN_CLICKED)
				CommitControl(controlId);
		}
		break;

	case WM_ENABLE:
		for (HWND child = ::GetWindow(in_hWnd, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
			::EnableWindow(child, static_cast<BOOL>(in_wParam));
		if (in_wParam)
			UpdateControls();
		break;

	case kPropertyChangedMessage:
		UpdateControls();
		InvalidatePreview();
		break;

	case WM_DESTROY:
		EndDrag();
		m_hwndCanvas = nullptr;
		m_hwndDialog = nullptr;
		break;

	default:
		break;
	}

	out_lResult = 0;
	return false;
}

bool RealWorldWeatherAcousticsPluginGUI::Help(
	HWND in_hWnd,
	AK::Wwise::Plugin::eDialog in_eDialog,
	const char* in_szLanguageCode) const
{
	(void)in_hWnd;
	(void)in_eDialog;
	(void)in_szLanguageCode;
	return false;
}

void RealWorldWeatherAcousticsPluginGUI::CreateControls(HWND in_hWnd)
{
	m_hwndDialog = in_hWnd;
	m_hGuiFont = reinterpret_cast<HFONT>(::SendMessageW(in_hWnd, WM_GETFONT, 0, 0));
	if (!m_hGuiFont)
		m_hGuiFont = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));

	auto setFont = [this](HWND in_control)
	{
		if (in_control && m_hGuiFont)
			::SendMessageW(in_control, WM_SETFONT, reinterpret_cast<WPARAM>(m_hGuiFont), TRUE);
	};

	m_hwndCanvas = ::CreateWindowExW(
		WS_EX_CLIENTEDGE,
		kCanvasClassName,
		L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
		0,
		0,
		100,
		100,
		in_hWnd,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCanvasId)),
		GetResourceHandle(),
		this);
	setFont(m_hwndCanvas);

	setFont(::CreateWindowExW(
		0, L"STATIC", L"Runtime and listener", WS_CHILD | WS_VISIBLE | SS_LEFT,
		0, 0, 10, 10, in_hWnd,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGlobalTitleId)),
		GetResourceHandle(), nullptr));

	setFont(::CreateWindowExW(
		0, L"STATIC", L"Selected feature", WS_CHILD | WS_VISIBLE | SS_LEFT,
		0, 0, 10, 10, in_hWnd,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSelectedFeatureTitleId)),
		GetResourceHandle(), nullptr));

	for (size_t index = 0; index < kGlobalBindings.size(); ++index)
	{
		const UINT controlId = kGlobalEditBaseId + static_cast<UINT>(index);
		const UINT labelId = kGlobalLabelBaseId + static_cast<UINT>(index);
		const PropertyBinding& binding = kGlobalBindings[index];

		if (binding.kind == PropertyKind::Bool)
		{
			setFont(::CreateWindowExW(
				0, L"BUTTON", binding.label,
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
				0, 0, 10, 10, in_hWnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
				GetResourceHandle(), nullptr));
			continue;
		}

		setFont(::CreateWindowExW(
			0, L"STATIC", binding.label, WS_CHILD | WS_VISIBLE | SS_RIGHT,
			0, 0, 10, 10, in_hWnd,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(labelId)),
			GetResourceHandle(), nullptr));
		DWORD editStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
		if (std::strcmp(binding.property, "FeatureCount") == 0)
			editStyle |= ES_READONLY;
		else
			editStyle |= WS_TABSTOP;
		setFont(::CreateWindowExW(
			WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
			0, 0, 10, 10, in_hWnd,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
			GetResourceHandle(), nullptr));
	}

	for (size_t index = 0; index < kFeatureBindings.size(); ++index)
	{
		const UINT controlId = kFeatureEditBaseId + static_cast<UINT>(index);
		const UINT labelId = kFeatureLabelBaseId + static_cast<UINT>(index);

		setFont(::CreateWindowExW(
			0, L"STATIC", kFeatureBindings[index].label, WS_CHILD | WS_VISIBLE | SS_RIGHT,
			0, 0, 10, 10, in_hWnd,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(labelId)),
			GetResourceHandle(), nullptr));
		setFont(::CreateWindowExW(
			WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			0, 0, 10, 10, in_hWnd,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
			GetResourceHandle(), nullptr));
	}

	for (size_t index = 0; index < kPresetNames.size(); ++index)
	{
		setFont(::CreateWindowExW(
			0, L"BUTTON", kPresetNames[index], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			0, 0, 10, 10, in_hWnd,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPresetButtonBaseId + index)),
			GetResourceHandle(), nullptr));
	}

	setFont(::CreateWindowExW(
		0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 10, 10, in_hWnd,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddFeatureButtonId)),
		GetResourceHandle(), nullptr));
	setFont(::CreateWindowExW(
		0, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 10, 10, in_hWnd,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDeleteFeatureButtonId)),
		GetResourceHandle(), nullptr));
}

void RealWorldWeatherAcousticsPluginGUI::LayoutControls()
{
	if (!m_hwndDialog || !m_hwndCanvas)
		return;

	RECT client{};
	::GetClientRect(m_hwndDialog, &client);
	const int width = std::max(1L, client.right - client.left);
	const int height = std::max(1L, client.bottom - client.top);

	constexpr int margin = 8;
	constexpr int gap = 10;
	constexpr int minimumCanvasWidth = 160;
	constexpr int preferredMinimumInspectorWidth = 400;
	const int maximumInspectorWidth = std::max(1, width - margin * 2 - gap - minimumCanvasWidth);
	const int minimumInspectorWidth = std::min(preferredMinimumInspectorWidth, maximumInspectorWidth);
	const int inspectorWidth = std::clamp(width * 46 / 100, minimumInspectorWidth, maximumInspectorWidth);
	const int canvasWidth = std::max(minimumCanvasWidth, width - inspectorWidth - gap - margin * 2);
	const int inspectorX = margin + canvasWidth + gap;

	constexpr int presetButtonHeight = 25;
	constexpr int presetButtonGap = 5;
	const int canvasHeight = std::max(120, height - margin * 3 - presetButtonHeight);
	::MoveWindow(m_hwndCanvas, margin, margin, canvasWidth, canvasHeight, TRUE);
	const int presetButtonWidth = std::max(72, (canvasWidth - presetButtonGap * 2) / 3);
	for (size_t index = 0; index < kPresetNames.size(); ++index)
	{
		::MoveWindow(::GetDlgItem(m_hwndDialog, kPresetButtonBaseId + static_cast<UINT>(index)),
			margin + static_cast<int>(index) * (presetButtonWidth + presetButtonGap),
			margin * 2 + canvasHeight,
			presetButtonWidth,
			presetButtonHeight,
			TRUE);
	}

	constexpr int inspectorColumnGap = 12;
	const int globalColumnWidth = std::max(1, (inspectorWidth - inspectorColumnGap) / 2);
	const int featureColumnWidth = std::max(1, inspectorWidth - inspectorColumnGap - globalColumnWidth);
	const int featureColumnX = inspectorX + globalColumnWidth + inspectorColumnGap;
	constexpr int titleHeight = 20;
	const int globalRowHeight = std::clamp(
		(height - margin * 2 - titleHeight) / static_cast<int>(kGlobalBindings.size()),
		18,
		23);
	const int featureRowHeight = std::clamp(
		(height - margin * 2 - titleHeight) / static_cast<int>(kFeatureBindings.size()),
		18,
		23);
	const int globalLabelWidth = std::clamp(
		globalColumnWidth * 52 / 100, 52, std::max(52, globalColumnWidth - 44));
	const int globalEditWidth = std::max(40, globalColumnWidth - globalLabelWidth - 4);
	const int featureLabelWidth = std::clamp(
		featureColumnWidth * 38 / 100, 44, std::max(44, featureColumnWidth - 44));
	const int featureEditWidth = std::max(40, featureColumnWidth - featureLabelWidth - 4);
	int globalY = margin;

	::MoveWindow(::GetDlgItem(m_hwndDialog, kGlobalTitleId),
		inspectorX, globalY, globalColumnWidth, titleHeight, TRUE);
	globalY += titleHeight;

	for (size_t index = 0; index < kGlobalBindings.size(); ++index)
	{
		const UINT controlId = kGlobalEditBaseId + static_cast<UINT>(index);
		if (kGlobalBindings[index].kind == PropertyKind::Bool)
		{
			::MoveWindow(::GetDlgItem(m_hwndDialog, controlId),
				inspectorX + 2, globalY, globalColumnWidth - 2, globalRowHeight, TRUE);
		}
		else
		{
			::MoveWindow(::GetDlgItem(m_hwndDialog, kGlobalLabelBaseId + static_cast<UINT>(index)),
				inspectorX, globalY + 2, globalLabelWidth - 4, globalRowHeight - 2, TRUE);
			::MoveWindow(::GetDlgItem(m_hwndDialog, controlId),
				inspectorX + globalLabelWidth, globalY, globalEditWidth, globalRowHeight - 1, TRUE);
		}
		globalY += globalRowHeight;
	}

	constexpr int featureButtonGap = 3;
	constexpr int minimumFeatureTitleWidth = 35;
	const int featureButtonWidth = std::clamp(
		(featureColumnWidth - minimumFeatureTitleWidth - featureButtonGap * 2) / 2,
		28,
		58);
	const int featureTitleWidth = std::max(
		minimumFeatureTitleWidth,
		featureColumnWidth - featureButtonWidth * 2 - featureButtonGap * 2);
	int featureY = margin;
	::MoveWindow(::GetDlgItem(m_hwndDialog, kSelectedFeatureTitleId),
		featureColumnX, featureY, featureTitleWidth, titleHeight, TRUE);
	::MoveWindow(::GetDlgItem(m_hwndDialog, kAddFeatureButtonId),
		featureColumnX + featureTitleWidth + featureButtonGap,
		featureY, featureButtonWidth, titleHeight, TRUE);
	::MoveWindow(::GetDlgItem(m_hwndDialog, kDeleteFeatureButtonId),
		featureColumnX + featureTitleWidth + featureButtonGap * 2 + featureButtonWidth,
		featureY, featureButtonWidth, titleHeight, TRUE);
	featureY += titleHeight;

	for (size_t index = 0; index < kFeatureBindings.size(); ++index)
	{
		::MoveWindow(::GetDlgItem(m_hwndDialog, kFeatureLabelBaseId + static_cast<UINT>(index)),
			featureColumnX, featureY + 2, featureLabelWidth - 4, featureRowHeight - 2, TRUE);
		::MoveWindow(::GetDlgItem(m_hwndDialog, kFeatureEditBaseId + static_cast<UINT>(index)),
			featureColumnX + featureLabelWidth, featureY, featureEditWidth, featureRowHeight - 1, TRUE);
		featureY += featureRowHeight;
	}
}

void RealWorldWeatherAcousticsPluginGUI::UpdateControls()
{
	if (!m_hwndDialog)
		return;

	m_updatingControls = true;
	const GUID platform = m_host.GetCurrentPlatform();
	const HWND focusedControl = ::GetFocus();
	wchar_t text[96]{};

	for (size_t index = 0; index < kGlobalBindings.size(); ++index)
	{
		const PropertyBinding& binding = kGlobalBindings[index];
		HWND control = ::GetDlgItem(m_hwndDialog, kGlobalEditBaseId + static_cast<UINT>(index));
		if (!control)
			continue;

		if (binding.kind == PropertyKind::Bool)
		{
			// GeometryEnabled is synchronized by Wwise through the populate table.
			// Do not overwrite the host-owned checkbox from a manual PropertySet read.
			continue;
		}
		else if (control != focusedControl)
		{
			if (std::strcmp(binding.property, "FeatureCount") == 0)
				std::swprintf(text, std::size(text), L"%d / %d",
					ClampFeatureCount(m_propertySet.GetInt32(platform, binding.property)), kMaximumFeatures);
			else if (binding.kind == PropertyKind::Int32)
				std::swprintf(text, std::size(text), L"%d", m_propertySet.GetInt32(platform, binding.property));
			else
				std::swprintf(text, std::size(text), L"%.6g", m_propertySet.GetReal32(platform, binding.property));
			::SetWindowTextW(control, text);
		}
	}

	const int featureCount = ClampFeatureCount(m_propertySet.GetInt32(platform, "FeatureCount"));
	if (featureCount > 0)
		m_selectedFeature = std::clamp(m_selectedFeature, 0, featureCount - 1);
	else
		m_selectedFeature = 0;

	if (featureCount > 0)
		std::swprintf(text, std::size(text), L"Feature %d / %d", m_selectedFeature + 1, featureCount);
	else
		std::swprintf(text, std::size(text), L"No feature");
	::SetWindowTextW(::GetDlgItem(m_hwndDialog, kSelectedFeatureTitleId), text);
	const bool dialogEnabled = ::IsWindowEnabled(m_hwndDialog) != FALSE;
	::EnableWindow(::GetDlgItem(m_hwndDialog, kAddFeatureButtonId), dialogEnabled && featureCount < kMaximumFeatures);
	::EnableWindow(::GetDlgItem(m_hwndDialog, kDeleteFeatureButtonId), dialogEnabled && featureCount > 0);

	for (size_t index = 0; index < kFeatureBindings.size(); ++index)
	{
		HWND control = ::GetDlgItem(m_hwndDialog, kFeatureEditBaseId + static_cast<UINT>(index));
		::EnableWindow(control, dialogEnabled && featureCount > 0);
		::EnableWindow(::GetDlgItem(m_hwndDialog, kFeatureLabelBaseId + static_cast<UINT>(index)),
			dialogEnabled && featureCount > 0);
		if (featureCount <= 0 || control == focusedControl)
			continue;

		char propertyName[48]{};
		BuildFeaturePropertyName(m_selectedFeature, kFeatureBindings[index].suffix, propertyName, std::size(propertyName));
		if (kFeatureBindings[index].kind == PropertyKind::Int32)
			std::swprintf(text, std::size(text), L"%d", m_propertySet.GetInt32(platform, propertyName));
		else
			std::swprintf(text, std::size(text), L"%.6g", m_propertySet.GetReal32(platform, propertyName));
		::SetWindowTextW(control, text);
	}

	m_updatingControls = false;
}

void RealWorldWeatherAcousticsPluginGUI::CommitControl(UINT in_controlId)
{
	if (!m_hwndDialog)
		return;

	const GUID platform = m_host.GetCurrentPlatform();
	const PropertyBinding* globalBinding = nullptr;
	const FeatureBinding* featureBinding = nullptr;
	char featurePropertyName[48]{};

	if (in_controlId >= kGlobalEditBaseId && in_controlId < kGlobalEditBaseId + kGlobalBindings.size())
	{
		globalBinding = &kGlobalBindings[in_controlId - kGlobalEditBaseId];
	}
	else if (in_controlId >= kFeatureEditBaseId && in_controlId < kFeatureEditBaseId + kFeatureBindings.size())
	{
		if (ClampFeatureCount(m_propertySet.GetInt32(platform, "FeatureCount")) <= 0)
			return;

		featureBinding = &kFeatureBindings[in_controlId - kFeatureEditBaseId];
		BuildFeaturePropertyName(m_selectedFeature, featureBinding->suffix, featurePropertyName, std::size(featurePropertyName));
	}
	else
	{
		return;
	}

	HWND control = ::GetDlgItem(m_hwndDialog, in_controlId);
	const PropertyKind kind = globalBinding ? globalBinding->kind : featureBinding->kind;
	const char* property = globalBinding ? globalBinding->property : featurePropertyName;
	if (std::strcmp(property, "FeatureCount") == 0)
	{
		UpdateControls();
		return;
	}
	if (kind == PropertyKind::Bool)
	{
		// Wwise's populate table commits this checkbox and creates the native undo
		// action. NotifyPropertyChanged will repaint the preview afterwards.
		return;
	}
	AK::Wwise::Plugin::AutoUndoGroup undoGroup(m_undoManager, "Edit weather acoustics preview");
	bool changed = false;

	if (kind == PropertyKind::Int32)
	{
		int32_t value = 0;
		if (TryParseInt32(control, value))
		{
			changed = m_propertySet.SetValueInt32(platform, property, value);
		}
	}
	else
	{
		float value = 0.0f;
		if (TryParseReal32(control, value))
		{
			if (std::strcmp(property, "ListenerYawDegrees") == 0)
				value = NormalizeYaw(value);
			else if (std::strcmp(property, "WindDirectionDegrees") == 0)
				value = NormalizeDirection(value);
			else if (std::strcmp(property, "WindSpeed") == 0)
				value = std::clamp(value, 0.0f, 40.0f);
			else if (std::strcmp(property, "WindGustiness") == 0)
				value = std::clamp(value, 0.0f, 1.0f);
			else if (featureBinding && std::strcmp(featureBinding->suffix, "Radius") == 0)
				value = std::max(kMinimumFeatureRadius, value);
			changed = m_propertySet.SetValueReal32(platform, property, value);
		}
	}

	(void)changed;
	UpdateControls();
	InvalidatePreview();
}

void RealWorldWeatherAcousticsPluginGUI::SelectFeature(int in_featureIndex)
{
	const int featureCount = ClampFeatureCount(m_propertySet.GetInt32(m_host.GetCurrentPlatform(), "FeatureCount"));
	if (featureCount <= 0)
		return;

	m_selectedFeature = std::clamp(in_featureIndex, 0, featureCount - 1);
	UpdateControls();
	InvalidatePreview();
}

void RealWorldWeatherAcousticsPluginGUI::AddFeature()
{
	const GUID platform = m_host.GetCurrentPlatform();
	const int featureCount = ClampFeatureCount(m_propertySet.GetInt32(platform, "FeatureCount"));
	if (featureCount >= kMaximumFeatures)
	{
		UpdateControls();
		return;
	}

	AK::Wwise::Plugin::AutoUndoGroup undoGroup(m_undoManager, "Add RWWA Feature");
	const float listenerX = m_propertySet.GetReal32(platform, "ListenerX");
	const float listenerZ = m_propertySet.GetReal32(platform, "ListenerZ");
	const float yaw = m_propertySet.GetReal32(platform, "ListenerYawDegrees") * kPi / 180.0f;
	const float featureX = listenerX + std::sin(yaw) * kNewFeatureDistance;
	const float featureZ = listenerZ + std::cos(yaw) * kNewFeatureDistance;
	char propertyName[48]{};

	BuildFeaturePropertyName(featureCount, "X", propertyName, std::size(propertyName));
	m_propertySet.SetValueReal32(platform, propertyName, featureX);
	BuildFeaturePropertyName(featureCount, "Y", propertyName, std::size(propertyName));
	m_propertySet.SetValueReal32(platform, propertyName, 0.0f);
	BuildFeaturePropertyName(featureCount, "Z", propertyName, std::size(propertyName));
	m_propertySet.SetValueReal32(platform, propertyName, featureZ);
	BuildFeaturePropertyName(featureCount, "Radius", propertyName, std::size(propertyName));
	m_propertySet.SetValueReal32(platform, propertyName, 2.0f);
	BuildFeaturePropertyName(featureCount, "Profile", propertyName, std::size(propertyName));
	m_propertySet.SetValueInt32(platform, propertyName, featureCount % 4);
	BuildFeaturePropertyName(featureCount, "Mask", propertyName, std::size(propertyName));
	m_propertySet.SetValueInt32(platform, propertyName, 3);
	BuildFeaturePropertyName(featureCount, "Priority", propertyName, std::size(propertyName));
	m_propertySet.SetValueInt32(platform, propertyName, 1);
	m_propertySet.SetValueBool(platform, "GeometryEnabled", true);
	m_propertySet.SetValueInt32(platform, "FeatureCount", featureCount + 1);
	m_selectedFeature = featureCount;

	UpdateControls();
	InvalidatePreview();
}

void RealWorldWeatherAcousticsPluginGUI::DeleteSelectedFeature()
{
	const GUID platform = m_host.GetCurrentPlatform();
	const int featureCount = ClampFeatureCount(m_propertySet.GetInt32(platform, "FeatureCount"));
	if (featureCount <= 0)
	{
		UpdateControls();
		return;
	}

	const int deletedFeature = std::clamp(m_selectedFeature, 0, featureCount - 1);
	AK::Wwise::Plugin::AutoUndoGroup undoGroup(m_undoManager, "Delete RWWA Feature");
	for (int destinationIndex = deletedFeature; destinationIndex < featureCount - 1; ++destinationIndex)
	{
		for (const FeatureBinding& binding : kFeatureBindings)
		{
			char sourceProperty[48]{};
			char destinationProperty[48]{};
			BuildFeaturePropertyName(destinationIndex + 1, binding.suffix, sourceProperty, std::size(sourceProperty));
			BuildFeaturePropertyName(destinationIndex, binding.suffix, destinationProperty, std::size(destinationProperty));
			if (binding.kind == PropertyKind::Int32)
			{
				m_propertySet.SetValueInt32(
					platform, destinationProperty, m_propertySet.GetInt32(platform, sourceProperty));
			}
			else
			{
				m_propertySet.SetValueReal32(
					platform, destinationProperty, m_propertySet.GetReal32(platform, sourceProperty));
			}
		}
	}

	const int tailIndex = featureCount - 1;
	for (const FeatureBinding& binding : kFeatureBindings)
	{
		char tailProperty[48]{};
		BuildFeaturePropertyName(tailIndex, binding.suffix, tailProperty, std::size(tailProperty));
		if (binding.kind == PropertyKind::Int32)
			m_propertySet.SetValueInt32(platform, tailProperty, 0);
		else
			m_propertySet.SetValueReal32(platform, tailProperty,
				std::strcmp(binding.suffix, "Radius") == 0 ? 0.01f : 0.0f);
	}

	m_propertySet.SetValueInt32(platform, "FeatureCount", featureCount - 1);
	m_selectedFeature = featureCount > 1 ? std::min(deletedFeature, featureCount - 2) : 0;
	UpdateControls();
	InvalidatePreview();
}

void RealWorldWeatherAcousticsPluginGUI::ApplyPreset(int in_presetIndex)
{
	if (in_presetIndex < 0 || in_presetIndex >= static_cast<int>(kPresetNames.size()))
		return;

	const GUID platform = m_host.GetCurrentPlatform();
	AK::Wwise::Plugin::AutoUndoGroup undoGroup(m_undoManager, "Apply weather acoustics preset");

	// Presets write the same production properties used by the DSP and bank writer.
	// The common deterministic weather state makes A/B comparisons repeatable.
	m_propertySet.SetValueReal32(platform, "Duration", 60.0f);
	m_propertySet.SetValueReal32(platform, "MasterGainDb", -12.0f);
	m_propertySet.SetValueInt32(platform, "Seed", 1337);
	m_propertySet.SetValueReal32(platform, "ListenerX", 0.0f);
	m_propertySet.SetValueReal32(platform, "ListenerY", 0.0f);
	m_propertySet.SetValueReal32(platform, "ListenerZ", 0.0f);
	m_propertySet.SetValueReal32(platform, "ListenerYawDegrees", 0.0f);

	const bool openWind = in_presetIndex == 0;
	const bool metalRain = in_presetIndex == 1;
	const int featureCount = openWind ? 0 : (metalRain ? 1 : 4);
	constexpr std::array<float, 4> ringX{{0.0f, 6.0f, 0.0f, -6.0f}};
	constexpr std::array<float, 4> ringZ{{6.0f, 0.0f, -6.0f, 0.0f}};
	m_propertySet.SetValueReal32(platform, "RainIntensity", openWind ? 0.0f : (metalRain ? 0.8f : 0.75f));
	m_propertySet.SetValueReal32(platform, "WindSpeed", openWind ? 10.0f : (metalRain ? 0.0f : 14.0f));
	m_propertySet.SetValueReal32(platform, "WindDirectionDegrees", 45.0f);
	m_propertySet.SetValueReal32(platform, "WindGustiness", openWind ? 0.45f : (metalRain ? 0.1f : 0.65f));
	m_propertySet.SetValueBool(platform, "GeometryEnabled", !openWind);

	for (int featureIndex = 0; featureIndex < kMaximumFeatures; ++featureIndex)
	{
		const bool active = featureIndex < featureCount;
		const float featureX = metalRain && active ? 0.0f :
			(active ? ringX[static_cast<size_t>(featureIndex)] : 0.0f);
		const float featureZ = metalRain && active ? 4.0f :
			(active ? ringZ[static_cast<size_t>(featureIndex)] : 0.0f);
		const float radius = active ? (metalRain ? 3.0f : 2.0f) : 0.01f;
		char propertyName[48]{};
		BuildFeaturePropertyName(featureIndex, "X", propertyName, std::size(propertyName));
		m_propertySet.SetValueReal32(platform, propertyName, featureX);
		BuildFeaturePropertyName(featureIndex, "Y", propertyName, std::size(propertyName));
		m_propertySet.SetValueReal32(platform, propertyName, 0.0f);
		BuildFeaturePropertyName(featureIndex, "Z", propertyName, std::size(propertyName));
		m_propertySet.SetValueReal32(platform, propertyName, featureZ);
		BuildFeaturePropertyName(featureIndex, "Radius", propertyName, std::size(propertyName));
		m_propertySet.SetValueReal32(platform, propertyName, radius);
		BuildFeaturePropertyName(featureIndex, "Profile", propertyName, std::size(propertyName));
		m_propertySet.SetValueInt32(platform, propertyName, active ? featureIndex % 4 : 0);
		BuildFeaturePropertyName(featureIndex, "Mask", propertyName, std::size(propertyName));
		m_propertySet.SetValueInt32(platform, propertyName, active ? (metalRain ? 1 : 3) : 0);
		BuildFeaturePropertyName(featureIndex, "Priority", propertyName, std::size(propertyName));
		m_propertySet.SetValueInt32(platform, propertyName, active ? 1 : 0);
	}
	m_propertySet.SetValueInt32(platform, "FeatureCount", featureCount);
	m_selectedFeature = 0;

	UpdateControls();
	InvalidatePreview();
}

void RealWorldWeatherAcousticsPluginGUI::PaintCanvas(HWND in_hWnd)
{
	PAINTSTRUCT paint{};
	HDC windowDc = ::BeginPaint(in_hWnd, &paint);
	RECT client{};
	::GetClientRect(in_hWnd, &client);
	const int width = std::max(1L, client.right);
	const int height = std::max(1L, client.bottom);

	HDC dc = ::CreateCompatibleDC(windowDc);
	HBITMAP bitmap = ::CreateCompatibleBitmap(windowDc, width, height);
	HGDIOBJ previousBitmap = nullptr;
	SelectGdiObject(dc, bitmap, previousBitmap);

	HBRUSH background = ::CreateSolidBrush(RGB(37, 39, 43));
	::FillRect(dc, &client, background);
	::DeleteObject(background);

	const CanvasTransform transform = m_dragMode == DragMode::None
		? CalculateCanvasTransform(client)
		: m_dragTransform;

	HBRUSH plotBrush = ::CreateSolidBrush(RGB(25, 27, 30));
	::FillRect(dc, &transform.plotRect, plotBrush);
	::DeleteObject(plotBrush);
	::SetBkMode(dc, TRANSPARENT);
	::SetTextColor(dc, RGB(220, 222, 225));
	if (m_hGuiFont)
		::SelectObject(dc, m_hGuiFont);

	float leftX = 0.0f;
	float topZ = 0.0f;
	float rightX = 0.0f;
	float bottomZ = 0.0f;
	ScreenToWorld(transform, POINT{transform.plotRect.left, transform.plotRect.top}, leftX, topZ);
	ScreenToWorld(transform, POINT{transform.plotRect.right, transform.plotRect.bottom}, rightX, bottomZ);
	const float gridStep = transform.pixelsPerMeter >= 18.0f ? 1.0f :
		(transform.pixelsPerMeter >= 8.0f ? 2.0f : 5.0f);

	HPEN gridPen = ::CreatePen(PS_SOLID, 1, RGB(51, 54, 59));
	HGDIOBJ previousPen = nullptr;
	SelectGdiObject(dc, gridPen, previousPen);
	for (float x = std::floor(leftX / gridStep) * gridStep; x <= rightX; x += gridStep)
	{
		const POINT point = WorldToScreen(transform, x, 0.0f);
		::MoveToEx(dc, point.x, transform.plotRect.top, nullptr);
		::LineTo(dc, point.x, transform.plotRect.bottom);
	}
	for (float z = std::floor(bottomZ / gridStep) * gridStep; z <= topZ; z += gridStep)
	{
		const POINT point = WorldToScreen(transform, 0.0f, z);
		::MoveToEx(dc, transform.plotRect.left, point.y, nullptr);
		::LineTo(dc, transform.plotRect.right, point.y);
	}
	::DeleteObject(::SelectObject(dc, previousPen));

	HPEN axisPen = ::CreatePen(PS_SOLID, 2, RGB(92, 96, 103));
	SelectGdiObject(dc, axisPen, previousPen);
	const POINT origin = WorldToScreen(transform, 0.0f, 0.0f);
	::MoveToEx(dc, std::clamp(origin.x, transform.plotRect.left, transform.plotRect.right), transform.plotRect.top, nullptr);
	::LineTo(dc, std::clamp(origin.x, transform.plotRect.left, transform.plotRect.right), transform.plotRect.bottom);
	::MoveToEx(dc, transform.plotRect.left, std::clamp(origin.y, transform.plotRect.top, transform.plotRect.bottom), nullptr);
	::LineTo(dc, transform.plotRect.right, std::clamp(origin.y, transform.plotRect.top, transform.plotRect.bottom));
	::DeleteObject(::SelectObject(dc, previousPen));

	::TextOutW(dc, transform.plotRect.left + 4, 3, L"Z (+forward)", 12);
	const wchar_t xAxisLabel[] = L"X (+right)";
	::TextOutW(dc, transform.plotRect.right - 66, transform.plotRect.bottom + 7, xAxisLabel, 10);

	const GUID platform = m_host.GetCurrentPlatform();
	const int featureCount = ClampFeatureCount(m_propertySet.GetInt32(platform, "FeatureCount"));
	const bool geometryEnabled =
		::SendMessageW(::GetDlgItem(m_hwndDialog, kGlobalEditBaseId + 4), BM_GETCHECK, 0, 0) == BST_CHECKED;
	for (int featureIndex = 0; featureIndex < featureCount; ++featureIndex)
	{
		char propertyName[48]{};
		BuildFeaturePropertyName(featureIndex, "X", propertyName, std::size(propertyName));
		const float x = m_propertySet.GetReal32(platform, propertyName);
		BuildFeaturePropertyName(featureIndex, "Y", propertyName, std::size(propertyName));
		const float y = m_propertySet.GetReal32(platform, propertyName);
		BuildFeaturePropertyName(featureIndex, "Z", propertyName, std::size(propertyName));
		const float z = m_propertySet.GetReal32(platform, propertyName);
		BuildFeaturePropertyName(featureIndex, "Radius", propertyName, std::size(propertyName));
		const float radius = std::max(0.0f, m_propertySet.GetReal32(platform, propertyName));
		BuildFeaturePropertyName(featureIndex, "Profile", propertyName, std::size(propertyName));
		const int profile = m_propertySet.GetInt32(platform, propertyName);
		BuildFeaturePropertyName(featureIndex, "Mask", propertyName, std::size(propertyName));
		const int mask = m_propertySet.GetInt32(platform, propertyName);
		BuildFeaturePropertyName(featureIndex, "Priority", propertyName, std::size(propertyName));
		const int priority = m_propertySet.GetInt32(platform, propertyName);

		const POINT center = WorldToScreen(transform, x, z);
		const int pixelRadius = std::max(5, static_cast<int>(std::lround(radius * transform.pixelsPerMeter)));
		const bool selected = featureIndex == m_selectedFeature;
		const size_t colorIndex = static_cast<size_t>(std::clamp(profile, 0, static_cast<int>(kFeatureColors.size()) - 1));
		const COLORREF featureColor = geometryEnabled ? kFeatureColors[colorIndex] : RGB(94, 97, 102);
		HBRUSH featureBrush = ::CreateSolidBrush(featureColor);
		HPEN featurePen = ::CreatePen(PS_SOLID, selected ? 4 : 2,
			selected ? RGB(255, 220, 62) : RGB(205, 210, 216));
		HGDIOBJ previousBrush = nullptr;
		SelectGdiObject(dc, featurePen, previousPen);
		SelectGdiObject(dc, featureBrush, previousBrush);
		::Ellipse(dc,
			center.x - pixelRadius, center.y - pixelRadius,
			center.x + pixelRadius, center.y + pixelRadius);
		::SelectObject(dc, previousBrush);
		::SelectObject(dc, previousPen);
		::DeleteObject(featureBrush);
		::DeleteObject(featurePen);

		if (selected)
		{
			const POINT radiusHandle{center.x + pixelRadius, center.y};
			HBRUSH handleBrush = ::CreateSolidBrush(RGB(255, 220, 62));
			HPEN handlePen = ::CreatePen(PS_SOLID, 1, RGB(32, 32, 32));
			SelectGdiObject(dc, handlePen, previousPen);
			SelectGdiObject(dc, handleBrush, previousBrush);
			::Rectangle(dc,
				radiusHandle.x - 5, radiusHandle.y - 5,
				radiusHandle.x + 6, radiusHandle.y + 6);
			::SelectObject(dc, previousBrush);
			::SelectObject(dc, previousPen);
			::DeleteObject(handleBrush);
			::DeleteObject(handlePen);
		}

		wchar_t label[128]{};
		std::swprintf(label, std::size(label), L"F%d  P%d M%d Pr%d  Y %.2g",
			featureIndex + 1, profile, mask, priority, y);
		::SetTextColor(dc, selected ? RGB(255, 230, 105) : RGB(235, 237, 240));
		::TextOutW(dc, center.x - pixelRadius, center.y - 8,
			label, static_cast<int>(std::wcslen(label)));
	}

	const float listenerX = m_propertySet.GetReal32(platform, "ListenerX");
	const float listenerZ = m_propertySet.GetReal32(platform, "ListenerZ");
	const float yaw = m_propertySet.GetReal32(platform, "ListenerYawDegrees") * kPi / 180.0f;
	const POINT listener = WorldToScreen(transform, listenerX, listenerZ);
	constexpr float arrowLength = 46.0f;
	const POINT arrowTip{
		listener.x + static_cast<LONG>(std::lround(std::sin(yaw) * arrowLength)),
		listener.y - static_cast<LONG>(std::lround(std::cos(yaw) * arrowLength)),
	};

	HPEN listenerPen = ::CreatePen(PS_SOLID, 3, RGB(75, 226, 241));
	SelectGdiObject(dc, listenerPen, previousPen);
	::MoveToEx(dc, listener.x, listener.y, nullptr);
	::LineTo(dc, arrowTip.x, arrowTip.y);

	const float screenAngle = std::atan2(
		static_cast<float>(arrowTip.y - listener.y),
		static_cast<float>(arrowTip.x - listener.x));
	for (float offset : {-2.55f, 2.55f})
	{
		::MoveToEx(dc, arrowTip.x, arrowTip.y, nullptr);
		::LineTo(dc,
			arrowTip.x + static_cast<LONG>(std::lround(std::cos(screenAngle + offset) * 12.0f)),
			arrowTip.y + static_cast<LONG>(std::lround(std::sin(screenAngle + offset) * 12.0f)));
	}
	::DeleteObject(::SelectObject(dc, previousPen));

	HBRUSH listenerBrush = ::CreateSolidBrush(RGB(244, 76, 96));
	HPEN listenerOutline = ::CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
	HGDIOBJ previousBrush = nullptr;
	SelectGdiObject(dc, listenerOutline, previousPen);
	SelectGdiObject(dc, listenerBrush, previousBrush);
	::Ellipse(dc, listener.x - 8, listener.y - 8, listener.x + 8, listener.y + 8);
	::Ellipse(dc, arrowTip.x - 5, arrowTip.y - 5, arrowTip.x + 5, arrowTip.y + 5);
	::SelectObject(dc, previousBrush);
	::SelectObject(dc, previousPen);
	::DeleteObject(listenerBrush);
	::DeleteObject(listenerOutline);

	wchar_t listenerLabel[128]{};
	std::swprintf(listenerLabel, std::size(listenerLabel), L"Listener  X %.3g  Z %.3g  yaw %.3g\x00B0",
		listenerX, listenerZ, NormalizeYaw(m_propertySet.GetReal32(platform, "ListenerYawDegrees")));
	::SetTextColor(dc, RGB(255, 230, 234));
	::TextOutW(dc, transform.plotRect.left + 5, transform.plotRect.bottom - 20,
		listenerLabel, static_cast<int>(std::wcslen(listenerLabel)));

	const wchar_t hint[] = L"Drag listener/arrow/circles; yellow handle resizes; Add/Delete on right.";
	::SetTextColor(dc, RGB(174, 178, 184));
	::TextOutW(dc, transform.plotRect.left + 5, transform.plotRect.top + 5,
		hint, static_cast<int>(std::wcslen(hint)));

	::BitBlt(windowDc, 0, 0, width, height, dc, 0, 0, SRCCOPY);
	::SelectObject(dc, previousBitmap);
	::DeleteObject(bitmap);
	::DeleteDC(dc);
	::EndPaint(in_hWnd, &paint);
}

RealWorldWeatherAcousticsPluginGUI::CanvasTransform
RealWorldWeatherAcousticsPluginGUI::CalculateCanvasTransform(const RECT& in_clientRect) const
{
	CanvasTransform transform{};
	transform.plotRect = RECT{
		36,
		18,
		std::max(56L, in_clientRect.right - 18),
		std::max(50L, in_clientRect.bottom - 30),
	};

	const GUID platform = m_host.GetCurrentPlatform();
	const float listenerX = m_propertySet.GetReal32(platform, "ListenerX");
	const float listenerZ = m_propertySet.GetReal32(platform, "ListenerZ");
	float minimumX = listenerX;
	float maximumX = listenerX;
	float minimumZ = listenerZ;
	float maximumZ = listenerZ;

	const int featureCount = ClampFeatureCount(m_propertySet.GetInt32(platform, "FeatureCount"));
	for (int featureIndex = 0; featureIndex < featureCount; ++featureIndex)
	{
		char propertyName[48]{};
		BuildFeaturePropertyName(featureIndex, "X", propertyName, std::size(propertyName));
		const float x = m_propertySet.GetReal32(platform, propertyName);
		BuildFeaturePropertyName(featureIndex, "Z", propertyName, std::size(propertyName));
		const float z = m_propertySet.GetReal32(platform, propertyName);
		BuildFeaturePropertyName(featureIndex, "Radius", propertyName, std::size(propertyName));
		const float radius = std::max(0.0f, m_propertySet.GetReal32(platform, propertyName));
		minimumX = std::min(minimumX, x - radius);
		maximumX = std::max(maximumX, x + radius);
		minimumZ = std::min(minimumZ, z - radius);
		maximumZ = std::max(maximumZ, z + radius);
	}

	float spanX = std::max(20.0f, maximumX - minimumX);
	float spanZ = std::max(20.0f, maximumZ - minimumZ);
	transform.worldCenterX = (minimumX + maximumX) * 0.5f;
	transform.worldCenterZ = (minimumZ + maximumZ) * 0.5f;
	const float availableWidth = static_cast<float>(std::max(1L, transform.plotRect.right - transform.plotRect.left));
	const float availableHeight = static_cast<float>(std::max(1L, transform.plotRect.bottom - transform.plotRect.top));
	transform.pixelsPerMeter = std::max(0.01f, std::min(availableWidth / (spanX * 1.2f), availableHeight / (spanZ * 1.2f)));
	return transform;
}

POINT RealWorldWeatherAcousticsPluginGUI::WorldToScreen(
	const CanvasTransform& in_transform,
	float in_x,
	float in_z) const
{
	const float centerX = (in_transform.plotRect.left + in_transform.plotRect.right) * 0.5f;
	const float centerY = (in_transform.plotRect.top + in_transform.plotRect.bottom) * 0.5f;
	return POINT{
		static_cast<LONG>(std::lround(centerX + (in_x - in_transform.worldCenterX) * in_transform.pixelsPerMeter)),
		static_cast<LONG>(std::lround(centerY - (in_z - in_transform.worldCenterZ) * in_transform.pixelsPerMeter)),
	};
}

void RealWorldWeatherAcousticsPluginGUI::ScreenToWorld(
	const CanvasTransform& in_transform,
	POINT in_point,
	float& out_x,
	float& out_z) const
{
	const float centerX = (in_transform.plotRect.left + in_transform.plotRect.right) * 0.5f;
	const float centerY = (in_transform.plotRect.top + in_transform.plotRect.bottom) * 0.5f;
	out_x = in_transform.worldCenterX + (static_cast<float>(in_point.x) - centerX) / in_transform.pixelsPerMeter;
	out_z = in_transform.worldCenterZ - (static_cast<float>(in_point.y) - centerY) / in_transform.pixelsPerMeter;
}

int RealWorldWeatherAcousticsPluginGUI::HitTestFeature(
	POINT in_point,
	const CanvasTransform& in_transform) const
{
	const GUID platform = m_host.GetCurrentPlatform();
	const int featureCount = ClampFeatureCount(m_propertySet.GetInt32(platform, "FeatureCount"));
	auto hitFeature = [&](int in_featureIndex)
	{
		char propertyName[48]{};
		BuildFeaturePropertyName(in_featureIndex, "X", propertyName, std::size(propertyName));
		const float x = m_propertySet.GetReal32(platform, propertyName);
		BuildFeaturePropertyName(in_featureIndex, "Z", propertyName, std::size(propertyName));
		const float z = m_propertySet.GetReal32(platform, propertyName);
		BuildFeaturePropertyName(in_featureIndex, "Radius", propertyName, std::size(propertyName));
		const float radius = std::max(0.0f, m_propertySet.GetReal32(platform, propertyName));
		const POINT center = WorldToScreen(in_transform, x, z);
		const float dx = static_cast<float>(in_point.x - center.x);
		const float dy = static_cast<float>(in_point.y - center.y);
		const float pixelRadius = std::max(7.0f, radius * in_transform.pixelsPerMeter);
		return dx * dx + dy * dy <= pixelRadius * pixelRadius;
	};

	if (m_selectedFeature < featureCount && hitFeature(m_selectedFeature))
		return m_selectedFeature;

	for (int featureIndex = featureCount - 1; featureIndex >= 0; --featureIndex)
	{
		if (featureIndex != m_selectedFeature && hitFeature(featureIndex))
			return featureIndex;
	}
	return -1;
}

bool RealWorldWeatherAcousticsPluginGUI::HitTestFeatureRadiusHandle(
	POINT in_point,
	const CanvasTransform& in_transform) const
{
	const GUID platform = m_host.GetCurrentPlatform();
	const int featureCount = ClampFeatureCount(m_propertySet.GetInt32(platform, "FeatureCount"));
	if (featureCount <= 0 || m_selectedFeature < 0 || m_selectedFeature >= featureCount)
		return false;

	char propertyName[48]{};
	BuildFeaturePropertyName(m_selectedFeature, "X", propertyName, std::size(propertyName));
	const float x = m_propertySet.GetReal32(platform, propertyName);
	BuildFeaturePropertyName(m_selectedFeature, "Z", propertyName, std::size(propertyName));
	const float z = m_propertySet.GetReal32(platform, propertyName);
	BuildFeaturePropertyName(m_selectedFeature, "Radius", propertyName, std::size(propertyName));
	const float radius = std::max(0.0f, m_propertySet.GetReal32(platform, propertyName));
	const POINT center = WorldToScreen(in_transform, x, z);
	const POINT handle{
		center.x + std::max(5L, static_cast<LONG>(std::lround(radius * in_transform.pixelsPerMeter))),
		center.y,
	};
	const float deltaX = static_cast<float>(in_point.x - handle.x);
	const float deltaY = static_cast<float>(in_point.y - handle.y);
	return deltaX * deltaX + deltaY * deltaY <= 9.0f * 9.0f;
}

bool RealWorldWeatherAcousticsPluginGUI::HitTestListener(
	POINT in_point,
	const CanvasTransform& in_transform,
	bool& out_isYawHandle) const
{
	const GUID platform = m_host.GetCurrentPlatform();
	const float listenerX = m_propertySet.GetReal32(platform, "ListenerX");
	const float listenerZ = m_propertySet.GetReal32(platform, "ListenerZ");
	const float yaw = m_propertySet.GetReal32(platform, "ListenerYawDegrees") * kPi / 180.0f;
	const POINT listener = WorldToScreen(in_transform, listenerX, listenerZ);
	const POINT arrowTip{
		listener.x + static_cast<LONG>(std::lround(std::sin(yaw) * 46.0f)),
		listener.y - static_cast<LONG>(std::lround(std::cos(yaw) * 46.0f)),
	};

	auto within = [&](POINT in_center, float in_radius)
	{
		const float dx = static_cast<float>(in_point.x - in_center.x);
		const float dy = static_cast<float>(in_point.y - in_center.y);
		return dx * dx + dy * dy <= in_radius * in_radius;
	};

	if (within(arrowTip, 11.0f))
	{
		out_isYawHandle = true;
		return true;
	}
	if (within(listener, 12.0f))
	{
		out_isYawHandle = false;
		return true;
	}
	return false;
}

void RealWorldWeatherAcousticsPluginGUI::BeginDrag(
	DragMode in_mode,
	const CanvasTransform& in_transform,
	POINT in_point,
	int in_featureIndex)
{
	EndDrag();
	m_dragMode = in_mode;
	m_dragTransform = in_transform;
	m_dragFeatureIndex = in_featureIndex;
	m_dragPointerOffsetX = 0.0f;
	m_dragPointerOffsetZ = 0.0f;
	m_dragChanged = false;

	float pointerX = 0.0f;
	float pointerZ = 0.0f;
	ScreenToWorld(in_transform, in_point, pointerX, pointerZ);
	const GUID platform = m_host.GetCurrentPlatform();
	if (in_mode == DragMode::ListenerPosition)
	{
		m_dragPointerOffsetX = m_propertySet.GetReal32(platform, "ListenerX") - pointerX;
		m_dragPointerOffsetZ = m_propertySet.GetReal32(platform, "ListenerZ") - pointerZ;
	}
	else if (in_mode == DragMode::FeaturePosition || in_mode == DragMode::FeatureRadius)
	{
		const int featureCount = ClampFeatureCount(m_propertySet.GetInt32(platform, "FeatureCount"));
		if (in_featureIndex < 0 || in_featureIndex >= featureCount)
		{
			m_dragMode = DragMode::None;
			m_dragFeatureIndex = -1;
			return;
		}
		if (in_mode == DragMode::FeaturePosition)
		{
			char propertyName[48]{};
			BuildFeaturePropertyName(in_featureIndex, "X", propertyName, std::size(propertyName));
			m_dragPointerOffsetX = m_propertySet.GetReal32(platform, propertyName) - pointerX;
			BuildFeaturePropertyName(in_featureIndex, "Z", propertyName, std::size(propertyName));
			m_dragPointerOffsetZ = m_propertySet.GetReal32(platform, propertyName) - pointerZ;
		}
	}

	m_dragUndoGroup = 0;
	if (m_undoManager.CanAddEvent())
		m_dragUndoGroup = m_undoManager.OpenGroup();
	::SetCapture(m_hwndCanvas);
}

void RealWorldWeatherAcousticsPluginGUI::UpdateDrag(POINT in_point)
{
	if (m_dragMode == DragMode::None)
		return;

	const GUID platform = m_host.GetCurrentPlatform();
	auto setReal32IfChanged = [&](const char* in_property, float in_value)
	{
		if (std::fabs(m_propertySet.GetReal32(platform, in_property) - in_value) <= 0.00001f)
			return false;
		return m_propertySet.SetValueReal32(platform, in_property, in_value);
	};
	float worldX = 0.0f;
	float worldZ = 0.0f;
	ScreenToWorld(m_dragTransform, in_point, worldX, worldZ);

	if (m_dragMode == DragMode::ListenerPosition)
	{
		const bool changedX = setReal32IfChanged("ListenerX", worldX + m_dragPointerOffsetX);
		const bool changedZ = setReal32IfChanged("ListenerZ", worldZ + m_dragPointerOffsetZ);
		m_dragChanged = changedX || changedZ || m_dragChanged;
	}
	else if (m_dragMode == DragMode::ListenerYaw)
	{
		const float listenerX = m_propertySet.GetReal32(platform, "ListenerX");
		const float listenerZ = m_propertySet.GetReal32(platform, "ListenerZ");
		const float deltaX = worldX - listenerX;
		const float deltaZ = worldZ - listenerZ;
		if (deltaX * deltaX + deltaZ * deltaZ > 0.0001f)
		{
			const float yaw = NormalizeYaw(std::atan2(deltaX, deltaZ) * 180.0f / kPi);
			m_dragChanged = setReal32IfChanged("ListenerYawDegrees", yaw) || m_dragChanged;
		}
	}
	else
	{
		const int featureCount = ClampFeatureCount(m_propertySet.GetInt32(platform, "FeatureCount"));
		if (m_dragFeatureIndex < 0 || m_dragFeatureIndex >= featureCount)
		{
			EndDrag();
			return;
		}

		char propertyName[48]{};
		if (m_dragMode == DragMode::FeaturePosition)
		{
			BuildFeaturePropertyName(m_dragFeatureIndex, "X", propertyName, std::size(propertyName));
			const bool changedX = setReal32IfChanged(propertyName, worldX + m_dragPointerOffsetX);
			BuildFeaturePropertyName(m_dragFeatureIndex, "Z", propertyName, std::size(propertyName));
			const bool changedZ = setReal32IfChanged(propertyName, worldZ + m_dragPointerOffsetZ);
			m_dragChanged = changedX || changedZ || m_dragChanged;
		}
		else
		{
			BuildFeaturePropertyName(m_dragFeatureIndex, "X", propertyName, std::size(propertyName));
			const float centerX = m_propertySet.GetReal32(platform, propertyName);
			BuildFeaturePropertyName(m_dragFeatureIndex, "Z", propertyName, std::size(propertyName));
			const float centerZ = m_propertySet.GetReal32(platform, propertyName);
			const float radius = std::max(kMinimumFeatureRadius, std::hypot(worldX - centerX, worldZ - centerZ));
			BuildFeaturePropertyName(m_dragFeatureIndex, "Radius", propertyName, std::size(propertyName));
			m_dragChanged = setReal32IfChanged(propertyName, radius) || m_dragChanged;
		}
	}

	UpdateControls();
	InvalidatePreview();
}

void RealWorldWeatherAcousticsPluginGUI::EndDrag()
{
	if (m_dragMode == DragMode::None)
		return;

	const DragMode completedMode = m_dragMode;
	const bool changed = m_dragChanged;
	m_dragMode = DragMode::None;
	m_dragFeatureIndex = -1;
	m_dragChanged = false;
	if (::GetCapture() == m_hwndCanvas)
		::ReleaseCapture();
	if (m_dragUndoGroup != 0)
	{
		const char* undoName = "Edit RWWA preview";
		switch (completedMode)
		{
		case DragMode::ListenerPosition: undoName = "Move RWWA Listener"; break;
		case DragMode::ListenerYaw: undoName = "Rotate RWWA Listener"; break;
		case DragMode::FeaturePosition: undoName = "Move RWWA Feature"; break;
		case DragMode::FeatureRadius: undoName = "Resize RWWA Feature"; break;
		default: break;
		}
		m_undoManager.CloseGroup(
			changed ? AK_WWISE_PLUGIN_UNDO_GROUP_CLOSE_ACTION_APPLY : AK_WWISE_PLUGIN_UNDO_GROUP_CLOSE_ACTION_CANCEL,
			m_dragUndoGroup,
			undoName);
		m_dragUndoGroup = 0;
	}
	InvalidatePreview();
}

void RealWorldWeatherAcousticsPluginGUI::InvalidatePreview()
{
	if (m_hwndCanvas)
		::InvalidateRect(m_hwndCanvas, nullptr, FALSE);
}

LRESULT CALLBACK RealWorldWeatherAcousticsPluginGUI::CanvasWindowProc(
	HWND in_hWnd,
	UINT in_message,
	WPARAM in_wParam,
	LPARAM in_lParam)
{
	RealWorldWeatherAcousticsPluginGUI* instance = reinterpret_cast<RealWorldWeatherAcousticsPluginGUI*>(
		::GetWindowLongPtrW(in_hWnd, GWLP_USERDATA));
	if (in_message == WM_NCCREATE)
	{
		const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(in_lParam);
		instance = static_cast<RealWorldWeatherAcousticsPluginGUI*>(create->lpCreateParams);
		::SetWindowLongPtrW(in_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
	}

	return instance
		? instance->HandleCanvasMessage(in_hWnd, in_message, in_wParam, in_lParam)
		: ::DefWindowProcW(in_hWnd, in_message, in_wParam, in_lParam);
}

LRESULT RealWorldWeatherAcousticsPluginGUI::HandleCanvasMessage(
	HWND in_hWnd,
	UINT in_message,
	WPARAM in_wParam,
	LPARAM in_lParam)
{
	switch (in_message)
	{
	case WM_PAINT:
		PaintCanvas(in_hWnd);
		// Wwise populates the empty host dialog after WM_INITDIALOG and may clear
		// dynamically-created edit controls in that pass. A canvas paint happens
		// after the selected Source/platform is bound, so refresh the custom
		// inspector here. UpdateControls skips the focused edit and never repaints
		// the canvas, avoiding both edit disruption and paint recursion.
		UpdateControls();
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_LBUTTONDOWN:
	{
		::SetFocus(in_hWnd);
		RECT client{};
		::GetClientRect(in_hWnd, &client);
		const CanvasTransform transform = CalculateCanvasTransform(client);
		const POINT point{GET_X_LPARAM(in_lParam), GET_Y_LPARAM(in_lParam)};
		bool isYawHandle = false;
		if (HitTestListener(point, transform, isYawHandle))
		{
			BeginDrag(isYawHandle ? DragMode::ListenerYaw : DragMode::ListenerPosition, transform, point);
			return 0;
		}
		if (HitTestFeatureRadiusHandle(point, transform))
		{
			BeginDrag(DragMode::FeatureRadius, transform, point, m_selectedFeature);
			return 0;
		}

		const int featureIndex = HitTestFeature(point, transform);
		if (featureIndex >= 0)
		{
			SelectFeature(featureIndex);
			BeginDrag(DragMode::FeaturePosition, transform, point, featureIndex);
		}
		return 0;
	}

	case WM_MOUSEMOVE:
		if (m_dragMode != DragMode::None)
		{
			if ((in_wParam & MK_LBUTTON) != 0)
				UpdateDrag(POINT{GET_X_LPARAM(in_lParam), GET_Y_LPARAM(in_lParam)});
			else
				EndDrag();
			return 0;
		}
		break;

	case WM_LBUTTONUP:
		if (m_dragMode != DragMode::None)
		{
			UpdateDrag(POINT{GET_X_LPARAM(in_lParam), GET_Y_LPARAM(in_lParam)});
			EndDrag();
			return 0;
		}
		break;

	case WM_CAPTURECHANGED:
		EndDrag();
		return 0;

	case WM_KEYDOWN:
		if (in_wParam == VK_DELETE)
		{
			DeleteSelectedFeature();
			return 0;
		}
		break;

	case WM_SETCURSOR:
		if (LOWORD(in_lParam) == HTCLIENT)
		{
			LPCWSTR cursorName = IDC_ARROW;
			if (m_dragMode == DragMode::FeatureRadius)
				cursorName = IDC_SIZEWE;
			else if (m_dragMode == DragMode::FeaturePosition || m_dragMode == DragMode::ListenerPosition)
				cursorName = IDC_SIZEALL;
			else if (m_dragMode == DragMode::ListenerYaw)
				cursorName = IDC_CROSS;
			else
			{
				POINT point{};
				const DWORD messagePosition = ::GetMessagePos();
				point.x = GET_X_LPARAM(messagePosition);
				point.y = GET_Y_LPARAM(messagePosition);
				::ScreenToClient(in_hWnd, &point);
				RECT client{};
				::GetClientRect(in_hWnd, &client);
				const CanvasTransform transform = CalculateCanvasTransform(client);
				bool isYawHandle = false;
				if (HitTestListener(point, transform, isYawHandle))
					cursorName = isYawHandle ? IDC_CROSS : IDC_SIZEALL;
				else if (HitTestFeatureRadiusHandle(point, transform))
					cursorName = IDC_SIZEWE;
				else if (HitTestFeature(point, transform) >= 0)
					cursorName = IDC_SIZEALL;
			}
			::SetCursor(::LoadCursorW(nullptr, cursorName));
			return TRUE;
		}
		break;

	default:
		break;
	}

	return ::DefWindowProcW(in_hWnd, in_message, in_wParam, in_lParam);
}

AK_ADD_PLUGIN_CLASS_TO_CONTAINER(
	RealWorldWeatherAcoustics,
	RealWorldWeatherAcousticsPluginGUI,
	RealWorldWeatherAcousticsSource
);
