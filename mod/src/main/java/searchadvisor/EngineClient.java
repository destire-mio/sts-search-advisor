package searchadvisor;
import com.google.gson.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

/** Each request owns its process. No retries, shells, or game-object references. */
public final class EngineClient implements AutoCloseable {
    public static final class Result {
        public final String token; public final JsonObject value; public final String error;
        Result(String t,JsonObject v,String e) {token=t;value=v;error=e;}
    }
    private static final class Job {
        final String id,token; final AtomicBoolean cancelled=new AtomicBoolean();
        volatile Process process; volatile Writer writer;
        Job(String i,String t){id=i;token=t;}
    }
    private static ThreadFactory daemon(final String name) { return r->{Thread t=new Thread(r,name);t.setDaemon(true);return t;}; }
    private final ExecutorService worker=Executors.newSingleThreadExecutor(daemon("SearchAdvisor-search"));
    private final ExecutorService canceller=Executors.newSingleThreadExecutor(daemon("SearchAdvisor-cancel"));
    private final ScheduledExecutorService timer=Executors.newSingleThreadScheduledExecutor(daemon("SearchAdvisor-timeout"));
    private final AtomicReference<Job> active=new AtomicReference<>();
    private final ConcurrentLinkedQueue<Result> results=new ConcurrentLinkedQueue<>();
    private final AtomicLong ids=new AtomicLong();
    public Result poll(){return results.poll();}
    public boolean busy(){return active.get()!=null;}
    public boolean search(Path engine,String token,JsonObject snapshot,int budget,int timeout) {
        Job job=new Job("search-"+ids.incrementAndGet(),token);
        if(!active.compareAndSet(null,job)) return false;
        String line=Protocol.request(job.id,token,snapshot,budget,timeout).toString();
        worker.execute(()->{
            ScheduledFuture<?> kill=null;
            try {
                if(job.cancelled.get()) return;
                if(engine==null || !Files.isRegularFile(engine)) throw new IOException("未安装搜索引擎");
                if(!Files.isExecutable(engine)) throw new IOException("搜索引擎没有执行权限");
                job.process=new ProcessBuilder(engine.toAbsolutePath().toString()).start();
                if(job.cancelled.get()) {job.process.destroyForcibly();return;}
                final Process process=job.process;
                Thread drain=daemon("SearchAdvisor-stderr").newThread(()->{try(InputStream err=process.getErrorStream()){byte[] b=new byte[4096];while(err.read(b)!=-1){/* drain, bounded memory */}}catch(IOException ignored){}}); drain.start();
                kill=timer.schedule(()->process.destroyForcibly(),timeout+1000L,TimeUnit.MILLISECONDS);
                job.writer=new OutputStreamWriter(process.getOutputStream(),StandardCharsets.UTF_8);
                synchronized(job){job.writer.write(line);job.writer.write('\n');job.writer.flush();}
                try(Reader reader=new BufferedReader(new InputStreamReader(process.getInputStream(),StandardCharsets.UTF_8))) {
                    JsonObject response=Protocol.response(Protocol.readLine(reader),job.id,token);
                    if(!job.cancelled.get()) results.add(new Result(token,response,null));
                }
            } catch(Exception e) { if(!job.cancelled.get()) results.add(new Result(token,null,e.getMessage()==null?e.toString():e.getMessage())); }
            finally {
                if(kill!=null) kill.cancel(false);
                if(job.process!=null) job.process.destroyForcibly();
                active.compareAndSet(job,null);
            }
        }); return true;
    }
    public void cancel() {
        final Job job=active.get(); if(job==null || !job.cancelled.compareAndSet(false,true)) return;
        timer.schedule(()->{if(job.process!=null)job.process.destroyForcibly();},250,TimeUnit.MILLISECONDS);
        canceller.execute(()->{
            try {
                JsonObject r=new JsonObject();r.addProperty("version",1);r.addProperty("id","cancel-"+ids.incrementAndGet());r.addProperty("op","cancel");r.addProperty("request_id",job.id);
                synchronized(job){if(job.writer!=null){job.writer.write(r.toString());job.writer.write('\n');job.writer.flush();}}
            } catch(IOException ignored) { }
        });
    }
    public void close(){cancel();worker.shutdown();canceller.shutdown();timer.shutdown();}
}
