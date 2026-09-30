import searchadvisor.*;
import com.google.gson.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;

/** A plain JVM entry point; does not initialize CardCrawlGame or any window. */
public final class ProtocolTest {
    static int checks;
    static void check(boolean value,String message){checks++;if(!value)throw new AssertionError(message);}
    static JsonObject clone(JsonObject x){return new JsonParser().parse(x.toString()).getAsJsonObject();}
    interface Throwing {void run() throws Exception;}
    static void rejects(Throwing x,String name)throws Exception{boolean failed=false;try{x.run();}catch(IOException e){failed=true;}check(failed,name);}
    static EngineClient.Result waitResult(EngineClient client)throws Exception{long end=System.nanoTime()+12000000000L;EngineClient.Result r;while((r=client.poll())==null&&System.nanoTime()<end)Thread.sleep(5);check(r!=null,"client result timeout");return r;}
    static void idle(EngineClient client)throws Exception{long end=System.nanoTime()+3000000000L;while(client.busy()&&System.nanoTime()<end)Thread.sleep(5);check(!client.busy(),"client failed to become idle");}
    public static void main(String[] args)throws Exception{
        JsonArray vectors=new JsonParser().parse(new String(Files.readAllBytes(Paths.get(args[0])),StandardCharsets.UTF_8)).getAsJsonArray();
        check(vectors.size()==12,"fixed original fixture coverage");
        int index=0;
        for(JsonElement element:vectors){
            JsonObject vector=element.getAsJsonObject(),game=vector.getAsJsonObject("snapshot"),response=vector.getAsJsonObject("response");
            JsonObject expected=response.getAsJsonArray("actions").get(0).getAsJsonObject().getAsJsonObject("before");
            check(Guard.matches(expected,game),"original snapshot root guard "+index);
            for(String change:new String[]{"energy","uuid","cost","target","power","rng","turn","move","max_hp","potion"}){
                JsonObject altered=clone(game),c=altered.getAsJsonObject("combat_state"),p=c.getAsJsonObject("player");
                JsonArray cardPile=c.getAsJsonArray("hand");if(cardPile.size()==0)cardPile=c.getAsJsonArray("draw_pile");if(cardPile.size()==0)cardPile=c.getAsJsonArray("discard_pile");
                if(change.equals("max_hp"))p.addProperty("max_hp",p.get("max_hp").getAsInt()+1);
                if(change.equals("potion"))altered.getAsJsonArray("potions").get(0).getAsJsonObject().addProperty("id","Strength Potion");
                if(change.equals("energy"))p.addProperty("energy",p.get("energy").getAsInt()+1);
                if(change.equals("uuid"))cardPile.get(0).getAsJsonObject().addProperty("uuid","different-card");
                if(change.equals("cost"))cardPile.get(0).getAsJsonObject().addProperty("cost",99);
                if(change.equals("target"))c.getAsJsonArray("monsters").get(0).getAsJsonObject().addProperty("current_hp",0);
                if(change.equals("power")){JsonObject strength=new JsonObject();strength.addProperty("id","Strength");strength.addProperty("amount",79);p.getAsJsonArray("powers").add(strength);}
                if(change.equals("rng"))c.getAsJsonObject("rngs").getAsJsonObject("ai").addProperty("seed0","1");
                if(change.equals("turn"))c.addProperty("turn",999);
                if(change.equals("move"))c.getAsJsonArray("monsters").get(0).getAsJsonObject().addProperty("move_id",999);
                check(!Guard.matches(expected,altered),"must stop for "+change+" fixture "+index);
            }
            String id=response.get("id").getAsString(),token=response.get("snapshot_token").getAsString();
            check(Protocol.response(response.toString(),id,token).equals(response),"response round trip");
            rejects(()->Protocol.response(response.toString(),id,"stale"),"stale result rejected");
            JsonObject wrongVersion=clone(response);wrongVersion.addProperty("version",1.9);
            rejects(()->Protocol.response(wrongVersion.toString(),id,token),"fractional version rejected");
            rejects(()->Protocol.response(response.toString(),"another-request",token),"wrong request rejected");
            index++;
        }
        rejects(()->Protocol.response("not json","id","token"),"bad json rejected");
        rejects(()->Protocol.readLine(new StringReader("")),"EOF rejected");
        check("{\"中文\":1}".equals(Protocol.readLine(new StringReader("{\"中文\":1}\n"))),"UTF8 line");
        check("macos-arm64".equals(AdvisorConfig.platform("Mac OS X","aarch64")),"mac arm");
        check("macos-x64".equals(AdvisorConfig.platform("Mac OS X","x86_64")),"mac Rosetta process architecture");
        check("windows-x64".equals(AdvisorConfig.platform("Windows 11","amd64")),"Windows");
        check("linux-x64".equals(AdvisorConfig.platform("Linux","amd64")),"Linux");
        check(AdvisorConfig.platform("Linux","arm64")==null,"unknown platform degrades");
        JsonObject game=vectors.get(0).getAsJsonObject().getAsJsonObject("snapshot");
        try(EngineClient client=new EngineClient()){
            check(client.search(Paths.get(args[1]),"jvm-native",game,64,10000),"submit");
            check(!client.search(Paths.get(args[1]),"duplicate",game,64,10000),"no overlapping requests");
            EngineClient.Result result=waitResult(client);check(result.error==null,"Java to native protocol: "+result.error);check(result.value.get("search_calls").getAsInt()==1,"single search through Java");idle(client);
            check(client.search(Paths.get("/nonexistent/SearchAdvisor-engine"),"missing",game,64,10000),"missing submit");
            result=waitResult(client);check(result.error.contains("未安装搜索引擎"),"missing engine degradation");idle(client);
            check(client.search(Paths.get(args[1]),"cancel",game,100000,30000),"cancel submit");Thread.sleep(20);client.cancel();idle(client);check(client.poll()==null,"cancel suppresses result");
            check(client.search(Paths.get(args[1]),"after-cancel",game,64,10000),"fresh request after cancellation");result=waitResult(client);check(result.error==null,"fresh result");idle(client);
            check(client.search(Paths.get(args[2]),"unresponsive",game,100000,30000),"unresponsive process submit");Thread.sleep(50);long start=System.nanoTime();client.cancel();idle(client);check(System.nanoTime()-start<2000000000L,"forced cancellation deadline");check(client.poll()==null,"unresponsive cancellation result suppressed");
        }
        System.out.println("PASS: "+checks+" assertions; 12 original guards, mutations, real native IPC, cancellation and missing engine. No game launch.");
    }
}
