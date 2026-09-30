#include "adapter.h"
#include "search_control.h"
#include "sim/search/BattleScumSearcher2.h"
#include <thread>
#include <mutex>
#include <iostream>
#include <set>
#include <memory>

using advisor::J;
namespace {
std::mutex gate;
std::ostream wire(std::cout.rdbuf());
void emit(const J &j) {
    std::string line=j.dump();
    if(line.size()>4*1024*1024) line=J{{"version",1},{"id",j.value("id",J(nullptr))},{"ok",false},{"error",{{"code","LIMIT"},{"message","response exceeds 4 MiB"}}}}.dump();
    wire << line << '\n'; wire.flush();
}
J error(const J &id,const std::string &code,const std::string &message) {
    return J{{"version",1},{"id",id},{"ok",false},{"error",{{"code",code},{"message",message}}}};
}
J plan(const J &request, advisor::Control &control) {
    advisor::control=&control;
    struct Reset { ~Reset(){ advisor::control=nullptr; sts::search::g_debug_scum_search=nullptr; } } reset;
    advisor::poll();
    auto imported=advisor::importGame(request.at("snapshot"));
    auto &b=imported.battle; const int turn=b.turn;
    J result={{"version",1},{"id",request.at("id")},{"ok",true},{"snapshot_token",request.at("snapshot_token")},
        {"parity","INCOMPLETE"},{"actions",J::array()},{"search_calls",0},{"simulations",0},{"complete_turn",false}};
    std::vector<sts::search::Action> actions;
    auto op=request.at("op").get<std::string>();
    if(op=="search") {
        int budget=request.at("simulations").get<int>();
        sts::search::BattleScumSearcher2 search(b);
        // Same initial seed, one search invocation, one uninterrupted tree.
        search.search(budget);
        result["search_calls"]=1; result["simulations"]=search.root.simulationCount;
        actions=search.bestActionSequence;
        if(actions.empty()) throw std::runtime_error("no executable plan found in budget");
    } else if(op=="validate") {
        if(!request.at("actions").is_array() || request.at("actions").size()>512) throw std::invalid_argument("actions must be an array of at most 512 bits");
        for(const auto &bits:request.at("actions")) {
            if(!bits.is_number_unsigned() && !bits.is_number_integer()) throw std::invalid_argument("action bits must be uint32");
            auto value=bits.get<int64_t>(); if(value<0 || value>UINT32_MAX) throw std::invalid_argument("action bits out of range");
            actions.emplace_back(static_cast<uint32_t>(value));
        }
    } else { result["imported_guard"]=advisor::guard(b,imported.uuids); return result; }
    for(std::size_t n=0;n<actions.size();++n) {
        advisor::poll(); auto a=actions[n];
        if(b.turn!=turn || b.outcome!=sts::Outcome::UNDECIDED) {
            if(op=="validate") throw std::invalid_argument("sequence extends beyond this turn");
            break;
        }
        // Validate enum and indices before calling engine assertions.
        int type=static_cast<int>(a.getActionType());
        if(type<0 || type>4 || !a.isValidAction(b)) throw std::invalid_argument("illegal action at index "+std::to_string(n));
        auto row=advisor::describe(a,b,imported.uuids);
        result["actions"].push_back(row);
        if(a.getActionType()==sts::search::ActionType::END_TURN) {
            result["complete_turn"]=true;
            if(op=="validate" && n+1!=actions.size()) throw std::invalid_argument("actions after end_turn");
            break; // Never simulate opponent actions merely to show the end button.
        }
        a.execute(b);
        if(b.outcome!=sts::Outcome::UNDECIDED || b.turn!=turn) result["complete_turn"]=true;
        if(result["actions"].size()>=512) break;
    }
    advisor::poll();
    result["stop_reason"]=result["complete_turn"].get<bool>()?"turn_end_or_combat_end":"plan_exhausted";
    result["final_guard"]=advisor::guard(b,imported.uuids);
    return result;
}
}
int main() {
    // Core diagnostics must never contaminate the one-line protocol channel.
    std::cout.rdbuf(std::cerr.rdbuf());
    std::thread worker;
    std::shared_ptr<advisor::Control> active;
    std::string activeId;
    std::set<std::string> seen;
    std::string line;
    // Bounded line reader also drains oversized lines to recover framing.
    while(true) {
        line.clear(); bool oversized=false; int ch;
        while((ch=std::cin.get())!=EOF && ch!='\n') { if(line.size()<4*1024*1024) line.push_back(static_cast<char>(ch)); else oversized=true; }
        if(ch==EOF && line.empty() && !oversized) break;
        J id=nullptr;
        try {
            if(oversized) throw std::invalid_argument("request exceeds 4 MiB");
            J request=J::parse(line);
            if(!request.is_object()) throw std::invalid_argument("request must be object");
            if(request.contains("id") && request.at("id").is_string()) id=request.at("id");
            if(id.is_null() || id.get<std::string>().empty() || id.get<std::string>().size()>128) throw std::invalid_argument("id must be 1..128 characters");
            if(!request.contains("version") || !request.at("version").is_number_integer() || request.at("version")!=1) {
                std::lock_guard<std::mutex> lock(gate); emit(error(id,"VERSION_MISMATCH","expected version 1")); continue;
            }
            auto op=request.at("op").get<std::string>();
            auto name=id.get<std::string>();
            if(!seen.insert(name).second) { std::lock_guard<std::mutex> lock(gate); emit(error(id,"DUPLICATE_ID","use a new request id")); continue; }
            if(seen.size()>10000) { std::lock_guard<std::mutex> lock(gate); emit(error(id,"LIMIT","restart session after 10000 requests")); break; }
            if(op=="ping") { std::lock_guard<std::mutex> lock(gate); emit(J{{"version",1},{"id",id},{"ok",true},{"engine","SearchAdvisor/0.1.0"},{"parity","INCOMPLETE"}}); continue; }
            if(op=="cancel") {
                auto target=request.at("request_id").get<std::string>();
                std::lock_guard<std::mutex> lock(gate); bool accepted=active && activeId==target;
                if(accepted) active->cancelled=true;
                emit(J{{"version",1},{"id",id},{"ok",true},{"accepted",accepted},{"request_id",target}}); continue;
            }
            if(op!="search" && op!="validate" && op!="inspect") { std::lock_guard<std::mutex> lock(gate); emit(error(id,"UNKNOWN_OP",op)); continue; }
            if(!request.at("snapshot_token").is_string() || request.at("snapshot_token").get<std::string>().size()>128 || !request.at("snapshot").is_object()) throw std::invalid_argument("snapshot and snapshot_token required");
            if(request.contains("timeout_ms") && !request.at("timeout_ms").is_number_integer()) throw std::invalid_argument("integer timeout_ms required");
            auto timeoutValue=request.value("timeout_ms",int64_t{5000});
            if(timeoutValue<1 || timeoutValue>30000) throw std::invalid_argument("timeout_ms must be 1..30000");
            int timeout=static_cast<int>(timeoutValue);
            if(timeout<1 || timeout>30000) throw std::invalid_argument("timeout_ms must be 1..30000");
            if(op=="search") {
                if(!request.at("simulations").is_number_integer()) throw std::invalid_argument("integer simulations required");
                auto budget=request.at("simulations").get<int64_t>(); if(budget<1 || budget>100000) throw std::invalid_argument("simulations must be 1..100000");
            }
            {
                std::lock_guard<std::mutex> lock(gate);
                if(active) { emit(error(id,"BUSY","one active computation per process")); continue; }
            }
            if(worker.joinable()) worker.join();
            auto control=std::make_shared<advisor::Control>(); control->deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
            { std::lock_guard<std::mutex> lock(gate); active=control; activeId=name; }
            worker=std::thread([&,request,control,id] {
                J response;
                try { response=plan(request,*control); }
                catch(const advisor::Interrupted &e) { response=error(id,e.what(),"no partial plan is returned"); }
                catch(const J::exception &e) { response=error(id,"INVALID_SNAPSHOT",e.what()); }
                catch(const std::invalid_argument &e) { response=error(id,"UNSUPPORTED_STATE",e.what()); }
                catch(const std::exception &e) { response=error(id,"SEARCH_FAILED",e.what()); }
                std::lock_guard<std::mutex> lock(gate);
                if(control->cancelled) response=error(id,"CANCELLED","no partial plan is returned");
                emit(response); active.reset(); activeId.clear();
            });
        } catch(const J::parse_error &e) { std::lock_guard<std::mutex> lock(gate); emit(error(id,"INVALID_JSON",e.what())); }
          catch(const std::exception &e) { std::lock_guard<std::mutex> lock(gate); emit(error(id,"BAD_REQUEST",e.what())); }
    }
    { std::lock_guard<std::mutex> lock(gate); if(active) active->cancelled=true; }
    if(worker.joinable()) worker.join();
}
