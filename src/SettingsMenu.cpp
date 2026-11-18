#include "SettingsMenu.h"

#include "Arousal.h"
#include "Body.h"
#include "FluidManager.h"
#include "OStimBridge.h"
#include "Revealing.h"
#include "Settings.h"

#include "SKSEMenuFramework.h"

#include <cmath>

namespace OStrands::SettingsMenu
{
	namespace
	{
		namespace UI = ImGuiMCP;

		using Condition = std::function<bool()>;

		void Hint(const char* a_text)
		{
			if (a_text && *a_text && UI::IsItemHovered(UI::ImGuiHoveredFlags_AllowWhenDisabled)) {
				UI::SetTooltip("%s", a_text);
			}
		}

		void Persist()
		{
			auto guard = Settings::Lock();
			Settings::Get().Save();
		}

		struct Option
		{
			Option(const char* a_name, const char* a_tooltip, Condition a_condition) :
				name(a_name), tooltip(a_tooltip), condition(std::move(a_condition))
			{}

			virtual ~Option() = default;

			void Draw()
			{
				bool enabled = true;
				if (condition) {
					auto guard = Settings::Lock();
					enabled = condition();
				}
				UI::BeginDisabled(!enabled);
				Body();
				UI::EndDisabled();
			}

			virtual void Body() = 0;

			const char* name;
			const char* tooltip;
			Condition   condition;
		};

		struct Heading : Option
		{
			explicit Heading(const char* a_name) :
				Option(a_name, nullptr, {})
			{}

			void Body() override { UI::SeparatorText(name); }
		};

		struct Note : Option
		{
			explicit Note(std::function<std::string()> a_text) :
				Option("", nullptr, {}), text(std::move(a_text))
			{}

			void Body() override { UI::TextWrapped("%s", text().c_str()); }

			std::function<std::string()> text;
		};

		struct Toggle : Option
		{
			Toggle(const char* a_name, const char* a_tooltip, bool Settings::* a_field, Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), field(a_field)
			{}

			void Body() override
			{
				bool value = false;
				{
					auto guard = Settings::Lock();
					value = Settings::Get().*field;
				}
				if (UI::Checkbox(name, &value)) {
					auto       guard = Settings::Lock();
					auto&      settings = Settings::Get();
					const bool previous = settings.*field;
					settings.*field = value;
					if (!settings.Save()) {
						settings.*field = previous;
					}
				}
				Hint(tooltip);
			}

			bool Settings::* field;
		};

		template <class T>
		struct Slider : Option
		{
			Slider(const char* a_name, const char* a_tooltip, T Settings::* a_field, float a_min, float a_max, float a_step,
				Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), field(a_field), min(a_min), max(a_max), step(a_step)
			{
				const int digits = a_step >= 1.0f ? 0 : a_step >= 0.1f ? 1 : a_step >= 0.01f ? 2 : 3;
				format = std::format("%.{}f", digits);
			}

			void Body() override
			{
				float value = 0.0f;
				{
					auto guard = Settings::Lock();
					value = static_cast<float>(Settings::Get().*field);
				}
				if (UI::SliderFloat(name, &value, min, max, format.c_str()) && std::isfinite(value)) {
					value = std::clamp(std::round(value / step) * step, min, max);
					auto  guard = Settings::Lock();
					auto& settings = Settings::Get();
					if constexpr (std::is_integral_v<T>) {
						settings.*field = static_cast<T>(std::lround(value));
					} else {
						settings.*field = static_cast<T>(value);
					}
					settings.Validate();
				}
				if (UI::IsItemDeactivatedAfterEdit()) {
					Persist();
				}
				Hint(tooltip);
			}

			T Settings::* field;
			float         min;
			float         max;
			float         step;
			std::string   format;
		};

		struct Choice : Option
		{
			Choice(const char* a_name, const char* a_tooltip, int Settings::* a_field, std::vector<std::pair<const char*, int>> a_items,
				Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), field(a_field), items(std::move(a_items))
			{
				for (const auto& item : items) {
					labels.push_back(item.first);
				}
			}

			void Body() override
			{
				int value = 0;
				{
					auto guard = Settings::Lock();
					value = Settings::Get().*field;
				}
				int index = 0;
				for (std::size_t i = 0; i < items.size(); ++i) {
					if (items[i].second == value) {
						index = static_cast<int>(i);
					}
				}
				if (UI::Combo(name, &index, labels.data(), static_cast<int>(labels.size())) && index >= 0 &&
					static_cast<std::size_t>(index) < items.size()) {
					auto      guard = Settings::Lock();
					auto&     settings = Settings::Get();
					const int previous = settings.*field;
					settings.*field = items[index].second;
					settings.Validate();
					if (!settings.Save()) {
						settings.*field = previous;
					}
				}
				Hint(tooltip);
			}

			int Settings::*                            field;
			std::vector<std::pair<const char*, int>>   items;
			std::vector<const char*>                   labels;
		};

		struct Key : Option
		{
			static constexpr int kEscape = 0x01;
			static constexpr int kBackspace = 0x0E;
			static constexpr int kDelete = 0xD3;

			Key(const char* a_name, const char* a_tooltip, int Settings::* a_field, Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), field(a_field)
			{}

			static const std::uint8_t* State()
			{
				auto* input = RE::BSInputDeviceManager::GetSingleton();
				auto* keyboard = input ? input->GetKeyboard() : nullptr;
				return keyboard ? keyboard->GetRuntimeData().curState : nullptr;
			}

			const std::string& NameOf(int a_code)
			{
				if (a_code != named) {
					named = a_code;
					RE::BSFixedString text;
					auto*             input = RE::BSInputDeviceManager::GetSingleton();
					if (a_code <= 0) {
						label = "Unbound";
					} else if (input && input->GetButtonNameFromID(RE::INPUT_DEVICE::kKeyboard, a_code, text) && !text.empty()) {
						label = text.c_str();
					} else {
						label = std::format("Key {}", a_code);
					}
				}
				return label;
			}

			int Pressed()
			{
				const auto* state = State();
				if (!state) {
					return -1;
				}
				for (std::size_t code = 1; code < held.size(); ++code) {
					const bool down = (state[code] & 0x80) != 0;
					if (down && !held[code]) {
						return static_cast<int>(code);
					}
					held[code] = held[code] && down;
				}
				return -1;
			}

			void Listen()
			{
				const auto* state = State();
				for (std::size_t code = 0; code < held.size(); ++code) {
					held[code] = state && (state[code] & 0x80) != 0;
				}
				listening = true;
			}

			void Body() override
			{
				int value = 0;
				{
					auto guard = Settings::Lock();
					value = Settings::Get().*field;
				}
				if (listening) {
					if (const int code = Pressed(); code > 0) {
						listening = false;
						if (code != kEscape) {
							auto  guard = Settings::Lock();
							auto& settings = Settings::Get();
							settings.*field = code == kBackspace || code == kDelete ? 0 : code;
							settings.Validate();
							settings.Save();
							value = settings.*field;
						}
					}
				}
				const std::string text = listening ? std::format("Press a key... (Esc to cancel, Delete to unbind)###{}", name) :
				                                     std::format("{}: {}###{}", name, NameOf(value), name);
				if (UI::Button(text.c_str())) {
					if (listening) {
						listening = false;
					} else {
						Listen();
					}
				}
				Hint(tooltip);
			}

			int Settings::*        field;
			bool                   listening{ false };
			std::array<bool, 256>  held{};
			int                    named{ -1 };
			std::string            label;
		};

		struct Button : Option
		{
			Button(const char* a_name, const char* a_tooltip, std::function<void()> a_action, Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), action(std::move(a_action))
			{}

			void Body() override
			{
				if (UI::Button(name)) {
					action();
				}
				Hint(tooltip);
			}

			std::function<void()> action;
		};

		struct FhuClothingRules : Option
		{
			FhuClothingRules() : Option("Equipment Exceptions", "", [] { return Settings::Get().fhuClothing; }) {}

			void Body() override
			{
				if (!UI::CollapsingHeader(name)) {
					return;
				}
				if (UI::Button("Read Worn Equipment")) {
					SKSE::GetTaskInterface()->AddTask([] { Revealing::ScanFhuEquipment(); });
				}
				Hint("Reads the player by default, or the NPC selected in the console.");
				const auto equipment = Revealing::FhuEquipment();
				if (equipment.empty()) {
					UI::TextUnformatted("Read worn equipment, then select an item to set what it blocks.");
					return;
				}

				const auto current = std::ranges::find(equipment, selected, &std::pair<std::string, std::string>::first);
				if (UI::BeginCombo("Equipment", current == equipment.end() ? "Select equipment" : current->second.c_str())) {
					for (const auto& [id, label] : equipment) {
						UI::PushID(id.c_str());
						if (UI::Selectable(label.c_str(), id == selected)) {
							selected = id;
						}
						UI::PopID();
					}
					UI::EndCombo();
				}
				if (std::ranges::find(equipment, selected, &std::pair<std::string, std::string>::first) == equipment.end()) {
					return;
				}
				auto guard = Settings::Lock();
				auto& settings = Settings::Get();
				auto& rules = settings.fhuClothingRules;
				const auto entry = std::ranges::find(rules, selected, &std::pair<std::string, int>::first);
				const bool customized = entry != rules.end();
				if (!customized && UI::Button("Mark as Non-Blocking and Edit")) {
					rules.emplace_back(selected, 0);
					if (!settings.Save()) {
						rules.pop_back();
					}
				}
				if (!customized) {
					UI::TextUnformatted("Uses automatic detection. Mark a bra as non-blocking if needed.");
					return;
				}
				int mask = entry->second;
				constexpr std::array labels{ "Block Vaginal Flow", "Block Anal Flow", "Block Oral Flow" };
				for (std::size_t i = 0; i < labels.size(); ++i) {
					const int bit = 1 << i;
					bool blocked = (mask & bit) != 0;
					if (UI::Checkbox(labels[i], &blocked)) {
						mask = blocked ? mask | bit : mask & ~bit;
					}
				}
				if (mask != entry->second) {
					const int previous = entry->second;
					entry->second = mask;
					if (!settings.Save()) {
						entry->second = previous;
					}
				}
				if (UI::Button("Restore Automatic Detection")) {
					rules.erase(entry);
					if (!settings.Save()) {
						rules.emplace_back(selected, mask);
					}
				}
			}

			std::string selected;
		};

		using Page = std::vector<std::unique_ptr<Option>>;

		Page strands;
		Page semen;
		Page fhu;
		Page arousal;
		Page alignment;

		void Draw(Page& a_page)
		{
			for (auto& option : a_page) {
				option->Draw();
			}
		}

		void __stdcall DrawStrands() { Draw(strands); }
		void __stdcall DrawSemen() { Draw(semen); }
		void __stdcall DrawFhu() { Draw(fhu); }
		void __stdcall DrawArousal() { Draw(arousal); }
		void __stdcall DrawAlignment() { Draw(alignment); }

		void BuildStrands()
		{
			auto&      page = strands;
			const auto on = [] { return Settings::Get().enabled; };
			const auto drips = [] { return Settings::Get().enabled && Settings::Get().drips; };
			const auto glints = [] { return Settings::Get().enabled && Settings::Get().glints; };
			const auto collision = [] { return Settings::Get().enabled && Settings::Get().collision; };

			page.push_back(std::make_unique<Heading>("General"));
			page.push_back(std::make_unique<Toggle>("Enabled", "Show strands, drips, and semen in OStim and SexLab scenes.", &Settings::enabled));
			page.push_back(std::make_unique<Toggle>("Debug Logging", "Write detailed scene and strand information to OStrands.log.", &Settings::debugLog));
			page.push_back(std::make_unique<Toggle>("Creature Support",
				"Enable creatures in SexLab and OStim scenes and arousal drips. Uses the bones listed under [CreatureRigs] in OStrands.ini. Takes effect at the next stage.",
				&Settings::creatures, on));

			page.push_back(std::make_unique<Heading>("Strand Types"));
			page.push_back(std::make_unique<Toggle>("Crotch Strands", "Form strands between partners as their bodies separate after a thrust.", &Settings::crotch, on));
			page.push_back(std::make_unique<Toggle>("Tribbing Strands", "Form strands during vulva-to-vulva contact. Takes effect in the next scene.", &Settings::tribbing, on));
			page.push_back(std::make_unique<Toggle>("Oral Strands", "Form strands during oral sex, cunnilingus, and anilingus. Takes effect in the next scene.",
				&Settings::oral, on));
			page.push_back(std::make_unique<Toggle>("Finger Strands", "Form strands during fingering or rubbing. Takes effect in the next scene.",
				&Settings::fingering, on));
			page.push_back(std::make_unique<Toggle>("Toy Strands", "Form strands when using toys such as strap-ons. Takes effect in the next scene.", &Settings::toys, on));

			page.push_back(std::make_unique<Heading>("Spawning"));
			page.push_back(std::make_unique<Slider<float>>("Penetration Strand Rate", "Maximum strands formed between the opening and penis per second.",
				&Settings::bridgeRate, Settings::kRateMin, Settings::kRateMax, 0.1f, on));
			page.push_back(std::make_unique<Slider<float>>("Crotch Strand Rate", "Maximum strands formed between bodies per second.", &Settings::crotchRate,
				Settings::kRateMin, Settings::kRateMax, 0.1f, on));
			page.push_back(std::make_unique<Slider<int>>("Maximum Strands", "Maximum number of visible strands at once.", &Settings::maxTotal,
				static_cast<float>(Settings::kCountMin), static_cast<float>(Settings::kCountMax), 1.0f, on));

			page.push_back(std::make_unique<Heading>("Drips"));
			page.push_back(std::make_unique<Toggle>("Drips", "Enable vaginal drips during and after scenes.", &Settings::drips, on));
			page.push_back(std::make_unique<Toggle>("Strap-On Wearer Drips", "Also allow women wearing strap-ons to drip.", &Settings::wearerDrips, drips));
			page.push_back(std::make_unique<Slider<float>>("Drip Interval", "Average time between drips, in seconds.", &Settings::dripInterval,
				Settings::kIntervalMin, Settings::kIntervalMax, 0.1f, drips));
			page.push_back(std::make_unique<Slider<float>>("Post-Scene Drip Duration", "Seconds that dripping continues after a scene, with decreasing frequency. Set to 0 to disable.",
				&Settings::dripLinger, Settings::kLingerMin, Settings::kLingerMax, 1.0f, drips));

			page.push_back(std::make_unique<Heading>("Appearance"));
			page.push_back(std::make_unique<Slider<float>>("Transparency", "Strand transparency. 0 is fully opaque.", &Settings::transparency,
				Settings::kTransparencyMin, Settings::kTransparencyMax, 0.05f, on));
			page.push_back(std::make_unique<Slider<float>>("Strand Lighting Compensation", "Brightens strands in dark areas. 0 disables compensation and 100 is brightest.",
				&Settings::strandLightingCompensation, Settings::kLightingCompensationMin, Settings::kLightingCompensationMax, 1.0f, on));
			page.push_back(std::make_unique<Slider<float>>("Specular Intensity", "Brightness of wet highlights on strands.", &Settings::shine,
				Settings::kShineMin, Settings::kShineMax, 0.5f, on));
			page.push_back(std::make_unique<Slider<float>>("Gloss", "Higher values produce smaller, sharper highlights.",
				&Settings::gloss, Settings::kGlossMin, Settings::kGlossMax, 10.0f, on));
			page.push_back(std::make_unique<Toggle>("Glints", "Small flashes of light on strands, like sunlight on water.",
				&Settings::glints, on));
			page.push_back(std::make_unique<Slider<float>>("Glint Rate", "Glints per strand per second.", &Settings::glintRate,
				Settings::kGlintRateMin, Settings::kGlintRateMax, 0.1f, glints));
			page.push_back(std::make_unique<Slider<float>>("Glint Size", "Size of each glint.", &Settings::glintSize, Settings::kGlintSizeMin,
				Settings::kGlintSizeMax, 0.05f, glints));
			page.push_back(std::make_unique<Slider<float>>("Glint Brightness", "Brightness of each glint.", &Settings::glintBrightness,
				Settings::kGlintBrightnessMin, Settings::kGlintBrightnessMax, 0.1f, glints));

			page.push_back(std::make_unique<Heading>("Collision"));
			page.push_back(std::make_unique<Toggle>("Body Collision", "Keep strands, drips, and semen on the body surface.",
				&Settings::collision, on));
			page.push_back(std::make_unique<Slider<float>>("Body Collision Thickness",
				"Adjusts the collision surface. Lower it if fluid floats above the skin, or raise it if fluid sinks into the skin.", &Settings::collisionThickness,
				Settings::kThicknessMin, Settings::kThicknessMax, 0.05f, collision));
			page.push_back(std::make_unique<Toggle>("Show Collision Shapes",
				"Show collision markers on the console-selected actor, scene actors, or player. Resets on a new game.",
				&Settings::showShapes, on));
			page.push_back(std::make_unique<Button>("Refit Collision Shapes",
				"Recalculate collision after changing body meshes or proportions. Debug logging records the chosen mesh, sample count, and fallback radius.",
				[] { Body::RequestRefit(); }));

			page.push_back(std::make_unique<Heading>("Configuration"));
			page.push_back(std::make_unique<Note>([] {
				return std::string("Changes are saved in Data/SKSE/Plugins/OStrands_User.ini. Keep it when updating the default OStrands.ini.");
			}));
			page.push_back(std::make_unique<Button>("Reload Configuration", "Reload OStrands.ini and OStrands_User.ini after editing them.", [] {
				if (Settings::Get().Load()) {
					OStimBridge::ReloadActions();
				}
			}));
		}

		void BuildSemen()
		{
			auto&      page = semen;
			const auto on = [] { return Settings::Get().enabled; };
			const auto active = [] { return Settings::Get().enabled && FluidManager::Get().SemenEnabled(); };

			page.push_back(std::make_unique<Choice>("Semen", "Auto enables semen in SexLab scenes and in OStim scenes when OCum is installed.", &Settings::semen,
				std::vector<std::pair<const char*, int>>{ { "Auto", -1 }, { "On", 1 }, { "Off", 0 } }, on));
			page.push_back(std::make_unique<Note>([] {
				auto       guard = Settings::Lock();
				const bool ocum = FluidManager::Get().OCumInstalled();
				const bool sexlab = FluidManager::Get().SexLabInstalled();
				const int  mode = Settings::Get().semen;
				if (mode > 0) {
					return std::string("Semen enabled");
				}
				if (mode == 0) {
					return std::string("Semen disabled");
				}
				if (ocum) {
					return std::string("Auto: OCum detected");
				}
				return std::string(sexlab ? "Auto: OCum absent. SexLab scenes enabled, OStim scenes disabled." : "Auto: OCum absent. Disabled.");
			}));

			page.push_back(std::make_unique<Toggle>("Continuous Scene Strands", "Use continuous strands for OStrands scene fluid too. Off by default.", &Settings::sceneContinuous));
			page.push_back(std::make_unique<Heading>("Ejaculation"));
			page.push_back(std::make_unique<Slider<float>>("Ejaculation Amount", "Total amount per ejaculation. OCum can adjust it in scenes involving the player.", &Settings::semenAmount,
				Settings::kAmountMin, Settings::kAmountMax, 0.05f, active));
			page.push_back(std::make_unique<Slider<float>>("Jet Strength", "Strength of each jet.", &Settings::semenStrength, Settings::kStrengthMin,
				Settings::kStrengthMax, 0.05f, active));
			page.push_back(std::make_unique<Slider<int>>("Number of Pulses", "Number of pulses per climax before the final dribble.", &Settings::semenPulses,
				static_cast<float>(Settings::kPulsesMin), static_cast<float>(Settings::kPulsesMax), 1.0f, active));
			page.push_back(std::make_unique<Slider<float>>("Penetration Overflow",
				"Fraction of each pulse that escapes around the penis during penetration. The remainder comes out after withdrawal.", &Settings::semenOverflow,
				Settings::kOverflowMin, Settings::kOverflowMax, 0.05f, active));

			page.push_back(std::make_unique<Heading>("Fluid"));
			page.push_back(std::make_unique<Slider<float>>("Viscosity", "Higher values slow the flow and keep streams together longer.",
				&Settings::semenViscosity, Settings::kViscosityMin, Settings::kViscosityMax, 1.0f, active));
			page.push_back(std::make_unique<Slider<float>>("Cohesion", "How strongly droplets clump together.", &Settings::semenCohesion,
				Settings::kCohesionMin, Settings::kCohesionMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<float>>("Stretch", "How far strands stretch before breaking. Higher values produce longer, thinner strands.",
				&Settings::semenStretch, Settings::kStretchMin, Settings::kStretchMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<float>>("Skin Adhesion", "How strongly semen sticks to skin. Lower values let it run off more easily.",
				&Settings::semenStickiness, Settings::kStickinessMin, Settings::kStickinessMax, 0.1f, active));
			page.push_back(std::make_unique<Slider<float>>("Opacity", "1 produces opaque white.", &Settings::semenOpacity, Settings::kOpacityMin,
				Settings::kOpacityMax, 0.05f, active));

			page.push_back(std::make_unique<Slider<float>>("Semen Lighting Compensation", "Brightens drops and streams in dark areas. 0 disables compensation and 100 is brightest.",
				&Settings::semenLightingCompensation, Settings::kLightingCompensationMin, Settings::kLightingCompensationMax, 1.0f, active));

			page.push_back(std::make_unique<Heading>("Lifetime"));
			page.push_back(std::make_unique<Slider<float>>("Lifetime", "Seconds before semen starts fading.", &Settings::fluidLifetime,
				Settings::kLifetimeMin, Settings::kLifetimeMax, 5.0f, active));
			page.push_back(std::make_unique<Slider<float>>("Fade Time", "Seconds for semen to shrink and disappear after its lifetime ends.", &Settings::fluidFade,
				Settings::kFadeMin, Settings::kFadeMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<int>>("Maximum Particles", "Maximum particles per fluid region. Lower values reduce simulation load.", &Settings::fluidMaxParticles,
				static_cast<float>(Settings::kParticlesMin), static_cast<float>(Settings::kParticlesMax), 32.0f, active));

			page.push_back(std::make_unique<Heading>("OCum and Testing"));
			page.push_back(std::make_unique<Slider<float>>("OCum Timeout",
				"Seconds to wait for OCum in a player scene before using the default amount.", &Settings::ocumWait, Settings::kWaitMin,
				Settings::kWaitMax, 0.1f, active));
			page.push_back(std::make_unique<Key>("Test Hotkey",
				"Click and press a key. In an OStim scene involving the player, it triggers climax for actors with penises. Leave unbound during normal play.",
				&Settings::fluidTestKey, active));
		}

		void BuildFhu()
		{
			fhu.push_back(std::make_unique<Toggle>("Show Round Droplets", "Show round droplets and puddles alongside strands. Off by default.", &Settings::fhuRoundDrops));
			fhu.push_back(std::make_unique<Toggle>("Randomize Flow", "Vary the flow rate over time during active release and passive leakage.", &Settings::fhuRandomRate));
			fhu.push_back(std::make_unique<Toggle>("Clothing Blocks Flow", "Check body and pelvis slots automatically. Equipment exceptions take priority. Applies to active release and passive leakage.", &Settings::fhuClothing));
			fhu.push_back(std::make_unique<FhuClothingRules>());
			fhu.push_back(std::make_unique<Heading>("Active Release"));
			fhu.push_back(std::make_unique<Toggle>("Vaginal Release", "Spawn vaginal strands during the release animation.", &Settings::fhuReleaseVaginal));
			fhu.push_back(std::make_unique<Toggle>("Anal Release", "Spawn anal strands during the release animation.", &Settings::fhuReleaseAnal));
			fhu.push_back(std::make_unique<Toggle>("Oral Release", "Spawn oral strands during the release animation.", &Settings::fhuReleaseOral));
			fhu.push_back(std::make_unique<Heading>("Passive Leakage"));
			fhu.push_back(std::make_unique<Toggle>("Vaginal Leakage", "Allow slow vaginal leakage from stored FHU fluid outside scenes.", &Settings::fhuSeepVaginal));
			fhu.push_back(std::make_unique<Toggle>("Anal Leakage", "Allow slow anal leakage from stored FHU fluid outside scenes.", &Settings::fhuSeepAnal));
			fhu.push_back(std::make_unique<Toggle>("Oral Leakage", "Allow slow oral leakage from stored FHU fluid outside scenes.", &Settings::fhuSeepOral));
			fhu.push_back(std::make_unique<Slider<float>>("Leakage Rate", "Rate of passive leakage from stored FHU fluid outside scenes. 0 disables it.", &Settings::fhuSeepRate,
				Settings::kRateMin, Settings::kRateMax, 0.1f));

			fhu.push_back(std::make_unique<Heading>("Release Flow"));
			fhu.push_back(std::make_unique<Slider<float>>("Release Amount", "Controls strand frequency and droplet amount during active release. 0 disables active release.", &Settings::fhuAmount,
				Settings::kFhuAmountMin, Settings::kFhuAmountMax, 0.05f));
			fhu.push_back(std::make_unique<Heading>("Round Droplet Settings"));
			fhu.push_back(std::make_unique<Slider<float>>("Release Strength", "Controls how fast fluid leaves the body.", &Settings::fhuStrength,
				Settings::kFhuStrengthMin, Settings::kFhuStrengthMax, 0.1f));
			fhu.push_back(std::make_unique<Slider<float>>("Viscosity", "Higher values slow the flow and help streams stay together.", &Settings::fhuViscosity,
				Settings::kViscosityMin, Settings::kViscosityMax, 1.0f));
			fhu.push_back(std::make_unique<Slider<float>>("Cohesion", "Controls how strongly droplets clump together.", &Settings::fhuCohesion,
				Settings::kCohesionMin, Settings::kCohesionMax, 0.5f));
			fhu.push_back(std::make_unique<Slider<float>>("Stretch", "Controls how far clumped droplets stretch before breaking apart.", &Settings::fhuStretch,
				Settings::kStretchMin, Settings::kStretchMax, 0.5f));
			fhu.push_back(std::make_unique<Slider<float>>("Skin Adhesion", "Lower values help fluid run off the skin.", &Settings::fhuStickiness,
				Settings::kStickinessMin, Settings::kStickinessMax, 0.1f));
			fhu.push_back(std::make_unique<Slider<float>>("Opacity", "1 is fully opaque.", &Settings::fhuOpacity,
				Settings::kOpacityMin, Settings::kOpacityMax, 0.05f));
			fhu.push_back(std::make_unique<Slider<float>>("Lighting Compensation", "Controls fluid brightness in dark areas.", &Settings::fhuLightingCompensation,
				Settings::kLightingCompensationMin, Settings::kLightingCompensationMax, 1.0f));

			fhu.push_back(std::make_unique<Heading>("Lifetime"));
			fhu.push_back(std::make_unique<Slider<float>>("Lifetime", "Seconds before fluid starts fading.", &Settings::fhuLifetime,
				Settings::kLifetimeMin, Settings::kLifetimeMax, 5.0f));
			fhu.push_back(std::make_unique<Slider<float>>("Fade Time", "Seconds from the start of fading until fluid disappears.", &Settings::fhuFadeTime,
				Settings::kFadeMin, Settings::kFadeMax, 0.5f));
		}

		void BuildArousal()
		{
			auto&      page = arousal;
			const auto found = [] { return Settings::Get().enabled && Arousal::Installed(); };
			const auto active = [] { return Settings::Get().enabled && Arousal::Installed() && Settings::Get().arousal; };

			page.push_back(std::make_unique<Note>([] {
				return std::string(Arousal::Live()      ? "SLO Aroused NG detected. Arousal is read live from its plugin." :
				                   Arousal::Installed() ? "An arousal mod was detected." :
				                                          "No arousal mod detected. Requires SLO Aroused NG, OSL Aroused, or another mod using the SexLab Aroused faction.");
			}));
			page.push_back(std::make_unique<Toggle>("Arousal Drips", "Allow nearby naked actors to drip from arousal outside scenes.", &Settings::arousal, found));
			page.push_back(std::make_unique<Toggle>("Lubrication", "Allow vaginal drips when aroused.", &Settings::arousalWetness, active));
			page.push_back(std::make_unique<Toggle>("Precum", "Allow precum drips from the tip of the penis when aroused.", &Settings::arousalPrecum, active));
			page.push_back(std::make_unique<Slider<float>>("Arousal Threshold", "Arousal required for drips to start (0-100).", &Settings::arousalThreshold,
				Settings::kThresholdMin, Settings::kThresholdMax, 1.0f, active));
			page.push_back(std::make_unique<Slider<float>>("Arousal Drip Interval",
				"Average interval in seconds at full arousal. At the threshold, the interval is four times longer.", &Settings::arousalInterval,
				Settings::kArousalIntervalMin, Settings::kArousalIntervalMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<float>>("Arousal Drip Range", "Maximum distance from the player in game units.", &Settings::arousalRadius,
				Settings::kReachMin, Settings::kReachMax, 128.0f, active));

			page.push_back(std::make_unique<Heading>("Revealing Armor"));
			page.push_back(std::make_unique<Note>([] {
				return std::string("Body armor blocks drips unless marked below or tagged with OStrands_Revealing.");
			}));
			page.push_back(std::make_unique<Button>("Mark Worn Armor as Revealing",
				"Uses the actor selected in the console, or the player. Anyone wearing this armor can drip through it.",
				[] { SKSE::GetTaskInterface()->AddTask([] { Revealing::Mark(true); }); }));
			page.push_back(std::make_unique<Button>("Unmark Worn Armor",
				"Uses the actor selected in the console, or the player. Unmarked armor blocks drips again.",
				[] { SKSE::GetTaskInterface()->AddTask([] { Revealing::Mark(false); }); }));
			page.push_back(std::make_unique<Key>("Toggle Revealing Armor Hotkey",
				"Click and press a key to toggle the mark on the selected actor's worn armor. Defaults to the player.",
				&Settings::revealingKey));
			page.push_back(std::make_unique<Note>([] {
				std::size_t marked = 0;
				{
					auto guard = Settings::Lock();
					marked = Settings::Get().revealingArmor.size();
				}
				const auto status = Revealing::Status();
				return std::format("{} armor items marked. {}{}", marked, status.empty() ? "" : " ", status);
			}));
		}
	}

	namespace
	{
		void BuildAlignment()
		{
			auto&      page = alignment;
			const auto on = [] { return Settings::Get().enabled; };
			const auto slider = [&](const char* a_name, const char* a_tooltip, float Settings::* a_field) {
				page.push_back(std::make_unique<Slider<float>>(a_name, a_tooltip, a_field, Settings::kAlignMin, Settings::kAlignMax, 0.1f, on));
			};

			page.push_back(std::make_unique<Note>([] {
				return std::string("Adjust the origins of strands, drips, and semen in game units. Use collision markers to check alignment with the skeleton.");
			}));
			page.push_back(std::make_unique<Toggle>("Show Collision Shapes",
				"Show collision and opening markers on the console-selected actor, scene actors, or player.", &Settings::showShapes, on));

			page.push_back(std::make_unique<Heading>("Vagina"));
			slider("Vaginal In/Out Offset", "Positive values move the origin out of the body. Negative values move it inward.", &Settings::vaginaOut);
			slider("Vaginal Vertical Offset", "Positive values move toward the head. Negative values move toward the feet.", &Settings::vaginaUp);

			page.push_back(std::make_unique<Heading>("Anus"));
			slider("Anal In/Out Offset", "Positive values move the origin out of the body. Negative values move it inward.", &Settings::anusOut);
			slider("Anal Vertical Offset", "Positive values move toward the head. Negative values move toward the feet.", &Settings::anusUp);

			page.push_back(std::make_unique<Heading>("Mouth"));
			slider("Oral In/Out Offset", "Positive values move toward the front of the face. Negative values move into the mouth.", &Settings::mouthOut);
			slider("Oral Vertical Offset", "Positive values move toward the nose. Negative values move toward the chin.", &Settings::mouthUp);

			page.push_back(std::make_unique<Button>("Reset Offsets", "Reset all offsets on this page to 0.", [] {
				auto  guard = Settings::Lock();
				auto& settings = Settings::Get();
				for (auto field : { &Settings::vaginaOut, &Settings::vaginaUp, &Settings::anusOut, &Settings::anusUp, &Settings::mouthOut, &Settings::mouthUp }) {
					settings.*field = 0.0f;
				}
				settings.Save();
			}));
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule()) {
			logger::info("SKSE Menu Framework not found; change settings in OStrands_User.ini");
			return;
		}
		if (strands.empty()) {
			BuildStrands();
			BuildSemen();
			BuildFhu();
			BuildArousal();
			BuildAlignment();
		}
		SKSEMenuFramework::SetSection("OStrands");
		SKSEMenuFramework::AddSectionItem("Strands", DrawStrands);
		SKSEMenuFramework::AddSectionItem("Semen", DrawSemen);
		SKSEMenuFramework::AddSectionItem("Fill Her Up Release", DrawFhu);
		SKSEMenuFramework::AddSectionItem("Arousal", DrawArousal);
		SKSEMenuFramework::AddSectionItem("Alignment", DrawAlignment);
		logger::info("settings added to SKSE Menu Framework (API {})", SKSEMenuFramework::GetMenuFrameworkAPIVersion());
	}
}
