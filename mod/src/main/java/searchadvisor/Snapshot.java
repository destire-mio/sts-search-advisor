package searchadvisor;

import com.google.gson.Gson;
import com.google.gson.JsonObject;
import com.megacrit.cardcrawl.actions.GameActionManager;
import com.megacrit.cardcrawl.cards.AbstractCard;
import com.megacrit.cardcrawl.cards.CardGroup;
import com.megacrit.cardcrawl.characters.AbstractPlayer;
import com.megacrit.cardcrawl.core.AbstractCreature;
import com.megacrit.cardcrawl.core.Settings;
import com.megacrit.cardcrawl.dungeons.AbstractDungeon;
import com.megacrit.cardcrawl.monsters.AbstractMonster;
import com.megacrit.cardcrawl.powers.AbstractPower;
import com.megacrit.cardcrawl.rooms.AbstractRoom;
import com.megacrit.cardcrawl.ui.panels.EnergyPanel;
import java.lang.reflect.Field;
import java.util.*;

/** All game reads happen on the BaseMod update thread, never in EngineClient. */
public final class Snapshot {
    private static final Gson GSON = new Gson();
    private Snapshot() { }
    public static boolean inCombat() {
        return AbstractDungeon.player != null && AbstractDungeon.getCurrMapNode() != null
            && AbstractDungeon.getCurrRoom().phase == AbstractRoom.RoomPhase.COMBAT
            && !AbstractDungeon.player.isDead && !AbstractDungeon.player.isDying;
    }
    public static boolean ready() {
        if (!inCombat() || AbstractDungeon.isScreenUp) return false;
        GameActionManager a = AbstractDungeon.actionManager;
        AbstractPlayer p = AbstractDungeon.player;
        return a != null && a.phase == GameActionManager.Phase.WAITING_ON_USER
            && a.currentAction == null && a.actions.isEmpty() && a.preTurnActions.isEmpty()
            && p.limbo.group.isEmpty() && a.cardQueue.isEmpty() && a.monsterQueue.isEmpty() && !a.usingCard
            && !a.turnHasEnded && !p.isEndingTurn && !p.endTurnQueued && !p.isDraggingCard
            && AbstractDungeon.effectList.isEmpty() && AbstractDungeon.topLevelEffects.isEmpty();
    }
    private static Object optional(Object owner, String name) {
        for (Class<?> c = owner.getClass(); c != null; c = c.getSuperclass()) {
            try { Field f=c.getDeclaredField(name); f.setAccessible(true); return f.get(owner); }
            catch (NoSuchFieldException ignored) { }
            catch (ReflectiveOperationException e) { throw new IllegalStateException(e); }
        }
        return null;
    }
    private static Map<String,Object> card(AbstractCard c) {
        Map<String,Object> v = new LinkedHashMap<>();
        v.put("id",c.cardID); v.put("name",c.name); v.put("uuid",c.uuid.toString());
        v.put("cost",c.costForTurn); v.put("base_cost",c.cost); v.put("upgrades",c.timesUpgraded);
        v.put("free_to_play_once",c.freeToPlayOnce); v.put("misc",c.misc); v.put("base_damage",c.baseDamage);
        v.put("exhausts",c.exhaust); v.put("ethereal",c.isEthereal);
        return v;
    }
    private static List<Map<String,Object>> cards(CardGroup group) {
        List<Map<String,Object>> out=new ArrayList<>(); for(AbstractCard c:group.group) out.add(card(c)); return out;
    }
    private static List<Map<String,Object>> powers(AbstractCreature owner) {
        List<Map<String,Object>> out=new ArrayList<>();
        for(AbstractPower p:owner.powers) {
            Map<String,Object> v=new LinkedHashMap<>(); v.put("id",p.ID); v.put("amount",p.amount);
            Object just=optional(p,"justApplied"); if(just instanceof Boolean) v.put("just_applied",just);
            Object damage=optional(p,"damage"); if(damage instanceof Number) v.put("damage",damage);
            Object hpLoss=optional(p,"hpLoss"); if(hpLoss instanceof Number) v.put("misc",hpLoss);
            Object captured=optional(p,"card"); if(captured instanceof AbstractCard) v.put("card",card((AbstractCard)captured));
            out.add(v);
        }
        return out;
    }
    public static JsonObject capture() {
        if(!ready()) throw new IllegalStateException("等待玩家回合和动作结算");
        AbstractPlayer p=AbstractDungeon.player;
        if(p.chosenClass!=AbstractPlayer.PlayerClass.IRONCLAD) throw new IllegalStateException("原型支持铁甲战士");
        Map<String,Object> game=new LinkedHashMap<>();
        game.put("class","IRONCLAD"); game.put("screen_type","NONE"); game.put("seed",Long.toUnsignedString(Settings.seed));
        game.put("floor",AbstractDungeon.floorNum); game.put("ascension_level",AbstractDungeon.ascensionLevel); game.put("gold",p.gold);
        HashMap<String,Object> c=new HashMap<>(); Map<String,Object> player=new LinkedHashMap<>();
        player.put("current_hp",p.currentHealth); player.put("max_hp",p.maxHealth); player.put("block",p.currentBlock); player.put("energy",EnergyPanel.totalCount); player.put("powers",powers(p)); c.put("player",player);
        c.put("turn",GameActionManager.turn); c.put("times_damaged",p.damagedThisCombat);
        c.put("hand",cards(p.hand)); c.put("draw_pile",cards(p.drawPile)); c.put("discard_pile",cards(p.discardPile)); c.put("exhaust_pile",cards(p.exhaustPile));
        List<Map<String,Object>> monsters=new ArrayList<>();
        for(AbstractMonster m:AbstractDungeon.getCurrRoom().monsters.monsters) {
            Map<String,Object> v=new LinkedHashMap<>(); v.put("id",m.id); v.put("name",m.name); v.put("current_hp",m.currentHealth); v.put("max_hp",m.maxHealth); v.put("block",m.currentBlock); v.put("is_gone",m.isDeadOrEscaped()); v.put("half_dead",m.halfDead); v.put("powers",powers(m)); monsters.add(v);
        }
        c.put("monsters",monsters); CombatExtras.postfix(c); game.put("combat_state",c);
        List<Map<String,Object>> relics=new ArrayList<>();
        for(com.megacrit.cardcrawl.relics.AbstractRelic r:p.relics) { Map<String,Object> v=new LinkedHashMap<>(); v.put("id",r.relicId); v.put("counter",r.counter); relics.add(v); }
        game.put("relics",relics);
        List<Map<String,Object>> potions=new ArrayList<>();
        for(com.megacrit.cardcrawl.potions.AbstractPotion potion:p.potions) { Map<String,Object> v=new LinkedHashMap<>(); v.put("id",potion.ID); potions.add(v); }
        game.put("potions",potions);
        return GSON.toJsonTree(game).getAsJsonObject();
    }
    public static JsonObject captureForSearch(JsonObject game) {
        JsonObject out = new com.google.gson.JsonParser().parse(game.toString()).getAsJsonObject();
        out.add("full_rng_state",GSON.toJsonTree(FullRngState.capture()));
        return out;
    }
}
