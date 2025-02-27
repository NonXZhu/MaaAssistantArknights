#include "InfrastTask.h"

#include "Utils/Logger.hpp"

#include "Plugin/DronesForShamareTaskPlugin.h"
#include "Plugin/ReplenishOriginiumShardTaskPlugin.h"
#include "Sub/InfrastControlTask.h"
#include "Sub/InfrastDormTask.h"
#include "Sub/InfrastInfoTask.h"
#include "Sub/InfrastMfgTask.h"
#include "Sub/InfrastOfficeTask.h"
#include "Sub/InfrastPowerTask.h"
#include "Sub/InfrastReceptionTask.h"
#include "Sub/InfrastTradeTask.h"
#include "Sub/ProcessTask.h"

asst::InfrastTask::InfrastTask(const AsstCallback& callback, void* callback_arg)
    : PackageTask(callback, callback_arg, TaskType),
      m_infrast_begin_task_ptr(std::make_shared<ProcessTask>(callback, callback_arg, TaskType)),
      m_info_task_ptr(std::make_shared<InfrastInfoTask>(callback, callback_arg, TaskType)),
      m_mfg_task_ptr(std::make_shared<InfrastMfgTask>(callback, callback_arg, TaskType)),
      m_trade_task_ptr(std::make_shared<InfrastTradeTask>(callback, callback_arg, TaskType)),
      m_power_task_ptr(std::make_shared<InfrastPowerTask>(callback, callback_arg, TaskType)),
      m_control_task_ptr(std::make_shared<InfrastControlTask>(callback, callback_arg, TaskType)),
      m_reception_task_ptr(std::make_shared<InfrastReceptionTask>(callback, callback_arg, TaskType)),
      m_office_task_ptr(std::make_shared<InfrastOfficeTask>(callback, callback_arg, TaskType)),
      m_dorm_task_ptr(std::make_shared<InfrastDormTask>(callback, callback_arg, TaskType))
{
    m_infrast_begin_task_ptr->set_tasks({ "InfrastBegin" });
    m_replenish_task_ptr = m_mfg_task_ptr->register_plugin<ReplenishOriginiumShardTaskPlugin>();

    m_subtasks.emplace_back(m_infrast_begin_task_ptr);
}

bool asst::InfrastTask::set_params(const json::value& params)
{
    LogTraceFunction;

    int mode = params.get("mode", 0);
    bool is_custom = static_cast<Mode>(mode) == Mode::Custom;

    if (!m_running) {
        auto facility_opt = params.find<json::array>("facility");
        if (!facility_opt) {
            return false;
        }

        auto append_infrast_begin = [&]() { m_subtasks.emplace_back(m_infrast_begin_task_ptr); };

        m_subtasks.clear();
        append_infrast_begin();
        m_subtasks.emplace_back(m_info_task_ptr);

        for (const auto& facility_json : facility_opt.value()) {
            if (!facility_json.is_string()) {
                m_subtasks.clear();
                append_infrast_begin();
                return false;
            }

            std::string facility = facility_json.as_string();
            if (facility == "Dorm") {
                m_subtasks.emplace_back(m_dorm_task_ptr);
            }
            else if (facility == "Mfg") {
                m_subtasks.emplace_back(m_mfg_task_ptr);
            }
            else if (facility == "Trade") {
                m_subtasks.emplace_back(m_trade_task_ptr);
            }
            else if (facility == "Power") {
                m_subtasks.emplace_back(m_power_task_ptr);
            }
            else if (facility == "Office") {
                m_subtasks.emplace_back(m_office_task_ptr);
            }
            else if (facility == "Reception") {
                m_subtasks.emplace_back(m_reception_task_ptr);
            }
            else if (facility == "Control") {
                m_subtasks.emplace_back(m_control_task_ptr);
            }
            else {
                Log.error(__FUNCTION__, "| Unknown facility", facility);
                m_subtasks.clear();
                append_infrast_begin();
                return false;
            }
            append_infrast_begin();
        }
    }

    if (!is_custom) {
        std::string drones = params.get("drones", "_NotUse");
        m_mfg_task_ptr->set_uses_of_drone(drones);
        m_trade_task_ptr->set_uses_of_drone(drones);
        m_trade_task_ptr->register_plugin<DronesForShamareTaskPlugin>()->set_retry_times(0);
    }

    double threshold = params.get("threshold", 0.3);
    m_info_task_ptr->set_mood_threshold(threshold);
    m_mfg_task_ptr->set_mood_threshold(threshold);
    m_trade_task_ptr->set_mood_threshold(threshold);
    m_power_task_ptr->set_mood_threshold(threshold);
    m_control_task_ptr->set_mood_threshold(threshold);
    m_reception_task_ptr->set_mood_threshold(threshold);
    m_office_task_ptr->set_mood_threshold(threshold);
    m_dorm_task_ptr->set_mood_threshold(threshold);

    bool dorm_notstationed_enabled = params.get("dorm_notstationed_enabled", false);
    m_dorm_task_ptr->set_notstationed_enabled(dorm_notstationed_enabled);

    bool drom_trust_enabled = params.get("drom_trust_enabled", true);
    m_dorm_task_ptr->set_trust_enabled(drom_trust_enabled);

    bool replenish = params.get("replenish", false);
    m_replenish_task_ptr->set_enable(replenish);

    if (is_custom && !m_running) {
        std::string filename = params.at("filename").as_string();
        int index = params.get("plan_index", 0);

        try {
            return parse_and_set_custom_config(utils::path(filename), index);
        }
        catch (const json::exception& e) {
            Log.error("Json parse failed", utils::path(filename), e.what());
            return false;
        }
        catch (const std::exception& e) {
            Log.error("Json parse failed", utils::path(filename), e.what());
            return false;
        }
    }

    return true;
}

bool asst::InfrastTask::parse_and_set_custom_config(const std::filesystem::path& path, int index)
{
    LogTraceFunction;

    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        Log.error("custom infrast file does not exist:", path);
        return false;
    }

    auto custom_json_opt = json::open(path);
    if (!custom_json_opt) {
        Log.error("failed to open json file:", path);
        return false;
    }
    auto& custom_json = custom_json_opt.value();
    Log.trace(__FUNCTION__, "| custom json:", custom_json.to_string());

    auto& all_plans = custom_json.at("plans").as_array();
    if (index < 0 || index >= int(all_plans.size())) {
        Log.error("index is out of range, plans size:", all_plans.size(), ", index:", index);
        return false;
    }
    auto& cur_plan = all_plans.at(index);

    for (const auto& [facility, facility_info] : cur_plan.at("rooms").as_object()) {
        infrast::CustomFacilityConfig facility_config;

        for (const auto& room_info : facility_info.as_array()) {
            infrast::CustomRoomConfig room_config;
            room_config.skip = room_info.get("skip", false);
            room_config.autofill = room_info.get("autofill", false);
            room_config.sort = room_info.get("sort", false);
            static std::unordered_map<std::string, infrast::CustomRoomConfig::Product> ProductNames = {
                { "Battle Record", infrast::CustomRoomConfig::Product::BattleRecord },
                { "Pure Gold", infrast::CustomRoomConfig::Product::PureGold },
                { "Dualchip", infrast::CustomRoomConfig::Product::Dualchip },
                { "Originium Shard", infrast::CustomRoomConfig::Product::OriginiumShard },
                { "LMD", infrast::CustomRoomConfig::Product::LMD },
                { "Orundum", infrast::CustomRoomConfig::Product::Orundum },
            };
            std::string product = room_info.get("product", std::string());
            if (!product.empty()) {
                if (auto iter = ProductNames.find(product); iter != ProductNames.cend()) {
                    room_config.product = iter->second;
                }
                else {
                    Log.error("Unknown product", product);
                    return false;
                }
            }

            if (auto opers_opt = room_info.find<json::array>("operators")) {
                for (const auto& oper_name : opers_opt.value()) {
                    std::string name = oper_name.as_string();
                    if (name.empty()) {
                        Log.warn("operators.name is empty");
                        continue;
                    }
                    room_config.names.emplace_back(std::move(name));
                }
            }
            if (auto candidates_opt = room_info.find<json::array>("candidates")) {
                for (const auto& candidate_name : candidates_opt.value()) {
                    std::string name = candidate_name.as_string();
                    if (name.empty()) {
                        Log.warn("operators.candidates is empty");
                        continue;
                    }
                    room_config.candidates.emplace_back(std::move(name));
                }
            }
            facility_config.emplace_back(std::move(room_config));
        }

        if (facility == "control") {
            m_control_task_ptr->set_custom_config(facility_config);
        }
        else if (facility == "manufacture") {
            m_mfg_task_ptr->set_custom_config(facility_config);
        }
        else if (facility == "trading") {
            m_trade_task_ptr->set_custom_config(facility_config);
        }
        else if (facility == "power") {
            m_power_task_ptr->set_custom_config(facility_config);
        }
        else if (facility == "meeting") {
            m_reception_task_ptr->set_custom_config(facility_config);
        }
        else if (facility == "hire") {
            m_office_task_ptr->set_custom_config(facility_config);
        }
        else if (facility == "dormitory") {
            m_dorm_task_ptr->set_custom_config(facility_config);
        }
        else {
            Log.error(__FUNCTION__, "unknown facility", facility);
            return false;
        }
    }

    do {
        auto Fia_opt = cur_plan.find<json::object>("Fiammetta");
        if (!Fia_opt) {
            break;
        }
        const auto& Fia_json = Fia_opt.value();
        bool enable = Fia_json.get("enable", true);
        if (!enable) {
            break;
        }
        std::string target = Fia_json.get("target", std::string());
        if (target.empty()) {
            Log.warn("Fiammetta's target is unsetted or empty");
            break;
        }
        Log.trace("Fiammetta's target:", target);
        infrast::CustomFacilityConfig facility_config(1);
        auto& room_config = facility_config.front();
        room_config.names = { target, "菲亚梅塔" };
        room_config.sort = true;

        auto Fia_task_ptr = std::make_shared<InfrastDormTask>(m_callback, m_callback_arg, TaskType);
        Fia_task_ptr->set_custom_config(std::move(facility_config));

        if (Fia_json.get("order", "pre") != "post") {
            m_subtasks.insert(m_subtasks.begin(), m_infrast_begin_task_ptr);
            m_subtasks.insert(m_subtasks.begin() + 1, std::move(Fia_task_ptr));
        }
        else {
            m_subtasks.emplace_back(std::move(Fia_task_ptr));
            m_subtasks.emplace_back(m_infrast_begin_task_ptr);
        }
    } while (false);

    do {
        auto drones_opt = cur_plan.find<json::object>("drones");
        if (!drones_opt) {
            break;
        }
        const auto& drones_json = drones_opt.value();
        bool enable = drones_json.get("enable", true);
        if (!enable) {
            break;
        }
        infrast::CustomDronesConfig drones_config;
        drones_config.index = drones_json.get("index", 1) - 1;
        drones_config.order = drones_json.get("order", "pre") == "post" ? infrast::CustomDronesConfig::Order::Post
                                                                        : infrast::CustomDronesConfig::Order::Pre;
        std::string room = drones_json.get("room", std::string());
        if (room.empty()) {
            Log.warn("drones room is unsetted or empty");
            break;
        }
        else if (room == "trading") {
            m_trade_task_ptr->set_custom_drones_config(std::move(drones_config));
        }
        else if (room == "manufacture") {
            m_mfg_task_ptr->set_custom_drones_config(std::move(drones_config));
        }
        else {
            Log.error("error drones config, unknown room", room);
            return false;
        }
    } while (false);

    return true;
}
