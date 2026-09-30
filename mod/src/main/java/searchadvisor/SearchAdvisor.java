package searchadvisor;
import basemod.BaseMod;
import basemod.interfaces.PostUpdateSubscriber;
import basemod.interfaces.RenderSubscriber;
import com.evacipated.cardcrawl.modthespire.lib.SpireInitializer;
import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.google.gson.*;
import com.megacrit.cardcrawl.cards.AbstractCard;
import com.megacrit.cardcrawl.cards.CardQueueItem;
import com.megacrit.cardcrawl.core.Settings;
import com.megacrit.cardcrawl.dungeons.AbstractDungeon;
import com.megacrit.cardcrawl.helpers.*;
import com.megacrit.cardcrawl.helpers.input.InputHelper;
import com.megacrit.cardcrawl.monsters.AbstractMonster;
import com.megacrit.cardcrawl.ui.panels.EnergyPanel;

@SpireInitializer
public final class SearchAdvisor implements PostUpdateSubscriber,RenderSubscriber {
    private final AdvisorConfig config=AdvisorConfig.load();
    private final EngineClient client=new EngineClient();
    private JsonArray plan;
    private JsonObject requestState,lastRequested;
    private EngineClient.Result pending;
    private String token,message="搜索建议可能与原版规则有出入";
    private boolean auto=false,replan=false,highlight=false,completeTurn=false;
    private int next=0,page=0;
    private long serial=0,notBefore=0;
    private Object room,player;
    private final Hitbox[] buttons={new Hitbox(110,34),new Hitbox(130,34),new Hitbox(90,34),new Hitbox(80,34)};
    public static void initialize(){new SearchAdvisor();}
    public SearchAdvisor(){BaseMod.subscribe(this);if(config.error!=null)message=config.error;Runtime.getRuntime().addShutdownHook(new Thread(client::close,"SearchAdvisor-shutdown"));}
    private void invalidate(){token=null;pending=null;plan=null;next=0;highlight=false;client.cancel();}
    private void stop(){auto=false;replan=false;invalidate();message="已停止；队列中的一张牌由游戏完成结算";}
    private void request(boolean explicit) {
        if(!Snapshot.ready()){message="等待玩家回合和动作结算";return;}
        try {
            JsonObject snapshot=Snapshot.capture(),comparable=Protocol.comparable(snapshot);
            if(!explicit && comparable.equals(lastRequested)){replan=false;message="相同状态未重试；请手动触发搜索";return;}
            if(client.busy()){invalidate();replan=true;return;}
            invalidate(); token="state-"+(++serial);requestState=comparable;lastRequested=comparable;
            plan=null;next=0;page=0;replan=false;
            client.search(config.engine,token,Snapshot.captureForSearch(snapshot),config.simulations,config.timeoutMs);
            message="搜索中（"+config.simulations+" 次模拟）";
        }catch(Exception e){auto=false;replan=false;invalidate();message="无法导出: "+e.getMessage();}
    }
    private void changed(){auto=false;invalidate();replan=true;message="状态改变，停止自动出牌并重新搜索";}
    public void receivePostUpdate() {
        try { update(); } catch(Exception e){auto=false;replan=false;invalidate();message="SearchAdvisor: "+e.getMessage();}
    }
    private void update() {
        highlight=false;
        if(!Snapshot.inCombat()){if(room!=null){stop();room=null;player=null;}return;}
        if(room!=AbstractDungeon.getCurrRoom()||player!=AbstractDungeon.player){stop();room=AbstractDungeon.getCurrRoom();player=AbstractDungeon.player;lastRequested=null;message="搜索建议可能与原版规则有出入";}
        float scale=Settings.scale, y=Settings.HEIGHT-135*scale;
        for(int i=0;i<buttons.length;i++){Hitbox h=buttons[i];h.resize((i==1?130:i==2?90:i==3?80:110)*scale,34*scale);h.move((75+i*140)*scale,y);h.update();}
        boolean stop=Gdx.input.isKeyJustPressed(config.stopKey)||clicked(2);
        if(stop){stop();return;}
        if(Gdx.input.isKeyJustPressed(config.autoKey)||clicked(1)){auto=!auto;message=auto?"自动出牌开启；停止键随时中断":"仅提示";}
        if(clicked(3)&&plan!=null){page=(page+1)%Math.max(1,(plan.size()+7)/8);}
        if(Gdx.input.isKeyJustPressed(config.searchKey)||clicked(0))request(true);
        EngineClient.Result r;while((r=client.poll())!=null)if(r.token.equals(token))pending=r;
        if(!Snapshot.ready())return;
        if(replan&&!client.busy()){request(false);return;}
        if(pending!=null){
            r=pending;pending=null;
            if(r.error!=null){auto=false;message=r.error;token=null;return;}
            if(!requestState.equals(Protocol.comparable(Snapshot.capture()))){changed();return;}
            plan=r.value.getAsJsonArray("actions");next=0;page=0;completeTurn=r.value.get("complete_turn").getAsBoolean();
            message=(completeTurn?"本回合 ":"回合前缀 ")+plan.size()+" 步；规则一致性 INCOMPLETE";
            if(plan.size()==0){auto=false;message="预算内没有可执行建议";return;}
        }
        if(plan==null||next>=plan.size())return;
        if(System.nanoTime()<notBefore)return;
        JsonObject action=plan.get(next).getAsJsonObject();
        if(!Guard.matches(action.getAsJsonObject("before"),Snapshot.capture())){changed();return;}
        highlight=true;
        if(!auto)return;
        if(!"play".equals(action.get("type").getAsString())){auto=false;message="后续步骤需手动操作："+action.get("label").getAsString();return;}
        int index=action.get("hand_index").getAsInt(),target=action.get("target_index").getAsInt();
        AbstractCard card=AbstractDungeon.player.hand.group.get(index);
        AbstractMonster monster=target<0?null:AbstractDungeon.getCurrRoom().monsters.monsters.get(target);
        if(monster!=null&&(monster.isDeadOrEscaped()||monster.halfDead||monster.isDying)){changed();return;}
        if(!card.canUse(AbstractDungeon.player,monster)){changed();return;}
        // Matching hand/energy/target checked this frame. Queue just one card;
        // never call use() directly or hold game objects on the search thread.
        if(monster!=null&&AbstractDungeon.player.hasPower("Surrounded"))AbstractDungeon.player.flipHorizontal=monster.drawX<AbstractDungeon.player.drawX;
        AbstractDungeon.actionManager.addCardQueueItem(new CardQueueItem(card,monster,EnergyPanel.totalCount),true);
        next++;page=next/8;highlight=false;notBefore=System.nanoTime()+200000000L;
        if(next>=plan.size()){auto=false;message=completeTurn?"建议出牌完成":"建议前缀结束，需要重新搜索";}
    }
    private boolean clicked(int i){if(buttons[i].hovered&&InputHelper.justClickedLeft){InputHelper.justClickedLeft=false;return true;}return false;}
    private static void text(SpriteBatch sb,String value,float x,float y,Color color){FontHelper.renderFontLeftTopAligned(sb,FontHelper.tipBodyFont,value,x,y,color);}
    private static void outline(SpriteBatch sb,Hitbox h){float w=3*Settings.scale;Color old=new Color(sb.getColor());sb.setColor(Color.GOLD);sb.draw(ImageMaster.WHITE_SQUARE_IMG,h.x,h.y,h.width,w);sb.draw(ImageMaster.WHITE_SQUARE_IMG,h.x,h.y+h.height-w,h.width,w);sb.draw(ImageMaster.WHITE_SQUARE_IMG,h.x,h.y,w,h.height);sb.draw(ImageMaster.WHITE_SQUARE_IMG,h.x+h.width-w,h.y,w,h.height);sb.setColor(old);}
    public void receiveRender(SpriteBatch sb) {
        if(!Snapshot.inCombat())return;
        float s=Settings.scale,y=Settings.HEIGHT-160*s;
        String[] labels={"搜索","自动: "+(auto?"开":"关"),"停止","翻页"};
        for(int i=0;i<buttons.length;i++){Hitbox h=buttons[i];text(sb,"["+labels[i]+"]",h.x,h.y+h.height, h.hovered?Color.GOLD:Color.WHITE);}
        text(sb,"SearchAdvisor  "+com.badlogic.gdx.Input.Keys.toString(config.searchKey)+" 搜索 / "+com.badlogic.gdx.Input.Keys.toString(config.autoKey)+" 自动 / "+com.badlogic.gdx.Input.Keys.toString(config.stopKey)+" 停止",20*s,y,Color.GOLD);
        String status=message.length()>85?message.substring(0,85)+"…":message;text(sb,status,20*s,y-30*s,Color.WHITE);
        if(plan==null)return;
        for(int i=page*8;i<Math.min(plan.size(),page*8+8);i++){
            JsonObject a=plan.get(i).getAsJsonObject();String label=a.get("label").getAsString();
            if(a.has("target_index")&&a.get("target_index").getAsInt()>=0)label+=" → #"+(a.get("target_index").getAsInt()+1);
            text(sb,(i==next?"> ":"  ")+(i+1)+". "+label,20*s,y-(65+(i-page*8)*27)*s,i==next?Color.GOLD:Color.WHITE);
        }
        if(highlight&&next<plan.size()){
            JsonObject a=plan.get(next).getAsJsonObject();
            if("play".equals(a.get("type").getAsString())){
                int i=a.get("hand_index").getAsInt(),t=a.get("target_index").getAsInt();
                if(i<AbstractDungeon.player.hand.group.size())outline(sb,AbstractDungeon.player.hand.group.get(i).hb);
                if(t>=0&&t<AbstractDungeon.getCurrRoom().monsters.monsters.size())outline(sb,AbstractDungeon.getCurrRoom().monsters.monsters.get(t).hb);
            }
        }
    }
}
