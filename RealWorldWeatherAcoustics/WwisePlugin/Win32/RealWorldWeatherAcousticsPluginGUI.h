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
#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <AK/Wwise/TargetVer.h>
#include <AK/AkPlatforms.h>
#if __has_include(<afxwin.h>)
#include <afxwin.h>
#define RWWA_HAS_MFC 1
#else
#include <Windows.h>
#define RWWA_HAS_MFC 0
#endif
#include <cguid.h>

#include "../RealWorldWeatherAcousticsPlugin.h"

class RealWorldWeatherAcousticsPluginGUI final
#if RWWA_HAS_MFC
	: public AK::Wwise::Plugin::PluginMFCWindows<>
	, public AK::Wwise::Plugin::GUIWindows
#else
	: public AK::Wwise::Plugin::GUIWindows
#endif
	, public AK::Wwise::Plugin::RequestHost
	, public AK::Wwise::Plugin::RequestPropertySet
	, public AK::Wwise::Plugin::RequestUndoManager
{
public:
	RealWorldWeatherAcousticsPluginGUI();

	void NotifyPropertyChanged(const GUID& in_guidPlatform, const char* in_szPropertyName) override;
	bool GetDialog(
		AK::Wwise::Plugin::eDialog in_eDialog,
		UINT& out_uiDialogID,
		AK::Wwise::Plugin::PopulateTableItem*& out_pTable) const override;
	bool WindowProc(
		AK::Wwise::Plugin::eDialog in_eDialog,
		HWND in_hWnd,
		UINT in_message,
		WPARAM in_wParam,
		LPARAM in_lParam,
		LRESULT& out_lResult) override;
	bool Help(
		HWND in_hWnd,
		AK::Wwise::Plugin::eDialog in_eDialog,
		const char* in_szLanguageCode) const override;

private:
	struct CanvasTransform
	{
		RECT plotRect{};
		float worldCenterX = 0.0f;
		float worldCenterZ = 0.0f;
		float pixelsPerMeter = 1.0f;
	};

	enum class DragMode
	{
		None,
		ListenerPosition,
		ListenerYaw,
	};

	static LRESULT CALLBACK CanvasWindowProc(HWND in_hWnd, UINT in_message, WPARAM in_wParam, LPARAM in_lParam);
	LRESULT HandleCanvasMessage(HWND in_hWnd, UINT in_message, WPARAM in_wParam, LPARAM in_lParam);

	void CreateControls(HWND in_hWnd);
	void LayoutControls();
	void UpdateControls();
	void CommitControl(UINT in_controlId);
	void SelectFeature(int in_featureIndex);
	void ApplyPreset(int in_presetIndex);

	void PaintCanvas(HWND in_hWnd);
	CanvasTransform CalculateCanvasTransform(const RECT& in_clientRect) const;
	POINT WorldToScreen(const CanvasTransform& in_transform, float in_x, float in_z) const;
	void ScreenToWorld(const CanvasTransform& in_transform, POINT in_point, float& out_x, float& out_z) const;
	int HitTestFeature(POINT in_point, const CanvasTransform& in_transform) const;
	bool HitTestListener(POINT in_point, const CanvasTransform& in_transform, bool& out_isYawHandle) const;

	void BeginDrag(DragMode in_mode, const CanvasTransform& in_transform);
	void UpdateDrag(POINT in_point);
	void EndDrag();
	void InvalidatePreview();

	HWND m_hwndDialog = nullptr;
	HWND m_hwndCanvas = nullptr;
	HFONT m_hGuiFont = nullptr;
	bool m_updatingControls = false;
	int m_selectedFeature = 0;
	DragMode m_dragMode = DragMode::None;
	CanvasTransform m_dragTransform{};
	ak_wwise_plugin_undo_group_id m_dragUndoGroup = 0;
};

#undef RWWA_HAS_MFC
