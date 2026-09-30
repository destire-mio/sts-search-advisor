#pragma once
#include "combat/BattleContext.h"
#include "sim/search/Action.h"
#include <nlohmann/json.hpp>
#include <map>
#include <string>
namespace advisor {
using J=nlohmann::json;
struct Imported {
    sts::BattleContext battle;
    std::map<int,std::string> uuids;
};
J normalize(const J &game);
Imported importGame(const J &game);
J guard(const sts::BattleContext &battle, const std::map<int,std::string> &uuids);
J describe(const sts::search::Action &action, const sts::BattleContext &battle, const std::map<int,std::string> &uuids);
}
