#pragma once

namespace OStrands::Revealing
{
	bool        Exposed(RE::Actor* a_actor);
	int         FhuBlocked(RE::Actor* a_actor);
	void        ScanFhuEquipment();
	std::vector<std::pair<std::string, std::string>> FhuEquipment();
	void        Mark(bool a_revealing);
	void        Toggle();
	std::string Status();
}
