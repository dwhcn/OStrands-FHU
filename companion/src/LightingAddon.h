#pragma once
#include <filesystem>
namespace OStrands::LightingAddon
{
    inline bool Enabled()
    {
        static const bool enabled = [] {
            std::error_code error;
            return std::filesystem::is_regular_file("Data/SKSE/Plugins/FHULightOriginal.ini", error);
        }();
        return enabled;
    }
}
