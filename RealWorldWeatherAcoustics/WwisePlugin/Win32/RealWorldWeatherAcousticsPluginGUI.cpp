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

constexpr int kMaximumFeatures = 8;
constexpr float kPi = 3.14159265358979323846f;

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

constexpr std::array<PropertyBinding, 10> kGlobalBindings{{
	{L"Duration", "Duration", PropertyKind::Real32},
	{L"Master gain (dB)", "MasterGainDb", PropertyKind::Real32},
	{L"Rain intensity", "RainIntensity", PropertyKind::Real32},
	{L"Seed", "Seed", PropertyKind::Int32},
	{L"Geometry enabled", "GeometryEnabled", PropertyKind::Bool},
	{L"Listener X", "ListenerX", PropertyKind::Real32},
	{L"Listener Y", "ListenerY", PropertyKind::Real32},
	{L"Listener Z", "ListenerZ", PropertyKind::Real32},
	{L"Listener yaw", "ListenerYawDegrees", PropertyKind::Real32},
	{L"Feature count", "FeatureCount", PropertyKind::Int32},
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
	L"Open Field",
	L"Single Metal",
	L"Multi-Material Ring",
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
	out_pTable = nullptr;
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
			else if (notification == EN_KILLFOCUS || notification == BN_CLICKED)
				CommitControl(controlId);
		}
		break;

	case WM_ENABLE:
		for (HWND child = ::GetWindow(in_hWnd, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
			::EnableWindow(child, static_cast<BOOL>(in_wParam));
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
		setFont(::CreateWindowExW(
			WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
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
	constexpr int minimumInspectorWidth = 220;
	const int inspectorWidth = std::clamp(width / 3, minimumInspectorWidth, 310);
	const int canvasWidth = std::max(160, width - inspectorWidth - gap - margin * 2);
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

	const int rowHeight = std::clamp((height - 70) / 17, 18, 23);
	const int titleHeight = 20;
	const int labelWidth = std::clamp(inspectorWidth / 2, 92, 135);
	const int editWidth = std::max(72, inspectorWidth - labelWidth - 8);
	int y = margin;

	::MoveWindow(::GetDlgItem(m_hwndDialog, kGlobalTitleId), inspectorX, y, inspectorWidth, titleHeight, TRUE);
	y += titleHeight;

	for (size_t index = 0; index < kGlobalBindings.size(); ++index)
	{
		const UINT controlId = kGlobalEditBaseId + static_cast<UINT>(index);
		if (kGlobalBindings[index].kind == PropertyKind::Bool)
		{
			::MoveWindow(::GetDlgItem(m_hwndDialog, controlId), inspectorX + 2, y, inspectorWidth - 2, rowHeight, TRUE);
		}
		else
		{
			::MoveWindow(::GetDlgItem(m_hwndDialog, kGlobalLabelBaseId + static_cast<UINT>(index)),
				inspectorX, y + 3, labelWidth - 6, rowHeight - 3, TRUE);
			::MoveWindow(::GetDlgItem(m_hwndDialog, controlId),
				inspectorX + labelWidth, y, editWidth, rowHeight - 2, TRUE);
		}
		y += rowHeight;
	}

	y += 5;
	::MoveWindow(::GetDlgItem(m_hwndDialog, kSelectedFeatureTitleId), inspectorX, y, inspectorWidth, titleHeight, TRUE);
	y += titleHeight;

	for (size_t index = 0; index < kFeatureBindings.size(); ++index)
	{
		::MoveWindow(::GetDlgItem(m_hwndDialog, kFeatureLabelBaseId + static_cast<UINT>(index)),
			inspectorX, y + 3, labelWidth - 6, rowHeight - 3, TRUE);
		::MoveWindow(::GetDlgItem(m_hwndDialog, kFeatureEditBaseId + static_cast<UINT>(index)),
			inspectorX + labelWidth, y, editWidth, rowHeight - 2, TRUE);
		y += rowHeight;
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
			::SendMessageW(control, BM_SETCHECK,
				m_propertySet.GetBool(platform, binding.property) ? BST_CHECKED : BST_UNCHECKED, 0);
		}
		else if (control != focusedControl)
		{
			if (binding.kind == PropertyKind::Int32)
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
		std::swprintf(text, std::size(text), L"Selected feature: %d of %d", m_selectedFeature + 1, featureCount);
	else
		std::swprintf(text, std::size(text), L"Selected feature: none");
	::SetWindowTextW(::GetDlgItem(m_hwndDialog, kSelectedFeatureTitleId), text);

	for (size_t index = 0; index < kFeatureBindings.size(); ++index)
	{
		HWND control = ::GetDlgItem(m_hwndDialog, kFeatureEditBaseId + static_cast<UINT>(index));
		::EnableWindow(control, featureCount > 0);
		::EnableWindow(::GetDlgItem(m_hwndDialog, kFeatureLabelBaseId + static_cast<UINT>(index)), featureCount > 0);
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
	AK::Wwise::Plugin::AutoUndoGroup undoGroup(m_undoManager, "Edit weather acoustics preview");
	bool changed = false;

	if (kind == PropertyKind::Bool)
	{
		changed = m_propertySet.SetValueBool(platform, property,
			::SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
	}
	else if (kind == PropertyKind::Int32)
	{
		int32_t value = 0;
		if (TryParseInt32(control, value))
		{
			if (std::strcmp(property, "FeatureCount") == 0)
				value = ClampFeatureCount(value);
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
	m_propertySet.SetValueReal32(platform, "RainIntensity", 0.65f);
	m_propertySet.SetValueInt32(platform, "Seed", 1337);
	m_propertySet.SetValueReal32(platform, "ListenerX", 0.0f);
	m_propertySet.SetValueReal32(platform, "ListenerY", 0.0f);
	m_propertySet.SetValueReal32(platform, "ListenerZ", 0.0f);
	m_propertySet.SetValueReal32(platform, "ListenerYawDegrees", 0.0f);

	if (in_presetIndex == 0)
	{
		m_propertySet.SetValueBool(platform, "GeometryEnabled", false);
		m_propertySet.SetValueInt32(platform, "FeatureCount", 0);
		m_selectedFeature = 0;
	}
	else
	{
		const int featureCount = in_presetIndex == 1 ? 1 : 4;
		constexpr std::array<float, 4> featureX{{0.0f, 6.0f, 0.0f, -6.0f}};
		constexpr std::array<float, 4> featureZ{{6.0f, 0.0f, -6.0f, 0.0f}};
		m_propertySet.SetValueBool(platform, "GeometryEnabled", true);
		m_propertySet.SetValueInt32(platform, "FeatureCount", featureCount);
		for (int featureIndex = 0; featureIndex < featureCount; ++featureIndex)
		{
			char propertyName[48]{};
			BuildFeaturePropertyName(featureIndex, "X", propertyName, std::size(propertyName));
			m_propertySet.SetValueReal32(platform, propertyName, featureX[featureIndex]);
			BuildFeaturePropertyName(featureIndex, "Y", propertyName, std::size(propertyName));
			m_propertySet.SetValueReal32(platform, propertyName, 0.0f);
			BuildFeaturePropertyName(featureIndex, "Z", propertyName, std::size(propertyName));
			m_propertySet.SetValueReal32(platform, propertyName, featureZ[featureIndex]);
			BuildFeaturePropertyName(featureIndex, "Radius", propertyName, std::size(propertyName));
			m_propertySet.SetValueReal32(platform, propertyName, 2.0f);
			BuildFeaturePropertyName(featureIndex, "Profile", propertyName, std::size(propertyName));
			m_propertySet.SetValueInt32(platform, propertyName, featureIndex);
			BuildFeaturePropertyName(featureIndex, "Mask", propertyName, std::size(propertyName));
			m_propertySet.SetValueInt32(platform, propertyName, 1);
			BuildFeaturePropertyName(featureIndex, "Priority", propertyName, std::size(propertyName));
			m_propertySet.SetValueInt32(platform, propertyName, 1);
		}
		m_selectedFeature = 0;
	}

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
	const bool geometryEnabled = m_propertySet.GetBool(platform, "GeometryEnabled");
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

	const wchar_t hint[] = L"Drag listener point to move X/Z; drag arrow handle to rotate yaw. Click a circle to edit it.";
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
	const CanvasTransform& in_transform)
{
	EndDrag();
	m_dragMode = in_mode;
	m_dragTransform = in_transform;
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
	float worldX = 0.0f;
	float worldZ = 0.0f;
	ScreenToWorld(m_dragTransform, in_point, worldX, worldZ);

	if (m_dragMode == DragMode::ListenerPosition)
	{
		m_propertySet.SetValueReal32(platform, "ListenerX", worldX);
		m_propertySet.SetValueReal32(platform, "ListenerZ", worldZ);
	}
	else
	{
		const float listenerX = m_propertySet.GetReal32(platform, "ListenerX");
		const float listenerZ = m_propertySet.GetReal32(platform, "ListenerZ");
		const float deltaX = worldX - listenerX;
		const float deltaZ = worldZ - listenerZ;
		if (deltaX * deltaX + deltaZ * deltaZ > 0.0001f)
		{
			const float yaw = NormalizeYaw(std::atan2(deltaX, deltaZ) * 180.0f / kPi);
			m_propertySet.SetValueReal32(platform, "ListenerYawDegrees", yaw);
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
	m_dragMode = DragMode::None;
	if (::GetCapture() == m_hwndCanvas)
		::ReleaseCapture();
	if (m_dragUndoGroup != 0)
	{
		m_undoManager.CloseGroup(
			AK_WWISE_PLUGIN_UNDO_GROUP_CLOSE_ACTION_APPLY,
			m_dragUndoGroup,
			completedMode == DragMode::ListenerPosition ? "Move weather listener" : "Rotate weather listener");
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
			BeginDrag(isYawHandle ? DragMode::ListenerYaw : DragMode::ListenerPosition, transform);
			return 0;
		}

		const int featureIndex = HitTestFeature(point, transform);
		if (featureIndex >= 0)
			SelectFeature(featureIndex);
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

	case WM_SETCURSOR:
		::SetCursor(::LoadCursorW(nullptr, IDC_CROSS));
		return TRUE;

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
