package searchadvisor;
import com.google.gson.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Checks expected simulator preconditions against a fresh original snapshot. */
public final class Guard {
    private static final JsonObject ALIASES;
    static {
        try(Reader r=new InputStreamReader(Guard.class.getResourceAsStream("/guard-aliases.json"),StandardCharsets.UTF_8)) {ALIASES=new JsonParser().parse(r).getAsJsonObject();}
        catch(Exception e){throw new ExceptionInInitializerError(e);}
    }
    private Guard() { }
    private static String key(String id,String table) {
        if(id.equals("Potion Slot"))return "emptypotionid";
        JsonObject names=ALIASES.getAsJsonObject(table); if(names.has(id)) id=names.get(id).getAsString();
        return id.replaceAll("[^a-zA-Z0-9]","").toLowerCase(Locale.ROOT);
    }
    private static boolean equal(JsonObject a,String ak,JsonObject b,String bk){return a.has(ak)&&b.has(bk)&&a.get(ak).equals(b.get(bk));}
    private static JsonObject powers(JsonArray raw,boolean player) {
        JsonObject out=new JsonObject();
        for(JsonElement element:raw){
            JsonObject p=element.getAsJsonObject();String id=p.get("id").getAsString();
            if(player&&(id.equals("Berserk")||id.startsWith("TheBomb")))continue;
            if(!player&&(id.equals("Split")||id.equals("Explosive")||id.equals("Unawakened")||id.equals("BackAttack")))continue;
            String k=key(id,player?"PLAYER_POWER_ALIASES":"MONSTER_POWER_ALIASES");
            int amount=p.get("amount").getAsInt();
            if(amount<0&&!k.equals("strength")&&!k.equals("dexterity")&&!k.equals("focus"))amount=1;
            if(id.equals("Panache"))amount=p.get("damage").getAsInt();
            if(!player&&id.equals("Compulsive"))amount=0;
            if(!player&&id.equals("Stasis"))amount=1;
            if(amount!=0||(!player&&id.equals("Compulsive")))out.addProperty(k,amount);
        }
        return out;
    }
    private static String bits(JsonElement value){return new java.math.BigInteger(value.getAsString()).mod(java.math.BigInteger.ONE.shiftLeft(64)).toString();}
    public static boolean matches(JsonObject expected,JsonObject game) {
        try {
            JsonObject c=game.getAsJsonObject("combat_state"),p=c.getAsJsonObject("player");
            if(!equal(expected,"turn",c,"turn") || !equal(expected,"energy",p,"energy") || !equal(expected,"hp",p,"current_hp") || !equal(expected,"block",p,"block")) return false;
            if(!equal(expected,"max_hp",p,"max_hp")||!equal(expected,"is_bloodied",c,"is_bloodied"))return false;
            for(String counter:new String[]{"cards_played_this_turn","attacks_played_this_turn","skills_played_this_turn","cards_discarded_this_turn","times_damaged"})if(!equal(expected,counter,c,counter))return false;
            JsonArray potions=new JsonArray();for(JsonElement e:game.getAsJsonArray("potions"))potions.add(new JsonPrimitive(key(e.getAsJsonObject().get("id").getAsString(),"CARD_ALIASES")));
            if(!potions.equals(expected.get("potions")))return false;
            for(JsonElement e:game.getAsJsonArray("relics")){
                JsonObject relic=e.getAsJsonObject();String k=key(relic.get("id").getAsString(),"CARD_ALIASES");
                if(expected.getAsJsonObject("relic_counters").has(k)&&!equal(expected.getAsJsonObject("relic_counters"),k,relic,"counter"))return false;
            }
            for(java.util.Map.Entry<String,JsonElement> flag:c.getAsJsonObject("relic_combat_state").entrySet()){
                if(expected.getAsJsonObject("relic_flags").has(flag.getKey())&&!flag.getValue().equals(expected.getAsJsonObject("relic_flags").get(flag.getKey())))return false;
            }
            for(String pile:new String[]{"hand","draw_pile","discard_pile","exhaust_pile"}) {
                JsonArray want=expected.getAsJsonArray(pile),actual=c.getAsJsonArray(pile);
                if(want.size()!=actual.size())return false;
                for(int i=0;i<want.size();i++) {
                    JsonObject w=want.get(i).getAsJsonObject(),a=actual.get(i).getAsJsonObject();
                    if(!w.get("key").getAsString().equals(key(a.get("id").getAsString(),"CARD_ALIASES")) || !equal(w,"cost",a,"cost") || !equal(w,"base_cost",a,"base_cost") || !equal(w,"upgrades",a,"upgrades") || !equal(w,"free",a,"free_to_play_once"))return false;
                    if(w.has("uuid") && !equal(w,"uuid",a,"uuid"))return false;
                }
            }
            if(!expected.getAsJsonObject("player_powers").equals(powers(p.getAsJsonArray("powers"),true)))return false;
            JsonArray bombs=new JsonArray();int energyPerTurn=c.get("energy_per_turn").getAsInt();
            for(JsonElement element:p.getAsJsonArray("powers")){
                JsonObject power=element.getAsJsonObject();String id=power.get("id").getAsString();
                if(id.startsWith("TheBomb")){JsonArray b=new JsonArray();b.add(power.get("amount"));b.add(power.get("damage"));bombs.add(b);}
                if(id.equals("Berserk"))energyPerTurn+=power.get("amount").getAsInt();
                if(id.equals("Panache")&&!equal(expected,"panache_counter",power,"amount"))return false;
                if(id.equals("Combust")&&!equal(expected,"combust_hp_loss",power,"misc"))return false;
            }
            if(!bombs.equals(expected.get("bombs"))||energyPerTurn!=expected.get("energy_per_turn").getAsInt())return false;
            for(String name:new String[]{"ai","card_random","misc","monster_hp","potion","shuffle"}){
                JsonObject wantRng=expected.getAsJsonObject("rngs").getAsJsonObject(name),actualRng=c.getAsJsonObject("rngs").getAsJsonObject(name);
                if(!equal(wantRng,"counter",actualRng,"counter")||!bits(wantRng.get("seed0")).equals(bits(actualRng.get("seed0")))||!bits(wantRng.get("seed1")).equals(bits(actualRng.get("seed1"))))return false;
            }
            JsonArray want=expected.getAsJsonArray("monsters"),actual=c.getAsJsonArray("monsters");
            if(want.size()!=actual.size())return false;
            for(int i=0;i<want.size();i++) {
                JsonObject w=want.get(i).getAsJsonObject(),a=actual.get(i).getAsJsonObject();
                if(!w.getAsJsonObject("powers").equals(powers(a.getAsJsonArray("powers"),false)))return false;
                int rawMove=a.has("move_id")?a.get("move_id").getAsInt():-1;
                String monsterName=a.get("id").getAsString();JsonObject aliases=ALIASES.getAsJsonObject("MONSTER_ALIASES");if(aliases.has(monsterName))monsterName=aliases.get(monsterName).getAsString();
                String moveKey=null;
                for(java.util.Map.Entry<String,JsonElement> entry:ALIASES.getAsJsonObject("MOVE_NAMES").entrySet()){
                    String[] pair=entry.getKey().split(":");if(key(pair[0],"MONSTER_ALIASES").equals(key(monsterName,"MONSTER_ALIASES"))&&Integer.parseInt(pair[1])==rawMove){moveKey=entry.getValue().getAsString();break;}
                }
                if(rawMove<0)moveKey="INVALID";
                if(!w.get("move").getAsString().equals(moveKey))return false;
                if(!w.get("key").getAsString().equals(key(a.get("id").getAsString(),"MONSTER_ALIASES")) || !equal(w,"hp",a,"current_hp") || !equal(w,"block",a,"block") || !equal(w,"half_dead",a,"half_dead"))return false;
            }
            return true;
        } catch(RuntimeException e){return false;}
    }
}
