#include "adapter.h"
#include "snapshot_import.h"
#include "name_tables.h"
#include "constants/Relics.h"
#include <algorithm>
#include <limits>
#include <set>

namespace advisor {
static std::string alias(const std::map<std::string,std::string> &table, const std::string &name) {
    const auto it=table.find(name); return it==table.end()?name:it->second;
}
static std::string identifier(const J &v) { return v.at("id").get<std::string>(); }
static int cardId(const std::string &name) {
    auto key=alias(CARD_ALIASES,name);
    try { return enumFromNames(key,cardEnumStrings); }
    catch(const std::invalid_argument &) { return enumFromNames(key,cardNames); }
}
static int relicId(const std::string &name) {
    auto key=normalizedName(name);
    for(int i=0;i<static_cast<int>(RelicId::INVALID);++i)
        if(normalizedName(relicIds[i])==key || normalizedName(relicEnumNames[i])==key || normalizedName(getRelicName(static_cast<RelicId>(i)))==key) return i;
    throw std::invalid_argument("unsupported relic: "+name);
}
static int potionId(const std::string &name) {
    if(name=="Potion Slot") return static_cast<int>(Potion::EMPTY_POTION_SLOT);
    try { return enumFromNames(name,potionEnumNames); }
    catch(const std::invalid_argument &) {
        try { return enumFromNames(name,potionIds); }
        catch(const std::invalid_argument &) { return enumFromNames(name,potionNames); }
    }
}
static int powerId(const std::string &name, bool player) {
    if(player) {
        auto key=alias(PLAYER_POWER_ALIASES,name);
        try { return enumFromNames(key,playerStatusEnumStrings); }
        catch(const std::invalid_argument &) { return enumFromNames(key,playerStatusStrings); }
    }
    auto key=alias(MONSTER_POWER_ALIASES,name);
    try { return enumFromNames(key,monsterStatusEnumStrings); }
    catch(const std::invalid_argument &) { return enumFromNames(key,enemyStatusStrings); }
}
static void range(int x,int lo,int hi,const char *field) {
    if(x<lo || x>hi) throw std::invalid_argument(std::string("out of range: ")+field);
}
static J card(const J &raw) {
    int id=cardId(identifier(raw));
    if(id==static_cast<int>(CardId::INVALID)) throw std::invalid_argument("invalid card");
    // CardInstance has fixed-width cost and misc fields. Reject rather than truncate.
    range(raw.at("cost").get<int>(),-2,127,"card cost");
    range(raw.at("base_cost").get<int>(),-2,127,"card base_cost");
    range(raw.at("upgrades").get<int>(),0,32767,"upgrades");
    CardInstance definition(static_cast<CardId>(id),raw.at("upgrades").get<int>()>0);
    if(raw.contains("exhausts") && raw.at("exhausts").get<bool>()!=definition.doesExhaust()) throw std::invalid_argument("unsupported per-instance exhaust override");
    if(raw.contains("ethereal") && raw.at("ethereal").get<bool>()!=definition.isEthereal()) throw std::invalid_argument("unsupported per-instance ethereal override");
    int misc=raw.value("misc",0);
    if(id==static_cast<int>(CardId::RAMPAGE)) misc=raw.at("base_damage").get<int>()-8;
    if(id==static_cast<int>(CardId::RITUAL_DAGGER)) misc=raw.at("base_damage").get<int>();
    range(misc,-32768,32767,"card misc");
    return J{{"id",id},{"cost",raw.at("cost")},{"base_cost",raw.at("base_cost")},
        {"upgrades",raw.at("upgrades")},{"misc",misc},{"free_to_play_once",raw.at("free_to_play_once")}};
}
static J power(const J &raw,bool player) {
    std::string name=identifier(raw);
    if(player && name.rfind("TheBomb",0)==0)
        return J{{"id",powerId("THE_BOMB",true)},{"amount",raw.at("damage")},{"bomb_turns",raw.at("amount")}};
    if(player && name=="Panache")
        return J{{"id",powerId(name,true)},{"amount",raw.at("damage")},{"counter",raw.at("amount")}};
    if(!player && name=="Stasis") return J{{"id",powerId(name,false)},{"amount",1},{"card",card(raw.at("card"))}};
    int amount=raw.at("amount").get<int>();
    bool signedAmount=name=="Strength" || name=="Dexterity" || name=="Focus";
    if(amount<0 && !signedAmount) amount=1;
    if(!player && name=="Compulsive") amount=0;
    range(amount,-32768,32767,"power amount");
    int id=powerId(name,player);
    // Imported monster status scalar storage varies; keep its proven narrow range.
    if(!player) range(amount,-128,127,"monster power amount");
    J out={{"id",id},{"amount",amount},{"just_applied",raw.value("just_applied",false)}};
    if(raw.contains("misc")) out["misc"]=raw.at("misc");
    return out;
}
static std::string monsterKey(const J &raw) {
    return monsterIdStrings[enumFromNames(alias(MONSTER_ALIASES,identifier(raw)),monsterIdStrings)];
}
static int move(const J &raw,const std::string &key,const char *field) {
    int value=raw.value(field,-1);
    if(value<0) {
        if(std::string(field)=="move_id" && (raw.at("current_hp").get<int>()>0 || raw.value("half_dead",false)) && !raw.value("is_gone",false))
            throw std::invalid_argument("living monster lacks move");
        return static_cast<int>(MMID::INVALID);
    }
    auto it=MOVE_NAMES.find({key,value});
    if(it==MOVE_NAMES.end()) throw std::invalid_argument("unsupported monster move: "+key+":"+std::to_string(value));
    return enumFromNames(it->second,monsterMoveStrings);
}
static J monster(const J &raw) {
    std::string key=monsterKey(raw); const J &in=raw.at("internal"); int misc=0;
    // Phase one keeps Java and simulator slots identical. Summons and split slot
    // canonicalization require separate identity contracts before auto execution.
    static const std::set<std::string> unsupported={"REPTOMANCER","DAGGER","GREMLIN_LEADER","BRONZE_AUTOMATON","BRONZE_ORB","THE_COLLECTOR","TORCH_HEAD","SLIME_BOSS","ACID_SLIME_L","SPIKE_SLIME_L"};
    if(unsupported.count(key)) throw std::invalid_argument("unsupported dynamic monster slots: "+key);
    if(key=="RED_LOUSE" || key=="GREEN_LOUSE") misc=in.at("biteDamage").get<int>();
    else if(key=="DARKLING") misc=in.at("nipDmg").get<int>();
    else if(key=="BOOK_OF_STABBING") misc=in.at("stabCount").get<int>();
    else if(key=="HEXAGHOST") misc=raw.at("move_id")==1?raw.at("move_base_damage").get<int>():0;
    else if(key=="GREMLIN_WIZARD") misc=in.at("currentCharge").get<int>();
    else if(key=="LAGAVULIN") misc=in.at("idleCount").get<int>();
    else if(key=="RED_SLAVER") misc=in.at("usedEntangle").get<bool>();
    else if(key=="THE_CHAMP") misc=in.at("forgeTimes").get<int>() | (in.at("thresholdReached").get<bool>()?4:0);
    else if(key=="THE_GUARDIAN") misc=in.at("dmgThreshold").get<int>()-(in.at("isOpen").get<bool>()?0:10);
    else if(key=="SPIKER") misc=in.at("thornsCount").get<int>();
    else if(key=="WRITHING_MASS") misc=in.at("usedMegaDebuff").get<bool>();
    else if(key=="LOOTER" || key=="MUGGER") misc=in.at("stolenGold").get<int>();
    else if(key=="TIME_EATER") misc=in.at("usedHaste").get<bool>();
    else if(key=="AWAKENED_ONE") {
        bool first=raw.value("half_dead",false); for(const auto &p:raw.at("powers")) if(identifier(p)=="Unawakened") first=true;
        misc=first?0:1;
    }
    J powers=J::array(); for(const auto &p:raw.at("powers")) if(!IGNORED_MONSTER_POWERS.count(identifier(p))) powers.push_back(power(p,false));
    if(key=="LAGAVULIN" && (raw.at("move_id")==5 || raw.at("move_id")==6) && in.at("asleep").get<bool>()) powers.push_back(J{{"id",powerId("ASLEEP",false)},{"amount",1}});
    J out={{"id",enumFromNames(key,monsterIdStrings)},{"current_hp",raw.at("current_hp")},{"max_hp",raw.at("max_hp")},{"block",raw.at("block")},
        {"half_dead",raw.at("half_dead")},{"is_gone",raw.at("is_gone")},{"move",move(raw,key,"move_id")},{"last_move",move(raw,key,"last_move_id")},{"misc_info",misc},{"powers",powers}};
    if(key=="HEXAGHOST") out["unique_power0"]=in.at("orbActiveCount");
    return out;
}
J normalize(const J &game) {
    if(game.at("screen_type")!="NONE") throw std::invalid_argument("not a normal player decision");
    if(game.at("class")!="IRONCLAD") throw std::invalid_argument("only Ironclad is supported");
    const J &c=game.at("combat_state"); const J &p=c.at("player");
    range(c.at("turn").get<int>(),1,499,"turn");
    range(p.at("current_hp").get<int>(),1,32767,"hp");
    range(p.at("max_hp").get<int>(),1,32767,"max_hp");
    range(p.at("energy").get<int>(),0,999,"energy");
    range(game.at("ascension_level").get<int>(),0,20,"ascension");
    if(c.at("monsters").empty() || c.at("monsters").size()>5) throw std::invalid_argument("invalid monster count");
    J s={{"seed",cast<std::uint64_t>(game.at("seed"))},{"floor",game.at("floor")},{"ascension",game.at("ascension_level")},{"turn",c.at("turn")},
        {"frame_delta_seconds",c.value("frame_delta_seconds",1.0/60.0)},{"rngs",c.at("rngs")},
        {"end_turn_shuffle",c.at("end_turn_shuffle")},{"encounter",static_cast<int>(MonsterEncounter::INVALID)}};
    // No guessed streams; consuming these fields also checks complete six-stream schema.
    for(auto name:{"ai","card_random","misc","monster_hp","potion","shuffle"}) {
        auto r=randomFromSnapshot(s.at("rngs").at(name));
        if((r.seed0|r.seed1)==0 || r.counter<0) throw std::invalid_argument("invalid RNG state");
    }
    auto shuffle=endTurnShuffleFromSnapshot(s.at("end_turn_shuffle"));
    if(shuffle.mode==EndTurnShuffleMode::JAVA_SHARED && !shuffle.sharedRngInitialized) throw std::invalid_argument("uninitialized shared RNG is nondeterministic");
    if(s.at("end_turn_shuffle").value("shared_rng_initialized",false) && cast<std::uint64_t>(s.at("end_turn_shuffle").at("shared_seed48"))>=(1ULL<<48)) throw std::invalid_argument("invalid shared seed");
    for(auto field:{"cards_played_this_turn","attacks_played_this_turn","skills_played_this_turn","cards_discarded_this_turn","times_damaged"}) s[field]=c.at(field);
    J player=p; player["gold"]=game.at("gold"); player["energy_per_turn"]=c.at("energy_per_turn"); player["card_draw_per_turn"]=c.at("card_draw_per_turn"); player["powers"]=J::array();
    for(const auto &v:p.at("powers")) {
        if(identifier(v)=="Berserk") player["energy_per_turn"]=player.at("energy_per_turn").get<int>()+v.at("amount").get<int>();
        else player["powers"].push_back(power(v,true));
    }
    player["is_bloodied"]=c.at("is_bloodied");
    for(auto it=c.at("relic_combat_state").begin();it!=c.at("relic_combat_state").end();++it) player[it.key()]=it.value();
    for(const auto &v:p.at("powers")) if(identifier(v)=="Surrounded") player["last_targeted_monster"]=c.at("facing_monster_index");
    s["player"]=player;
    std::set<std::string> uuids; int count=0;
    for(auto pile:{"hand","draw_pile","discard_pile","exhaust_pile"}) {
        s[pile]=J::array(); if(!c.at(pile).is_array()) throw std::invalid_argument("pile must be array");
        for(const auto &v:c.at(pile)) {
            if(++count>500) throw std::invalid_argument("pile capacity");
            auto uuid=v.at("uuid").get<std::string>(); if(uuid.empty() || !uuids.insert(uuid).second) throw std::invalid_argument("duplicate or missing card identity");
            s[pile].push_back(card(v));
        }
    }
    if(s.at("hand").size()>10) throw std::invalid_argument("hand capacity");
    s["monsters"]=J::array();
    for(const auto &v:c.at("monsters")) {
        s["monsters"].push_back(monster(v)); auto key=monsterKey(v);
        auto boss=BOSS_ENCOUNTERS.find(key); if(boss!=BOSS_ENCOUNTERS.end()) s["encounter"]=enumFromNames(boss->second,monsterEncounterStrings);
        if(key=="SPIRE_SHIELD" || key=="SPIRE_SPEAR") s["encounter"]=static_cast<int>(MonsterEncounter::SHIELD_AND_SPEAR);
    }
    s["relics"]=J::array(); for(const auto &v:game.at("relics")) {
        int id=relicId(identifier(v)); s["relics"].push_back(J{{"id",id},{"counter",v.at("counter")}});
        const std::map<RelicId,std::string> flags={{RelicId::CENTENNIAL_PUZZLE,"centennial_puzzle_used"},{RelicId::RED_SKULL,"red_skull_active"},{RelicId::NECRONOMICON,"necronomicon_used"},{RelicId::ORANGE_PELLETS,"orange_pellets_mask"}};
        auto f=flags.find(static_cast<RelicId>(id)); if(f!=flags.end() && !player.contains(f->second)) throw std::invalid_argument("missing relic flag: "+f->second);
    }
    s["potions"]=J::array(); for(const auto &v:game.at("potions")) s["potions"].push_back(potionId(identifier(v)));
    return s;
}
Imported importGame(const J &game) {
    auto snapshot=normalize(game); Imported out; out.battle=battleFromSnapshot(snapshot);
    if(out.battle.monsters.monstersAlive==0) throw std::invalid_argument("no living monster");
    int id=0; for(auto pile:{"hand","draw_pile","discard_pile","exhaust_pile"}) for(const auto &v:game.at("combat_state").at(pile)) out.uuids[id++]=v.at("uuid").get<std::string>();
    return out;
}
static J guardCard(const CardInstance &c,const std::map<int,std::string> &uuids) {
    J v={{"key",normalizedName(cardEnumStrings[static_cast<int>(c.id)])},{"cost",c.costForTurn},{"base_cost",c.cost},{"upgrades",c.getUpgradeCount()},{"free",c.freeToPlayOnce}};
    auto it=uuids.find(c.uniqueId); if(it!=uuids.end()) v["uuid"]=it->second;
    return v;
}
J guard(const BattleContext &b,const std::map<int,std::string> &uuids) {
    J g={{"turn",b.turn+1},{"energy",b.player.energy},{"hp",b.player.curHp},{"block",b.player.block},{"hand",J::array()},{"draw_pile",J::array()},{"discard_pile",J::array()},{"exhaust_pile",J::array()},{"monsters",J::array()}};
    for(int i=0;i<b.cards.cardsInHand;++i) g["hand"].push_back(guardCard(b.cards.hand[i],uuids));
    for(const auto &c:b.cards.drawPile) g["draw_pile"].push_back(guardCard(c,uuids));
    for(const auto &c:b.cards.discardPile) g["discard_pile"].push_back(guardCard(c,uuids));
    for(const auto &c:b.cards.exhaustPile) g["exhaust_pile"].push_back(guardCard(c,uuids));
    for(int i=0;i<b.monsters.monsterCount;++i) { const auto &m=b.monsters.arr[i]; g["monsters"].push_back(J{{"key",normalizedName(monsterIdStrings[static_cast<int>(m.id)])},{"hp",m.curHp},{"block",m.block},{"half_dead",m.halfDead}}); }
    g["max_hp"]=b.player.maxHp;g["is_bloodied"]=b.player.isBloodied;
    g["cards_played_this_turn"]=b.player.cardsPlayedThisTurn;g["attacks_played_this_turn"]=b.player.attacksPlayedThisTurn;
    g["skills_played_this_turn"]=b.player.skillsPlayedThisTurn;g["cards_discarded_this_turn"]=b.player.cardsDiscardedThisTurn;g["times_damaged"]=b.player.timesDamagedThisCombat;
    g["potions"]=J::array();for(int i=0;i<b.potionCapacity;++i)g["potions"].push_back(normalizedName(potionIds[static_cast<int>(b.potions[i])]));
    g["relic_counters"]={{"happyflower",b.player.happyFlowerCounter},{"incenseburner",b.player.incenseBurnerCounter},{"inkbottle",b.player.inkBottleCounter},{"inserter",b.player.inserterCounter},{"nunchaku",b.player.nunchakuCounter},{"pennib",b.player.penNibCounter},{"sundial",b.player.sundialCounter}};
    g["relic_flags"]={{"necronomicon_used",b.player.haveUsedNecronomiconThisTurn},{"orange_pellets_mask",b.player.orangePelletsCardTypesPlayed.to_ulong()},{"red_skull_active",b.player.redSkullActive},{"centennial_puzzle_used",!b.player.hasRelic<RelicId::CENTENNIAL_PUZZLE>()}};
    g["player_powers"]=J::object();
    for(int i=1;i<static_cast<int>(PlayerStatus::THE_BOMB);++i) {
        auto status=static_cast<PlayerStatus>(i);
        if(b.player.hasStatusRuntime(status)) g["player_powers"][normalizedName(playerStatusEnumStrings[i])]=b.player.getStatusRuntime(status);
    }
    g["bombs"]=J::array(); for(const auto &bomb:b.player.bombs) g["bombs"].push_back(J::array({bomb.turns,bomb.damage}));
    g["panache_counter"]=b.player.panacheCounter; g["combust_hp_loss"]=b.player.combustHpLoss;
    g["energy_per_turn"]=b.player.energyPerTurn;
    g["rngs"]=J::object();
    auto rng=[&](const char *name,const Random &r){g["rngs"][name]=J{{"seed0",std::to_string(r.seed0)},{"seed1",std::to_string(r.seed1)},{"counter",r.counter}};};
    rng("ai",b.aiRng);rng("card_random",b.cardRandomRng);rng("misc",b.miscRng);rng("monster_hp",b.monsterHpRng);rng("potion",b.potionRng);rng("shuffle",b.shuffleRng);
    for(int i=0;i<b.monsters.monsterCount;++i) {
        const auto &m=b.monsters.arr[i]; J powers=J::object();
        for(int j=0;j<static_cast<int>(MonsterStatus::INVALID);++j){auto status=static_cast<MonsterStatus>(j);if(status!=MonsterStatus::ASLEEP && status!=MonsterStatus::MINION_LEADER && m.hasStatusInternal(status))powers[normalizedName(monsterStatusEnumStrings[j])]=m.getStatusInternal(status);}
        g["monsters"][i]["powers"]=powers;
        g["monsters"][i]["move"]=monsterMoveStrings[static_cast<int>(m.moveHistory[0])];
    }
    return g;
}
J describe(const search::Action &a,const BattleContext &b,const std::map<int,std::string> &uuids) {
    J out={{"bits",a.bits},{"before",guard(b,uuids)}};
    using Type=search::ActionType;
    switch(a.getActionType()) {
        case Type::CARD: {
            int i=a.getSourceIdx(); const auto &c=b.cards.hand[i];
            out["type"]="play"; out["hand_index"]=i; out["card"]=guardCard(c,uuids); out["label"]=c.getName();
            out["target_index"]=c.requiresTarget()?a.getTargetIdx():-1; break;
        }
        case Type::END_TURN: out["type"]="end_turn"; out["label"]="End turn"; break;
        case Type::POTION:
            out["type"]="potion"; out["potion_index"]=a.getSourceIdx();
            out["discard"]=a.getTargetIdx()>5;
            out["target_index"]=potionRequiresTarget(b.potions[a.getSourceIdx()]) && a.getTargetIdx()<=5?a.getTargetIdx():-1;
            out["label"]=std::string(a.getTargetIdx()>5?"Discard ":"Use ")+potionNames[static_cast<int>(b.potions[a.getSourceIdx()])]+" (manual)"; break;
        default: {
            out["type"]="select";out["task"]=cardSelectTaskStrings[static_cast<int>(b.cardSelectInfo.cardSelectTask)];
            out["selected_indices"]=J::array();
            if(a.getActionType()==Type::SINGLE_CARD_SELECT)out["selected_indices"].push_back(a.getSelectIdx());
            else for(int i:a.getSelectedIdxs())out["selected_indices"].push_back(i);
            out["label"]=out["task"].get<std::string>()+" indices "+out["selected_indices"].dump()+" (manual, 0-based)"; break;
        }
    }
    return out;
}
}
