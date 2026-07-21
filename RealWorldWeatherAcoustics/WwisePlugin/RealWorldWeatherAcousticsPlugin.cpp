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

#include "RealWorldWeatherAcousticsPlugin.h"
#include "../SoundEnginePlugin/RealWorldWeatherAcousticsSourceFactory.h"

#include <limits>

namespace
{
#define RWWA_FEATURE_PROPERTIES(slot) \
    { "Feature" #slot "X", "Feature" #slot "Y", "Feature" #slot "Z", "Feature" #slot "Radius", \
      "Feature" #slot "Profile", "Feature" #slot "Mask", "Feature" #slot "Priority" }

const char* const kFeatureProperties[8][7] =
{
    RWWA_FEATURE_PROPERTIES(1),
    RWWA_FEATURE_PROPERTIES(2),
    RWWA_FEATURE_PROPERTIES(3),
    RWWA_FEATURE_PROPERTIES(4),
    RWWA_FEATURE_PROPERTIES(5),
    RWWA_FEATURE_PROPERTIES(6),
    RWWA_FEATURE_PROPERTIES(7),
    RWWA_FEATURE_PROPERTIES(8),
};

#undef RWWA_FEATURE_PROPERTIES
}

RealWorldWeatherAcousticsPlugin::RealWorldWeatherAcousticsPlugin()
{
}

RealWorldWeatherAcousticsPlugin::~RealWorldWeatherAcousticsPlugin()
{
}

bool RealWorldWeatherAcousticsPlugin::GetBankParameters(const GUID & in_guidPlatform, AK::Wwise::Plugin::DataWriter& in_dataWriter) const
{
    bool success = true;
    success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "Duration"));
    success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "MasterGainDb"));
    success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "RainIntensity"));
    success &= in_dataWriter.WriteInt32(m_propertySet.GetInt32(in_guidPlatform, "Seed"));
    success &= in_dataWriter.WriteBool(m_propertySet.GetBool(in_guidPlatform, "GeometryEnabled"));
    success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "ListenerX"));
    success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "ListenerY"));
    success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "ListenerZ"));
    success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "ListenerYawDegrees"));
    success &= in_dataWriter.WriteInt32(m_propertySet.GetInt32(in_guidPlatform, "FeatureCount"));

    for (AkUInt32 slot = 0; slot < 8; ++slot)
    {
        success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, kFeatureProperties[slot][0]));
        success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, kFeatureProperties[slot][1]));
        success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, kFeatureProperties[slot][2]));
        success &= in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, kFeatureProperties[slot][3]));
        success &= in_dataWriter.WriteInt32(m_propertySet.GetInt32(in_guidPlatform, kFeatureProperties[slot][4]));
        success &= in_dataWriter.WriteInt32(m_propertySet.GetInt32(in_guidPlatform, kFeatureProperties[slot][5]));
        success &= in_dataWriter.WriteInt32(m_propertySet.GetInt32(in_guidPlatform, kFeatureProperties[slot][6]));
    }

    return success;
}

bool RealWorldWeatherAcousticsPlugin::GetSourceDuration(double& out_minDuration, double& out_maxDuration) const
{
    if (m_propertySet.PropertyHasRTPC("Duration"))
    {
        out_minDuration = 0.0;
        out_maxDuration = (std::numeric_limits<float>::max)();
        return false;
    }

    const double duration = m_propertySet.GetReal32(m_host.GetCurrentPlatform(), "Duration");
    out_minDuration = duration;
    out_maxDuration = duration;
    return true;
}

AK_DEFINE_PLUGIN_CONTAINER(RealWorldWeatherAcoustics);											// Create a PluginContainer structure that contains the info for our plugin
AK_EXPORT_PLUGIN_CONTAINER(RealWorldWeatherAcoustics);											// This is a DLL, we want to have a standardized name
AK_ADD_PLUGIN_CLASS_TO_CONTAINER(                                             // Add our CLI class to the PluginContainer
    RealWorldWeatherAcoustics,        // Name of the plug-in container for this shared library
    RealWorldWeatherAcousticsPlugin,  // Authoring plug-in class to add to the plug-in container
    RealWorldWeatherAcousticsSource   // Corresponding Sound Engine plug-in class
);
DEFINE_PLUGIN_REGISTER_HOOK

DEFINEDUMMYASSERTHOOK;							// Placeholder assert hook for Wwise plug-ins using AKASSERT (cassert used by default)
