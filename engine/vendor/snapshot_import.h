// Adapted from combat4r bindings/slaythespire.cpp (see PROVENANCE.json).
// JSON transport replaces pybind11; exact RNG streams are mandatory.
#pragma once
#include "combat/BattleContext.h"
#include "constants/MonsterMoves.h"
#include "constants/PlayerStatusEffects.h"
#include "constants/MonsterStatusEffects.h"
#include <nlohmann/json.hpp>
#include <cctype>
#include <stdexcept>
#include <type_traits>
using namespace sts;
using J = nlohmann::json;
template<class T> T cast(const J &v) {
    if constexpr (std::is_same_v<T, std::uint64_t>) {
        if (v.is_string()) { auto s=v.get<std::string>(); std::size_t n=0; auto x=std::stoull(s,&n); if(n!=s.size()) throw std::invalid_argument("invalid uint64"); return x; }
    }
    return v.get<T>();
}
    std::string normalizedName(const std::string &value) {
        std::string out;
        for (const unsigned char ch : value) {
            if (std::isalnum(ch)) out.push_back(static_cast<char>(std::tolower(ch)));
        }
        return out;
    }

    template <typename T, std::size_t N>
    int enumFromNames(const std::string &name, const T (&names)[N]) {
        const auto wanted = normalizedName(name);
        for (std::size_t i = 0; i < N; ++i) {
            if (normalizedName(names[i]) == wanted) return static_cast<int>(i);
        }
        throw std::invalid_argument("unknown enum name: " + name);
    }

    CardInstance snapshotCard(const J &src, int uniqueId) {
        const auto id = static_cast<CardId>(cast<int>(src.at("id")));
        if (id == CardId::INVALID) throw std::invalid_argument("snapshot card id is INVALID");
        const int upgrades = src.contains("upgrades") ? cast<int>(src.at("upgrades")) : 0;
        CardInstance card(id, upgrades > 0);
        card.uniqueId = static_cast<std::int16_t>(uniqueId);
        if (id == CardId::SEARING_BLOW) card.specialData = static_cast<std::int16_t>(upgrades);
        else if (src.contains("misc")) card.specialData = static_cast<std::int16_t>(cast<int>(src.at("misc")));
        if (src.contains("cost")) card.costForTurn = static_cast<std::int8_t>(cast<int>(src.at("cost")));
        if (src.contains("base_cost")) card.cost = cast<int>(src.at("base_cost"));
        if (src.contains("free_to_play_once")) card.freeToPlayOnce = cast<bool>(src.at("free_to_play_once"));
        return card;
    }

    void setPlayerStatus(Player &player, PlayerStatus status, int amount, bool justApplied) {
        switch (status) {
            case PlayerStatus::ARTIFACT: player.artifact = amount; break;
            case PlayerStatus::DEXTERITY: player.dexterity = amount; break;
            case PlayerStatus::FOCUS: player.focus = amount; break;
            case PlayerStatus::STRENGTH: player.strength = amount; break;
            default: {
                const int idx = static_cast<int>(status);
                if (idx <= 0 || idx >= static_cast<int>(PlayerStatus::THE_BOMB) + 1) {
                    throw std::invalid_argument("invalid player status id");
                }
                if (idx < 64) player.statusBits0 |= 1ULL << idx;
                else player.statusBits1 |= 1ULL << (idx - 64);
                player.statusMap[status] = static_cast<std::int16_t>(amount);
            }
        }
        player.updatePowerOrder(status, player.hasStatusRuntime(status));
        if (justApplied) player.justAppliedBits |= 1ULL << static_cast<int>(status);
    }

    void setMonsterStatus(Monster &monster, MonsterStatus status, int amount) {
        const int idx = static_cast<int>(status);
        if (idx < 0 || status == MonsterStatus::INVALID) throw std::invalid_argument("invalid monster status id");
        monster.statusBits |= 1ULL << idx;
        switch (status) {
            case MonsterStatus::ARTIFACT: monster.artifact = amount; break;
            case MonsterStatus::BLOCK_RETURN: monster.blockReturn = amount; break;
            case MonsterStatus::CHOKED: monster.choked = amount; break;
            case MonsterStatus::CORPSE_EXPLOSION: monster.corpseExplosion = amount; break;
            case MonsterStatus::LOCK_ON: monster.lockOn = amount; break;
            case MonsterStatus::MARK: monster.mark = amount; break;
            case MonsterStatus::METALLICIZE: monster.metallicize = amount; break;
            case MonsterStatus::PLATED_ARMOR: monster.platedArmor = amount; break;
            case MonsterStatus::POISON: monster.poison = amount; break;
            case MonsterStatus::REGEN: monster.regen = amount; break;
            case MonsterStatus::SHACKLED: monster.shackled = amount; break;
            case MonsterStatus::STRENGTH: monster.strength = amount; break;
            case MonsterStatus::VULNERABLE: monster.vulnerable = amount; break;
            case MonsterStatus::WEAK: monster.weak = amount; break;
            case MonsterStatus::ANGRY:
            case MonsterStatus::BEAT_OF_DEATH:
            case MonsterStatus::CURIOSITY:
            case MonsterStatus::CURL_UP:
            case MonsterStatus::ENRAGE:
            case MonsterStatus::FADING:
            case MonsterStatus::FLIGHT:
            case MonsterStatus::GENERIC_STRENGTH_UP:
            case MonsterStatus::INTANGIBLE:
            case MonsterStatus::MALLEABLE:
            case MonsterStatus::MODE_SHIFT:
            case MonsterStatus::RITUAL:
            case MonsterStatus::SLOW:
            case MonsterStatus::SPORE_CLOUD:
            case MonsterStatus::THIEVERY:
            case MonsterStatus::THORNS:
            case MonsterStatus::TIME_WARP: monster.uniquePower0 = amount; break;
            case MonsterStatus::INVINCIBLE:
            case MonsterStatus::REACTIVE:
            case MonsterStatus::SHARP_HIDE: monster.uniquePower1 = amount; break;
            default: break;
        }
    }

    Random randomFromSnapshot(const J &src) {
        Random rng;
        rng.seed0 = cast<std::uint64_t>(src.at("seed0"));
        rng.seed1 = cast<std::uint64_t>(src.at("seed1"));
        rng.counter = cast<std::int32_t>(src.at("counter"));
        return rng;
    }

    EndTurnShuffleState endTurnShuffleFromSnapshot(const J &src) {
        EndTurnShuffleState value;
        const auto mode = cast<std::string>(src.at("mode"));
        if (mode == "basemod_seeded") value.mode = EndTurnShuffleMode::BASEMOD_SEEDED;
        else if (mode == "java_shared") value.mode = EndTurnShuffleMode::JAVA_SHARED;
        else throw std::invalid_argument("unknown end-turn shuffle mode");
        if (!src.at("shared_rng_initialized").is_boolean())
            throw std::invalid_argument("shared_rng_initialized must be boolean");
        value.sharedRngInitialized = cast<bool>(src.at("shared_rng_initialized"));
        if (value.sharedRngInitialized) {
            if (!src.at("shared_seed48").is_number_integer() || src.at("shared_seed48").is_boolean())
                throw std::invalid_argument("shared_seed48 must be an integer");
            value.sharedRng.setRawSeed(cast<std::uint64_t>(src.at("shared_seed48")));
        }
        else if (src.contains("shared_seed48"))
            throw std::invalid_argument("uninitialized shared RNG cannot have a seed");
        return value;
    }

    J endTurnShuffleSnapshot(const EndTurnShuffleState &value) {
        J result = J::object();
        result["mode"] = value.mode == EndTurnShuffleMode::BASEMOD_SEEDED ? "basemod_seeded" : "java_shared";
        result["shared_rng_initialized"] = value.sharedRngInitialized;
        if (value.sharedRngInitialized) result["shared_seed48"] = value.sharedRng.rawSeed();
        return result;
    }

    void setRelicCounter(Player &player, RelicId id, int counter) {
        switch (id) {
            case RelicId::HAPPY_FLOWER: player.happyFlowerCounter = counter; break;
            case RelicId::INCENSE_BURNER: player.incenseBurnerCounter = counter; break;
            case RelicId::INK_BOTTLE: player.inkBottleCounter = counter; break;
            case RelicId::INSERTER: player.inserterCounter = counter; break;
            case RelicId::NUNCHAKU: player.nunchakuCounter = counter; break;
            case RelicId::PEN_NIB: player.penNibCounter = counter; break;
            case RelicId::SUNDIAL: player.sundialCounter = counter; break;
            default: break;
        }
    }

    BattleContext battleFromSnapshot(const J &snapshot) {
        BattleContext bc;
        bc.seed = cast<std::uint64_t>(snapshot.at("seed"));
        bc.floorNum = cast<int>(snapshot.at("floor"));
        bc.ascension = snapshot.contains("ascension") ? cast<int>(snapshot.at("ascension")) : 0;
        bc.encounter = snapshot.contains("encounter")
            ? static_cast<MonsterEncounter>(cast<int>(snapshot.at("encounter")))
            : MonsterEncounter::INVALID;
        bc.turn = std::max(0, cast<int>(snapshot.at("turn")) - 1);
        if (snapshot.contains("end_turn_shuffle"))
            bc.endTurnShuffle = endTurnShuffleFromSnapshot(cast<J>(snapshot.at("end_turn_shuffle")));
        if (snapshot.contains("frame_delta_seconds")) bc.actionFrameDelta = cast<float>(snapshot.at("frame_delta_seconds"));
        bc.outcome = Outcome::UNDECIDED;
        bc.inputState = InputState::PLAYER_NORMAL;
        bc.isBattleOver = false;
        bc.endTurnQueued = false;
        bc.turnHasEnded = false;
        bc.skipMonsterTurn = false;
        bc.monsterTurnIdx = 6;
        bc.actionQueue.clear();
        bc.cardQueue.clear();
        bc.miscBits.reset();

        if (snapshot.contains("rngs")) {
            const auto rngs = cast<J>(snapshot.at("rngs"));
            bc.aiRng = randomFromSnapshot(cast<J>(rngs.at("ai")));
            bc.cardRandomRng = randomFromSnapshot(cast<J>(rngs.at("card_random")));
            bc.miscRng = randomFromSnapshot(cast<J>(rngs.at("misc")));
            bc.monsterHpRng = randomFromSnapshot(cast<J>(rngs.at("monster_hp")));
            bc.potionRng = randomFromSnapshot(cast<J>(rngs.at("potion")));
            bc.shuffleRng = randomFromSnapshot(cast<J>(rngs.at("shuffle")));
        } else { throw std::invalid_argument("missing exact RNG states"); }

        bc.player = Player();
        const auto player = cast<J>(snapshot.at("player"));
        bc.player.cc = CharacterClass::IRONCLAD;
        bc.player.curHp = cast<int>(player.at("current_hp"));
        bc.player.maxHp = cast<int>(player.at("max_hp"));
        // Legacy snapshots lack these flags; current exporters preserve history.
        bc.player.isBloodied = player.contains("is_bloodied") ? cast<bool>(player.at("is_bloodied"))
            : bc.player.curHp <= bc.player.maxHp / 2;
        bc.player.block = cast<int>(player.at("block"));
        bc.player.energy = cast<int>(player.at("energy"));
        bc.player.gold = player.contains("gold") ? cast<int>(player.at("gold")) : 0;
        bc.player.haveUsedNecronomiconThisTurn = player.contains("necronomicon_used") && cast<bool>(player.at("necronomicon_used"));
        bc.player.orangePelletsCardTypesPlayed = player.contains("orange_pellets_mask") ? cast<unsigned>(player.at("orange_pellets_mask")) : 0;
        if (player.contains("last_targeted_monster")) bc.player.lastTargetedMonster = cast<int>(player.at("last_targeted_monster"));
        bc.player.energyPerTurn = static_cast<std::int8_t>(player.contains("energy_per_turn") ? cast<int>(player.at("energy_per_turn")) : 3);
        bc.player.cardDrawPerTurn = player.contains("card_draw_per_turn") ? cast<int>(player.at("card_draw_per_turn")) : 5;
        bc.player.cardsPlayedThisTurn = snapshot.contains("cards_played_this_turn") ? cast<int>(snapshot.at("cards_played_this_turn")) : 0;
        bc.player.attacksPlayedThisTurn = snapshot.contains("attacks_played_this_turn") ? cast<int>(snapshot.at("attacks_played_this_turn")) : 0;
        bc.player.skillsPlayedThisTurn = snapshot.contains("skills_played_this_turn") ? cast<int>(snapshot.at("skills_played_this_turn")) : 0;
        bc.player.cardsDiscardedThisTurn = snapshot.contains("cards_discarded_this_turn") ? cast<int>(snapshot.at("cards_discarded_this_turn")) : 0;
        bc.player.timesDamagedThisCombat = snapshot.contains("times_damaged") ? cast<int>(snapshot.at("times_damaged")) : 0;
        if (player.contains("powers")) {
            for (const auto item : cast<J>(player.at("powers"))) {
                const auto power = cast<J>(item);
                const auto status = static_cast<PlayerStatus>(cast<int>(power.at("id")));
                const int amount = cast<int>(power.at("amount"));
                if (status == PlayerStatus::PANACHE) bc.player.panacheCounter = power.contains("counter") ? cast<int>(power.at("counter")) : 5;
                if (status == PlayerStatus::COMBUST) bc.player.combustHpLoss = power.contains("misc") ? cast<int>(power.at("misc")) : 1;
                if (status == PlayerStatus::THE_BOMB) {
                    if (!power.contains("bomb_turns")) throw std::invalid_argument("Bomb snapshot requires remaining turns");
                    const int turns = cast<int>(power.at("bomb_turns"));
                    if (turns <= 0) throw std::invalid_argument("Bomb snapshot requires positive remaining turns");
                    bc.player.addBomb(amount, turns);
                } else {
                    setPlayerStatus(bc.player, status, amount,
                        power.contains("just_applied") && cast<bool>(power.at("just_applied")));
                }
            }
        }

        bc.cards = CardManager();
        int uniqueId = 0;
        const auto loadCards = [&](const char *key, auto &target, auto notify) {
            for (const auto item : cast<J>(snapshot.at(key))) {
                auto card = snapshotCard(cast<J>(item), uniqueId++);
                bc.cards.notifyAddCardToCombat(card);
                (bc.cards.*notify)(card);
                target.push_back(card);
            }
        };
        for (const auto item : cast<J>(snapshot.at("hand"))) {
            if (bc.cards.cardsInHand >= CardManager::MAX_HAND_SIZE) throw std::invalid_argument("snapshot hand exceeds simulator capacity");
            auto card = snapshotCard(cast<J>(item), uniqueId++);
            bc.cards.notifyAddCardToCombat(card);
            bc.cards.notifyAddToHand(card);
            bc.cards.hand[bc.cards.cardsInHand++] = card;
        }
        loadCards("draw_pile", bc.cards.drawPile, &CardManager::notifyAddToDrawPile);
        loadCards("discard_pile", bc.cards.discardPile, &CardManager::notifyAddToDiscardPile);
        for (const auto item : cast<J>(snapshot.at("exhaust_pile"))) {
            auto card = snapshotCard(cast<J>(item), uniqueId++);
            bc.cards.notifyAddCardToCombat(card);
            bc.cards.exhaustPile.push_back(card);
        }
        bc.cards.nextUniqueCardId = uniqueId;

        bc.monsters = MonsterGroup();
        const auto monsters = cast<J>(snapshot.at("monsters"));
        if (monsters.size() > bc.monsters.arr.size()) throw std::invalid_argument("snapshot monster count exceeds simulator capacity");
        bc.monsters.monsterCount = static_cast<int>(monsters.size());
        bc.monsters.monstersAlive = 0;
        for (int i = 0; i < bc.monsters.monsterCount; ++i) {
            const auto src = cast<J>(monsters[i]);
            auto &monster = bc.monsters.arr[i];
            monster = Monster();
            monster.idx = i;
            monster.id = static_cast<MonsterId>(cast<int>(src.at("id")));
            if (monster.id == MonsterId::INVALID) throw std::invalid_argument("snapshot monster id is INVALID");
            monster.curHp = cast<int>(src.at("current_hp"));
            monster.maxHp = cast<int>(src.at("max_hp"));
            monster.block = cast<int>(src.at("block"));
            monster.halfDead = src.contains("half_dead") && cast<bool>(src.at("half_dead"));
            monster.isEscapingB = !monster.halfDead && src.contains("is_gone") && cast<bool>(src.at("is_gone"));
            monster.moveHistory[0] = static_cast<MMID>(cast<int>(src.at("move")));
            monster.moveHistory[1] = src.contains("last_move")
                ? static_cast<MMID>(cast<int>(src.at("last_move"))) : MMID::INVALID;
            monster.miscInfo = src.contains("misc_info") ? cast<int>(src.at("misc_info")) : 0;
            if (src.contains("unique_power0")) monster.uniquePower0 = cast<int>(src.at("unique_power0"));
            if (src.contains("powers")) {
                for (const auto item : cast<J>(src.at("powers"))) {
                    const auto power = cast<J>(item);
                    setMonsterStatus(monster,
                        static_cast<MonsterStatus>(cast<int>(power.at("id"))),
                        cast<int>(power.at("amount")));
                    if (power.contains("just_applied") && cast<bool>(power.at("just_applied"))) {
                        switch (static_cast<MonsterStatus>(cast<int>(power.at("id")))) {
                            case MonsterStatus::VULNERABLE: monster.setJustApplied<MonsterStatus::VULNERABLE>(true); break;
                            case MonsterStatus::WEAK: monster.setJustApplied<MonsterStatus::WEAK>(true); break;
                            case MonsterStatus::RITUAL: monster.setJustApplied<MonsterStatus::RITUAL>(true); break;
                            default: break;
                        }
                    }
                    if (cast<int>(power.at("id")) == static_cast<int>(MonsterStatus::STASIS)) {
                        if (!power.contains("card")) throw std::invalid_argument("Stasis snapshot requires captured card");
                        bc.cards.stasisCards[std::min(i, 1)] = snapshotCard(cast<J>(power.at("card")), bc.cards.nextUniqueCardId++);
                    }
                }
            }
            if (monster.id==MonsterId::AWAKENED_ONE && monster.miscInfo && !monster.halfDead)
                monster.buff<MonsterStatus::MINION_LEADER>();
            if (!monster.isEscapingB && monster.curHp > 0) ++bc.monsters.monstersAlive;
        }

        bc.potionCapacity = 0;
        bc.potionCount = 0;
        if (snapshot.contains("potions")) {
            const auto potions = cast<J>(snapshot.at("potions"));
            if (potions.size() > bc.potions.size()) throw std::invalid_argument("snapshot potion count exceeds simulator capacity");
            bc.potionCapacity = static_cast<int>(potions.size());
            for (int i = 0; i < bc.potionCapacity; ++i) {
                bc.potions[i] = static_cast<Potion>(cast<int>(potions[i]));
                if (bc.potions[i] != Potion::EMPTY_POTION_SLOT) ++bc.potionCount;
            }
        }
        if (snapshot.contains("relics")) {
            for (const auto item : cast<J>(snapshot.at("relics"))) {
                const auto relic = cast<J>(item);
                const int id = cast<int>(relic.at("id"));
                bc.player.updateCardUseRelic(static_cast<RelicId>(id), true);
                bc.player.updateHpLossDrawRelic(static_cast<RelicId>(id), true);
                if (id < 64) bc.player.relicBits0 |= 1ULL << id;
                else if (id < 128) bc.player.relicBits1 |= 1ULL << (id - 64);
                if (static_cast<RelicId>(id) == RelicId::LIZARD_TAIL) {
                    const int counter = relic.contains("counter") ? cast<int>(relic.at("counter")) : -1;
                    bc.player.setHasRelic<RelicId::LIZARD_TAIL>(counter == -1);
                }
                if (relic.contains("counter")) {
                    const int counter = cast<int>(relic.at("counter"));
                    if (counter >= 0) setRelicCounter(bc.player, static_cast<RelicId>(id), counter);
                }
            }
        }
        if (bc.player.hasRelic<RelicId::CENTENNIAL_PUZZLE>()) {
            if (player.contains("centennial_puzzle_used")) {
                if (!player.at("centennial_puzzle_used").is_boolean())
                    throw std::invalid_argument("Centennial Puzzle snapshot requires a boolean centennial_puzzle_used");
                if (cast<bool>(player.at("centennial_puzzle_used")))
                    bc.player.setHasRelic<RelicId::CENTENNIAL_PUZZLE>(false);
            } else if (bc.player.timesDamagedThisCombat > 0) {
                throw std::invalid_argument("Centennial Puzzle snapshot requires centennial_puzzle_used after HP loss");
            }
        }
        bc.player.redSkullActive = player.contains("red_skull_active") ? cast<bool>(player.at("red_skull_active"))
            : bc.player.hasRelic<RelicId::RED_SKULL>() && bc.player.isBloodied;
        return bc;
    }
