#include "SettingsMenu.h"
#include "LightingAddon.h"

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
						label = "未绑定";
					} else if (input && input->GetButtonNameFromID(RE::INPUT_DEVICE::kKeyboard, a_code, text) && !text.empty()) {
						label = text.c_str();
					} else {
						label = std::format("按键 {}", a_code);
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
				const std::string text = listening ? std::format("请按键…（Esc 取消，Delete 解绑）###{}", name) :
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
			FhuClothingRules() : Option("装备遮挡例外", "", [] { return Settings::Get().fhuClothing; }) {}

			void Body() override
			{
				if (!UI::CollapsingHeader(name)) {
					return;
				}
				if (UI::Button("读取当前穿戴装备")) {
					SKSE::GetTaskInterface()->AddTask([] { Revealing::ScanFhuEquipment(); });
				}
				Hint("默认读取玩家。已在控制台选中 NPC 时读取该角色。");
				const auto equipment = Revealing::FhuEquipment();
				if (equipment.empty()) {
					UI::TextUnformatted("读取后可选中一件装备并设置遮挡部位。");
					return;
				}

				const auto current = std::ranges::find(equipment, selected, &std::pair<std::string, std::string>::first);
				if (UI::BeginCombo("装备", current == equipment.end() ? "选择装备" : current->second.c_str())) {
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
				if (!customized && UI::Button("设为不遮挡并编辑")) {
					rules.emplace_back(selected, 0);
					if (!settings.Save()) {
						rules.pop_back();
					}
				}
				if (!customized) {
					UI::TextUnformatted("当前使用自动判断。胸罩可设为不遮挡。");
					return;
				}
				int mask = entry->second;
				constexpr std::array labels{ "遮挡阴道", "遮挡肛门", "遮挡嘴部" };
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
				if (UI::Button("恢复自动判断")) {
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

			page.push_back(std::make_unique<Heading>("常规"));
			page.push_back(std::make_unique<Toggle>("启用", "在 OStim 和 SexLab 场景中显示液丝、滴液和精液。", &Settings::enabled));
			page.push_back(std::make_unique<Toggle>("调试日志", "将场景与液丝的详细信息写入 OStrands.log。", &Settings::debugLog));
			page.push_back(std::make_unique<Toggle>("生物支持",
				"在 SexLab 和 OStim 场景及唤起滴液中启用生物支持。插件根据 OStrands.ini 中 [CreatureRigs] 列出的骨骼定位。下一阶段生效。",
				&Settings::creatures, on));

			page.push_back(std::make_unique<Heading>("液丝类型"));
			page.push_back(std::make_unique<Toggle>("胯部液丝", "抽动后身体分开时，在两人之间形成液丝。", &Settings::crotch, on));
			page.push_back(std::make_unique<Toggle>("磨镜液丝", "两人外阴接触时形成液丝。下个场景生效。", &Settings::tribbing, on));
			page.push_back(std::make_unique<Toggle>("口部液丝", "口交、舔阴和舔肛时形成液丝。下个场景生效。",
				&Settings::oral, on));
			page.push_back(std::make_unique<Toggle>("手指液丝", "用手指刺激或揉搓时形成液丝。下个场景生效。",
				&Settings::fingering, on));
			page.push_back(std::make_unique<Toggle>("玩具液丝", "使用假阴茎等玩具时形成液丝。下个场景生效。", &Settings::toys, on));

			page.push_back(std::make_unique<Heading>("生成设置"));
			page.push_back(std::make_unique<Slider<float>>("插入部位生成速率", "每秒在开口与阴茎之间生成的液丝上限。",
				&Settings::bridgeRate, Settings::kRateMin, Settings::kRateMax, 0.1f, on));
			page.push_back(std::make_unique<Slider<float>>("胯部生成速率", "每秒在两人身体之间生成的液丝上限。", &Settings::crotchRate,
				Settings::kRateMin, Settings::kRateMax, 0.1f, on));
			page.push_back(std::make_unique<Slider<int>>("液丝数量上限", "屏幕上同时显示的液丝数量上限。", &Settings::maxTotal,
				static_cast<float>(Settings::kCountMin), static_cast<float>(Settings::kCountMax), 1.0f, on));

			page.push_back(std::make_unique<Heading>("滴液"));
			page.push_back(std::make_unique<Toggle>("滴液", "场景中及结束后从阴道自然滴液。", &Settings::drips, on));
			page.push_back(std::make_unique<Toggle>("佩戴者滴液", "佩戴穿戴式假阴茎的女性也会滴液。", &Settings::wearerDrips, drips));
			page.push_back(std::make_unique<Slider<float>>("滴液间隔", "两次滴液的平均间隔（秒）。", &Settings::dripInterval,
				Settings::kIntervalMin, Settings::kIntervalMax, 0.1f, drips));
			page.push_back(std::make_unique<Slider<float>>("滴液持续时间", "场景结束后继续滴液的秒数，频率逐渐降低。设为 0 可关闭。",
				&Settings::dripLinger, Settings::kLingerMin, Settings::kLingerMax, 1.0f, drips));

			page.push_back(std::make_unique<Heading>("外观"));
			page.push_back(std::make_unique<Slider<float>>("透明度", "液丝的透明程度。0 为完全不透明。", &Settings::transparency,
				Settings::kTransparencyMin, Settings::kTransparencyMax, 0.05f, on));

			page.push_back(std::make_unique<Slider<float>>("高光亮度", "液丝湿润高光的亮度。", &Settings::shine,
				Settings::kShineMin, Settings::kShineMax, 0.5f, on));
			page.push_back(std::make_unique<Slider<float>>("高光锐度", "数值越高，高光越小、越锐利，越接近液体反光。",
				&Settings::gloss, Settings::kGlossMin, Settings::kGlossMax, 10.0f, on));
			page.push_back(std::make_unique<Toggle>("星芒", "液丝上闪烁的小星芒，类似阳光在水面的反光。",
				&Settings::glints, on));
			page.push_back(std::make_unique<Slider<float>>("星芒频率", "每条液丝每秒闪烁的次数。", &Settings::glintRate,
				Settings::kGlintRateMin, Settings::kGlintRateMax, 0.1f, glints));
			page.push_back(std::make_unique<Slider<float>>("星芒大小", "单个星芒的大小。", &Settings::glintSize, Settings::kGlintSizeMin,
				Settings::kGlintSizeMax, 0.05f, glints));
			page.push_back(std::make_unique<Slider<float>>("星芒亮度", "单个星芒闪烁时的亮度。", &Settings::glintBrightness,
				Settings::kGlintBrightnessMin, Settings::kGlintBrightnessMax, 0.1f, glints));

			page.push_back(std::make_unique<Heading>("碰撞"));
			page.push_back(std::make_unique<Toggle>("身体碰撞", "让液丝、滴液和精液停留在体表，避免穿过身体。",
				&Settings::collision, on));
			page.push_back(std::make_unique<Slider<float>>("身体碰撞厚度",
				"调整身体碰撞体大小。液体浮在皮肤上方时调低，陷入皮肤时调高。", &Settings::collisionThickness,
				Settings::kThicknessMin, Settings::kThicknessMax, 0.05f, collision));
			page.push_back(std::make_unique<Toggle>("显示碰撞体",
				"在控制台选中的角色、场景角色或玩家身上显示碰撞点，用于捏脸后的调试。开始新游戏时会自动关闭。正常游玩时请关闭。",
				&Settings::showShapes, on));
			page.push_back(std::make_unique<Button>("重新拟合碰撞体",
				"更换身体模型或调整体型后，重新测量已加载的模型。启用调试日志后会记录选中的网格、采样数量和备用半径。",
				[] { Body::RequestRefit(); }));

			page.push_back(std::make_unique<Heading>("配置文件"));
			page.push_back(std::make_unique<Note>([] {
				return std::string("改动保存在 Data/SKSE/Plugins/OStrands_User.ini。更新时保留该文件，并替换存放默认值的 OStrands.ini。                                      ");
			}));
			page.push_back(std::make_unique<Button>("重新加载配置", "手动编辑配置后，重新读取 OStrands.ini 和 OStrands_User.ini。", [] {
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

			page.push_back(std::make_unique<Choice>("精液", "自动模式下，SexLab 场景启用精液。装有 OCum 时，OStim 场景也会启用。", &Settings::semen,
				std::vector<std::pair<const char*, int>>{ { "自动", -1 }, { "开启", 1 }, { "关闭", 0 } }, on));
			page.push_back(std::make_unique<Note>([] {
				auto       guard = Settings::Lock();
				const bool ocum = FluidManager::Get().OCumInstalled();
				const bool sexlab = FluidManager::Get().SexLabInstalled();
				const int  mode = Settings::Get().semen;
				if (mode > 0) {
					return std::string("精液开启");
				}
				if (mode == 0) {
					return std::string("精液关闭 ");
				}
				if (ocum) {
					return std::string("自动：有 OCum    ");
				}
				return std::string(sexlab ? "自动：未检测到 OCum，SexLab 场景开启，OStim 场景关闭。" : "自动：未检测到 OCum，已关闭。");
			}));

			page.push_back(std::make_unique<Heading>("射精"));
			page.push_back(std::make_unique<Slider<float>>("射精量", "每次射精的总量。玩家参与的场景中，OCum 会进一步调整此值。", &Settings::semenAmount,
				Settings::kAmountMin, Settings::kAmountMax, 0.05f, active));
			page.push_back(std::make_unique<Slider<float>>("喷射力度", "每股精液喷出的力度。", &Settings::semenStrength, Settings::kStrengthMin,
				Settings::kStrengthMax, 0.05f, active));
			page.push_back(std::make_unique<Slider<int>>("喷射次数", "每次高潮在末尾滴流前的喷射次数。", &Settings::semenPulses,
				static_cast<float>(Settings::kPulsesMin), static_cast<float>(Settings::kPulsesMax), 1.0f, active));
			page.push_back(std::make_unique<Slider<float>>("插入时溢出比例",
				"仍在插入时，每股精液沿阴茎周围溢出的比例。剩余部分会在抽出后流出。", &Settings::semenOverflow,
				Settings::kOverflowMin, Settings::kOverflowMax, 0.05f, active));

			page.push_back(std::make_unique<Heading>("流体"));
			page.push_back(std::make_unique<Slider<float>>("黏稠度", "精液越黏稠，流动越慢。数值越高，液柱越不易散开。",
				&Settings::semenViscosity, Settings::kViscosityMin, Settings::kViscosityMax, 1.0f, active));
			page.push_back(std::make_unique<Slider<float>>("凝聚力", "液滴相互聚成团的强度。", &Settings::semenCohesion,
				Settings::kCohesionMin, Settings::kCohesionMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<float>>("拉伸性", "液丝断开前的拉伸程度。数值越高，液丝越细长。",
				&Settings::semenStretch, Settings::kStretchMin, Settings::kStretchMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<float>>("皮肤附着力", "精液附着在皮肤上的强度。调低后更容易往下流。",
				&Settings::semenStickiness, Settings::kStickinessMin, Settings::kStickinessMax, 0.1f, active));
			page.push_back(std::make_unique<Slider<float>>("不透明度", "1 为不透明白色。", &Settings::semenOpacity, Settings::kOpacityMin,
				Settings::kOpacityMax, 0.05f, active));


			page.push_back(std::make_unique<Heading>("存留时间"));
			page.push_back(std::make_unique<Slider<float>>("存留时间", "精液开始消退前的存留秒数。", &Settings::fluidLifetime,
				Settings::kLifetimeMin, Settings::kLifetimeMax, 5.0f, active));
			page.push_back(std::make_unique<Slider<float>>("消退时间", "存留时间结束后，精液逐渐缩小直至消失所需的秒数。", &Settings::fluidFade,
				Settings::kFadeMin, Settings::kFadeMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<int>>("粒子数量上限", "单处精液粒子的数量上限。调低可减轻性能负担。", &Settings::fluidMaxParticles,
				static_cast<float>(Settings::kParticlesMin), static_cast<float>(Settings::kParticlesMax), 32.0f, active));

			page.push_back(std::make_unique<Heading>("OCum 与测试"));
			page.push_back(std::make_unique<Slider<float>>("OCum 等待时间",
				"玩家场景中等待 OCum 提供射精量的秒数。超时后按默认量喷射。", &Settings::ocumWait, Settings::kWaitMin,
				Settings::kWaitMax, 0.1f, active));
			page.push_back(std::make_unique<Key>("测试热键",
				"点击后按键。玩家参与 OStim 场景时，该键会使场景中所有拥有阴茎的角色高潮。正常游玩时建议留空。",
				&Settings::fluidTestKey, active));
		}

		void BuildFhu()
		{
			fhu.push_back(std::make_unique<Toggle>("显示圆形液滴", "在旧版悬垂液丝之外显示圆滴和积液。关闭后只显示旧版液丝。", &Settings::fhuRoundDrops));
            fhu.push_back(std::make_unique<Toggle>("随机流速", "让主动排出和自然渗出的流速随时间变化。", &Settings::fhuRandomRate));
			fhu.push_back(std::make_unique<Toggle>("衣物遮挡出液", "自动检查身体和骨盆装备槽。装备例外优先。对主动排出和自然渗出均生效。", &Settings::fhuClothing));
			fhu.push_back(std::make_unique<FhuClothingRules>());
			fhu.push_back(std::make_unique<Heading>("主动排出"));
			fhu.push_back(std::make_unique<Toggle>("阴道主动排出", "播放排出动作时从阴道生成液丝。", &Settings::fhuReleaseVaginal));
			fhu.push_back(std::make_unique<Toggle>("肛门主动排出", "播放排出动作时从肛门生成液丝。", &Settings::fhuReleaseAnal));
			fhu.push_back(std::make_unique<Toggle>("嘴部主动排出", "播放排出动作时从嘴部生成液丝。", &Settings::fhuReleaseOral));
			fhu.push_back(std::make_unique<Heading>("自然渗出"));
			fhu.push_back(std::make_unique<Toggle>("无存量也渗出", "开启后，场景外即使 FHU 没有检测到存量，或未安装 FHU，也按下方的部位和渗出速度生成效果。默认关闭。",
				&Settings::fhuSeepWithoutStorage));
			fhu.push_back(std::make_unique<Toggle>("阴道自然渗出", "场景外有存量时从阴道缓慢渗出。", &Settings::fhuSeepVaginal));
			fhu.push_back(std::make_unique<Toggle>("肛门自然渗出", "场景外有存量时从肛门缓慢渗出。", &Settings::fhuSeepAnal));
			fhu.push_back(std::make_unique<Toggle>("嘴部自然渗出", "场景外有存量时从嘴部缓慢渗出。", &Settings::fhuSeepOral));
			fhu.push_back(std::make_unique<Slider<float>>("自然渗出速度", "控制场景外缓慢渗出的速度。0 关闭。", &Settings::fhuSeepRate,
				Settings::kRateMin, Settings::kRateMax, 0.1f));

			fhu.push_back(std::make_unique<Heading>("持续排出"));
			fhu.push_back(std::make_unique<Slider<float>>("出液量", "控制 FHU 动作中的原版流体出液量。0 关闭主动排出。", &Settings::fhuAmount,
				Settings::kFhuAmountMin, Settings::kFhuAmountMax, 0.05f));
			fhu.push_back(std::make_unique<Heading>("流体参数"));
			fhu.push_back(std::make_unique<Slider<float>>("喷出力度", "控制液体离开身体时的速度。", &Settings::fhuStrength,
				Settings::kFhuStrengthMin, Settings::kFhuStrengthMax, 0.1f));
			fhu.push_back(std::make_unique<Slider<float>>("黏稠度", "数值越高，液体流动越慢，液柱越不易散开。", &Settings::fhuViscosity,
				Settings::kViscosityMin, Settings::kViscosityMax, 1.0f));
			fhu.push_back(std::make_unique<Slider<float>>("凝聚力", "控制液滴聚成团的强度。", &Settings::fhuCohesion,
				Settings::kCohesionMin, Settings::kCohesionMax, 0.5f));
			fhu.push_back(std::make_unique<Slider<float>>("拉伸性", "控制液滴聚团后断开前的拉伸程度。", &Settings::fhuStretch,
				Settings::kStretchMin, Settings::kStretchMax, 0.5f));
			fhu.push_back(std::make_unique<Slider<float>>("皮肤附着力", "数值越低，液体越容易从皮肤上流下。", &Settings::fhuStickiness,
				Settings::kStickinessMin, Settings::kStickinessMax, 0.1f));
			fhu.push_back(std::make_unique<Slider<float>>("不透明度", "1 为不透明。", &Settings::fhuOpacity,
				Settings::kOpacityMin, Settings::kOpacityMax, 0.05f));
			if (LightingAddon::Enabled()) {
				fhu.push_back(std::make_unique<Slider<float>>("FHU 照明补偿", "给悬垂细丝、圆滴与积液增加材质补光。0 为原版亮度。", &Settings::fhuLightingCompensation, Settings::kLightingCompensationMin, Settings::kLightingCompensationMax, 1.0f));
			}

			fhu.push_back(std::make_unique<Heading>("存留时间"));
			fhu.push_back(std::make_unique<Slider<float>>("存留时间", "液体开始消退前的存留秒数。", &Settings::fhuLifetime,
				Settings::kLifetimeMin, Settings::kLifetimeMax, 5.0f));
			fhu.push_back(std::make_unique<Slider<float>>("消退时间", "液体开始消退后到消失的秒数。", &Settings::fhuFadeTime,
				Settings::kFadeMin, Settings::kFadeMax, 0.5f));
		}

		void BuildArousal()
		{
			auto&      page = arousal;
			const auto found = [] { return Settings::Get().enabled && Arousal::Installed(); };
			const auto active = [] { return Settings::Get().enabled && Arousal::Installed() && Settings::Get().arousal; };

			page.push_back(std::make_unique<Note>([] {
				return std::string(Arousal::Live()      ? "已检测到 SLO Aroused NG，唤起值从插件实时读取。" :
				                   Arousal::Installed() ? "已检测到唤起值 Mod。" :
				                                          "未检测到唤起值 Mod。此功能需要 SLO Aroused NG、OSL Aroused，或沿用 SexLab Aroused 唤起值阵营的其他 Mod。");
			}));
			page.push_back(std::make_unique<Toggle>("唤起滴液", "附近裸体角色在场景之外也会因唤起而滴液。", &Settings::arousal, found));
			page.push_back(std::make_unique<Toggle>("润滑液", "有阴道的角色在唤起时会从阴道滴液。", &Settings::arousalWetness, active));
			page.push_back(std::make_unique<Toggle>("射精前液", "有阴茎的角色在唤起时会从龟头滴出射精前液。", &Settings::arousalPrecum, active));
			page.push_back(std::make_unique<Slider<float>>("唤起阈值", "开始滴液所需的唤起值（0–100）。", &Settings::arousalThreshold,
				Settings::kThresholdMin, Settings::kThresholdMax, 1.0f, active));
			page.push_back(std::make_unique<Slider<float>>("唤起滴液间隔",
				"唤起值满时的平均滴液间隔（秒）。刚达到阈值时，间隔是该值的 4 倍。", &Settings::arousalInterval,
				Settings::kArousalIntervalMin, Settings::kArousalIntervalMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<float>>("唤起滴液范围", "角色在玩家周围多远以内会滴液，采用游戏单位。", &Settings::arousalRadius,
				Settings::kReachMin, Settings::kReachMax, 128.0f, active));

			page.push_back(std::make_unique<Heading>("暴露型护甲"));
			page.push_back(std::make_unique<Note>([] {
				return std::string("身体装备栏有护甲时不滴液，下方标记的护甲及带 OStrands_Revealing 关键词的护甲除外。      ");
			}));
			page.push_back(std::make_unique<Button>("将身上护甲标记为暴露型",
				"以控制台选中的角色为准，未选中时使用玩家。任何穿着此护甲的角色都可以透过它滴液。",
				[] { SKSE::GetTaskInterface()->AddTask([] { Revealing::Mark(true); }); }));
			page.push_back(std::make_unique<Button>("取消身上护甲的标记",
				"以控制台选中的角色为准，未选中时使用玩家。取消后，此护甲会重新阻挡滴液。",
				[] { SKSE::GetTaskInterface()->AddTask([] { Revealing::Mark(false); }); }));
			page.push_back(std::make_unique<Key>("护甲标记切换热键",
				"点击后按键。该键可切换控制台所选角色身上护甲的标记。未选中角色时切换玩家身上的护甲。",
				&Settings::revealingKey));
			page.push_back(std::make_unique<Note>([] {
				std::size_t marked = 0;
				{
					auto guard = Settings::Lock();
					marked = Settings::Get().revealingArmor.size();
				}
				const auto status = Revealing::Status();
				return std::format("已标记 {} 件护甲。{}{}", marked, status.empty() ? "" : " ", status);
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
				return std::string("调整液丝、滴液和精液的起点。若开口与骨骼位置不一致，可开启碰撞体显示，边调边看标记。数值采用游戏单位。                                          ");
			}));
			page.push_back(std::make_unique<Toggle>("显示碰撞体",
				"在控制台选中的角色、场景角色或玩家身上显示碰撞点及开口标记。正常游玩时请关闭。", &Settings::showShapes, on));

			page.push_back(std::make_unique<Heading>("阴道"));
			slider("阴道前后偏移", "正值使起点向体外移动，负值使其向体内移动。", &Settings::vaginaOut);
			slider("阴道上下偏移", "正值使起点向头部移动，负值使其向脚部移动。", &Settings::vaginaUp);

			page.push_back(std::make_unique<Heading>("肛门"));
			slider("肛门前后偏移", "正值使起点向体外移动，负值使其向体内移动。", &Settings::anusOut);
			slider("肛门上下偏移", "正值使起点向头部移动，负值使其向脚部移动。", &Settings::anusUp);

			page.push_back(std::make_unique<Heading>("嘴部"));
			slider("嘴部前后偏移", "正值使起点向面部前方移动，负值使其向口腔内移动。", &Settings::mouthOut);
			slider("嘴部上下偏移", "正值使起点向鼻部移动，负值使其向下巴移动。", &Settings::mouthUp);

			page.push_back(std::make_unique<Button>("重置位置", "将此页所有偏移值重置为 0。", [] {
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
			logger::info("SKSE Menu Framework not found; change settings in FHUCompanion_User.ini");
			return;
		}
		if (fhu.empty()) BuildFhu();
        SKSEMenuFramework::SetSection("FHU x OStrands");


		SKSEMenuFramework::AddSectionItem("Fill Her Up 排出", DrawFhu);


		logger::info("settings added to SKSE Menu Framework (API {})", SKSEMenuFramework::GetMenuFrameworkAPIVersion());
	}
}
