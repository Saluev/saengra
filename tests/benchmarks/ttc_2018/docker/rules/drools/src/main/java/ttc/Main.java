package ttc;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import org.kie.api.KieServices;
import org.kie.api.builder.KieBuilder;
import org.kie.api.builder.KieFileSystem;
import org.kie.api.builder.Message;
import org.kie.api.runtime.KieContainer;
import org.kie.api.runtime.KieSession;

/**
 * Replays a TTC 2018 trace (see ttc_2018/harness.py) on Drools and prints timings as JSON,
 * in the same format as the Python runner.
 *
 *     java -jar ttc2018-drools.jar QUERY TRACE_PATH
 */
public final class Main {

    private final KieSession session;
    private final Scorer scorer;

    private Main(String query) throws IOException {
        this.scorer = query.equals("Q1") ? new Scorer("Post") : new ComponentsScorer();
        this.session = createSession("/ttc/" + query.toLowerCase() + ".drl");
        session.setGlobal("scorer", scorer);
    }

    private static KieSession createSession(String resource) throws IOException {
        KieServices ks = KieServices.Factory.get();
        KieFileSystem kfs = ks.newKieFileSystem();
        String drl = new String(Main.class.getResourceAsStream(resource).readAllBytes());
        kfs.write("src/main/resources" + resource, drl);
        KieBuilder builder = ks.newKieBuilder(kfs).buildAll();
        if (builder.getResults().hasMessages(Message.Level.ERROR)) {
            throw new IllegalStateException(builder.getResults().toString());
        }
        KieContainer container = ks.newKieContainer(ks.getRepository().getDefaultReleaseId());
        return container.newKieSession();
    }

    private void apply(List<String[]> ops) {
        for (String[] op : ops) {
            scorer.observe(op);
            if (op[0].equals("E")) {
                session.insert(new Edge(op[1], Long.parseLong(op[2]), Long.parseLong(op[3])));
            }
        }
    }

    private String check() {
        session.fireAllRules();
        return scorer.result();
    }

    private static long procStatusBytes(String field) throws IOException {
        for (String line : Files.readAllLines(Path.of("/proc/self/status"))) {
            if (line.startsWith(field + ":")) {
                return Long.parseLong(line.replaceAll("[^0-9]", "")) * 1024;
            }
        }
        return 0;
    }

    private static List<List<String[]>> readTrace(String path) throws IOException {
        List<List<String[]>> phases = new ArrayList<>();
        try (BufferedReader reader = new BufferedReader(new FileReader(path))) {
            String line;
            while ((line = reader.readLine()) != null) {
                String[] parts = line.split(" ");
                switch (parts[0]) {
                    case "LOAD", "CHANGE" -> phases.add(new ArrayList<>());
                    default -> phases.get(phases.size() - 1).add(parts);
                }
            }
        }
        return phases;
    }

    public static void main(String[] args) throws Exception {
        List<List<String[]>> phases = readTrace(args[1]);
        Main main = new Main(args[0]);
        System.gc();
        long memBaseline = procStatusBytes("VmRSS");

        List<String> results = new ArrayList<>();
        List<Double> updates = new ArrayList<>();

        long t0 = System.nanoTime();
        main.apply(phases.get(0));
        results.add(main.check());
        double initial = (System.nanoTime() - t0) / 1e9;
        for (List<String[]> ops : phases.subList(1, phases.size())) {
            long t = System.nanoTime();
            main.apply(ops);
            results.add(main.check());
            updates.add((System.nanoTime() - t) / 1e9);
        }
        long memPeak = procStatusBytes("VmHWM");

        StringBuilder json = new StringBuilder("{\"initial\": ").append(initial).append(", \"updates\": [");
        for (int i = 0; i < updates.size(); i++) {
            json.append(i > 0 ? ", " : "").append(updates.get(i));
        }
        json.append("], \"results\": [");
        for (int i = 0; i < results.size(); i++) {
            json.append(i > 0 ? ", " : "").append('"').append(results.get(i)).append('"');
        }
        json.append("], \"mem_baseline\": ").append(memBaseline).append(", \"mem_peak\": ").append(memPeak).append('}');
        System.out.println(json);
        main.session.dispose();
    }
}
