package searchadvisor;
import com.google.gson.*;
import java.io.*;

/** No game classes: exercised by standalone JVM tests. */
public final class Protocol {
    public static final int VERSION=1;
    private Protocol() { }
    public static JsonObject request(String id,String token,JsonObject snapshot,int budget,int timeout) {
        JsonObject r=new JsonObject(); r.addProperty("version",VERSION); r.addProperty("id",id); r.addProperty("op","search");
        r.addProperty("snapshot_token",token); r.addProperty("simulations",budget); r.addProperty("timeout_ms",timeout); r.add("snapshot",snapshot); return r;
    }
    public static JsonObject response(String line,String id,String token) throws IOException {
        try {
            JsonObject r=new JsonParser().parse(line).getAsJsonObject();
            if(!r.get("version").getAsJsonPrimitive().isNumber() || !"1".equals(r.get("version").getAsString()) || !r.get("id").getAsJsonPrimitive().isString() || !id.equals(r.get("id").getAsString()) || !r.get("ok").getAsJsonPrimitive().isBoolean()) throw new IOException("协议版本或请求编号不匹配");
            if(!r.get("ok").getAsBoolean()) throw new IOException(r.getAsJsonObject("error").get("code").getAsString()+": "+r.getAsJsonObject("error").get("message").getAsString());
            if(!token.equals(r.get("snapshot_token").getAsString()) || !r.get("actions").isJsonArray()) throw new IOException("过期或无效的搜索结果");
            return r;
        } catch(RuntimeException e) { throw new IOException("无效 JSON 响应",e); }
    }
    public static String readLine(Reader in) throws IOException {
        StringBuilder s=new StringBuilder(); int c;
        while((c=in.read())!=-1 && c!='\n') { if(s.length()>=4*1024*1024) throw new IOException("响应超过 4 MiB"); s.append((char)c); }
        if(c==-1 && s.length()==0) throw new EOFException("搜索进程退出"); return s.toString();
    }
    public static JsonObject comparable(JsonObject snapshot) {
        JsonObject copy=new JsonParser().parse(snapshot.toString()).getAsJsonObject();
        copy.remove("full_rng_state"); copy.getAsJsonObject("combat_state").remove("frame_delta_seconds"); return copy;
    }
}
