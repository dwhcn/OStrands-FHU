#include "Revealing.h"

#include "Anatomy.h"
#include "Settings.h"

namespace OStrands::Revealing
{
	namespace
	{
		std::mutex  statusLock;
		std::string status;
		std::vector<std::pair<std::string, std::string>> equipment;

		std::string Lower(std::string_view a_text)
		{
			std::string text(a_text);
			std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		std::string KeyOf(const RE::TESForm* a_form)
		{
			const auto* file = a_form ? a_form->GetFile(0) : nullptr;
			if (!file || a_form->IsDynamicForm()) {
				return {};
			}
			return std::format("{}|{:#x}", Lower(file->GetFilename()), a_form->GetLocalFormID());
		}

		bool Tagged(RE::TESObjectARMO* a_armor)
		{
			const auto& settings = Settings::Get();
			const auto  key = KeyOf(a_armor);
			if (!key.empty() && std::ranges::any_of(settings.revealingArmor, [&](const auto& a_entry) { return a_entry.first == key; })) {
				return true;
			}
			for (std::uint32_t i = 0; a_armor->keywords && i < a_armor->numKeywords; ++i) {
				const auto* keyword = a_armor->keywords[i];
				const char* id = keyword ? keyword->GetFormEditorID() : nullptr;
				if (id && *id && std::ranges::find(settings.revealingKeywords, Lower(id)) != settings.revealingKeywords.end()) {
					return true;
				}
			}
			return false;
		}

		void Report(std::string a_message)
		{
			logger::info("{}", a_message);
			RE::SendHUDMessage::ShowHUDMessage(std::format("OStrands: {}", a_message).c_str());
			std::scoped_lock guard(statusLock);
			status = std::move(a_message);
		}

		RE::Actor* Target()
		{
			const auto selected = RE::Console::GetSelectedRef();
			auto*      actor = selected ? selected->As<RE::Actor>() : nullptr;
			return actor ? actor : RE::PlayerCharacter::GetSingleton();
		}

		bool Marked(const std::string& a_key)
		{
			const auto& list = Settings::Get().revealingArmor;
			return !a_key.empty() && std::ranges::any_of(list, [&](const auto& a_entry) { return a_entry.first == a_key; });
		}
	}

	bool Exposed(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		if (!a_actor->GetParentCell() || !a_actor->Is3DLoaded()) {
			return false;
		}
		auto* armor = Anatomy::BodyArmor(a_actor);
		if (!armor) {
			return true;
		}
		auto guard = Settings::Lock();
		return Tagged(armor);
	}

	int FhuBlocked(RE::Actor* a_actor)
	{
		const auto& settings = Settings::Get();
		if (!settings.fhuClothing || !a_actor || !a_actor->GetParentCell()) {
			return 0;
		}
		const auto& biped = a_actor->GetBiped(false);
		if (!biped) {
			return 0;
		}
		// The body and the two pelvis slots are the automatic lower-body heuristic.
		using Slot = RE::BIPED_OBJECT;
		constexpr int lower = 0b011;
		auto* skin = a_actor->GetSkin();
		int blocked = 0;
		for (std::size_t i = 0; i < std::size(biped->objects); ++i) {
			const auto& object = biped->objects[i];
			auto* armor = object.item ? object.item->As<RE::TESObjectARMO>() : nullptr;
			if (!armor || armor == skin) {
				continue;
			}
			const auto key = KeyOf(armor);
			const auto rule = std::ranges::find(settings.fhuClothingRules, key, &std::pair<std::string, int>::first);
			if (rule != settings.fhuClothingRules.end()) {
				blocked |= rule->second;
				continue;
			}
			const auto slot = static_cast<Slot>(i);
			if ((slot == Slot::kBody || slot == Slot::kModPelvisPrimary || slot == Slot::kModPelvisSecondary) && !Tagged(armor)) {
				blocked |= lower;
			}
		}
		return blocked;
	}

	void ScanFhuEquipment()
	{
		std::vector<std::pair<std::string, std::string>> scanned;
		auto* actor = Target();
		if (actor && actor->GetParentCell()) {
			const auto& biped = actor->GetBiped(false);
			const auto* skin = actor->GetSkin();
			if (biped) {
				for (const auto& object : biped->objects) {
					auto* armor = object.item ? object.item->As<RE::TESObjectARMO>() : nullptr;
					if (!armor || armor == skin) {
						continue;
					}
					auto key = KeyOf(armor);
					if (key.empty() || std::ranges::find(scanned, key, &std::pair<std::string, std::string>::first) != scanned.end()) {
						continue;
					}
					scanned.emplace_back(std::move(key), armor->GetName());
				}
			}
		}
		std::scoped_lock guard(statusLock);
		equipment = std::move(scanned);
	}

	std::vector<std::pair<std::string, std::string>> FhuEquipment()
	{
		std::scoped_lock guard(statusLock);
		return equipment;
	}

	void Toggle()
	{
		auto* actor = Target();
		auto* armor = Anatomy::BodyArmor(actor);
		bool  marked = false;
		if (armor) {
			auto guard = Settings::Lock();
			marked = Marked(KeyOf(armor));
		}
		Mark(!marked);
	}

	void Mark(bool a_revealing)
	{
		auto* actor = Target();
		if (!actor) {
			return;
		}
		const std::string who = actor->GetDisplayFullName();
		auto*             armor = Anatomy::BodyArmor(actor);
		if (!armor) {
			Report(std::format("{} has nothing in the body slot.", who));
			return;
		}
		const auto key = KeyOf(armor);
		if (key.empty()) {
			Report(std::format("{}'s {} was made in-game and can't be marked.", who, armor->GetName()));
			return;
		}
		auto       guard = Settings::Lock();
		auto&      settings = Settings::Get();
		auto&      list = settings.revealingArmor;
		const auto entry = std::ranges::find_if(list, [&](const auto& a_entry) { return a_entry.first == key; });
		if (a_revealing == (entry != list.end())) {
			Report(std::format("{} ({}) already counts as {}.", armor->GetName(), who, a_revealing ? "revealing" : "covering"));
			return;
		}
		const auto previous = list;
		if (a_revealing) {
			list.emplace_back(key, armor->GetName());
		} else {
			list.erase(entry);
		}
		if (!settings.Save()) {
			list = previous;
			Report("Could not save OStrands_User.ini; see OStrands.log.");
			return;
		}
		Report(std::format("{} ({}) now counts as {}.", armor->GetName(), who, a_revealing ? "revealing" : "covering"));
	}

	std::string Status()
	{
		std::scoped_lock guard(statusLock);
		return status;
	}
}
