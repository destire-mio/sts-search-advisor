package searchadvisor;
import com.badlogic.gdx.Input;
import com.evacipated.cardcrawl.modthespire.Loader;
import com.evacipated.cardcrawl.modthespire.ModInfo;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public final class AdvisorConfig {
    public int searchKey=Input.Keys.F8,autoKey=Input.Keys.F9,stopKey=Input.Keys.F10;
    public int simulations=2000,timeoutMs=5000;
    public Path engine;
    public String error;
    public static String platform(String os,String arch) {
        os=os.toLowerCase(Locale.ROOT);arch=arch.toLowerCase(Locale.ROOT);
        String cpu=(arch.equals("aarch64")||arch.equals("arm64"))?"arm64":(arch.equals("amd64")||arch.equals("x86_64"))?"x64":null;
        if(cpu==null)return null;
        if(os.contains("mac"))return "macos-"+cpu;
        if(os.contains("windows")&&cpu.equals("x64"))return "windows-x64";
        if(os.contains("linux")&&cpu.equals("x64"))return "linux-x64";
        return null;
    }
    public static AdvisorConfig load() {
        AdvisorConfig c=new AdvisorConfig();
        try {
            Path jar=Paths.get(AdvisorConfig.class.getProtectionDomain().getCodeSource().getLocation().toURI());
            if(Loader.MODINFOS!=null)for(ModInfo mod:Loader.MODINFOS)if("searchadvisor".equals(mod.ID))jar=Paths.get(mod.jarURL.toURI());
            Path base=Files.isDirectory(jar)?jar:jar.getParent();
            String conf=System.getProperty("searchadvisor.config");
            Path file=conf==null?base.resolve("SearchAdvisor.properties"):Paths.get(conf);
            Properties p=new Properties(); if(Files.isRegularFile(file))try(Reader in=Files.newBufferedReader(file)){p.load(in);}
            c.searchKey=key(p,"searchKey","F8");c.autoKey=key(p,"autoKey","F9");c.stopKey=key(p,"stopKey","F10");
            if(c.searchKey==c.autoKey||c.searchKey==c.stopKey||c.autoKey==c.stopKey)throw new IllegalArgumentException("快捷键不能重复");
            c.simulations=number(p,"simulations",2000,1,100000); c.timeoutMs=number(p,"timeoutMs",5000,1,30000);
            String explicit=System.getProperty("searchadvisor.engine",p.getProperty("enginePath",""));
            String platform=platform(System.getProperty("os.name"),System.getProperty("os.arch"));
            if(!explicit.isEmpty()){Path path=Paths.get(explicit);c.engine=path.isAbsolute()?path:file.toAbsolutePath().getParent().resolve(path);}
            else if(platform!=null)c.engine=base.resolve("searchadvisor-engine").resolve(platform).resolve(platform.startsWith("windows")?"sts-search-server.exe":"sts-search-server");
        } catch(Exception e){c.error="配置错误: "+e.getMessage();c.engine=null;}
        return c;
    }
    private static int key(Properties p,String key,String def){int k=Input.Keys.valueOf(p.getProperty(key,def));if(k==Input.Keys.UNKNOWN)throw new IllegalArgumentException(key);return k;}
    private static int number(Properties p,String key,int def,int lo,int hi){int x=Integer.parseInt(p.getProperty(key,""+def));if(x<lo||x>hi)throw new IllegalArgumentException(key);return x;}
}
