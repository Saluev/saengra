package tb;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.kie.api.KieServices;
import org.kie.api.builder.KieBuilder;
import org.kie.api.builder.KieFileSystem;
import org.kie.api.builder.Message;
import org.kie.api.runtime.KieContainer;
import org.kie.api.runtime.KieSession;
import org.kie.api.runtime.rule.FactHandle;
import org.kie.api.runtime.rule.Row;
import org.kie.api.runtime.rule.ViewChangedEventListener;

/**
 * Replays a Train Benchmark trace (see workload.py) on Drools and prints timings as JSON,
 * in the same format as the Python runner.
 */
public final class Main {

    private static final String[] QUERIES = {
        "ConnectedSegments", "PosLength", "RouteSensor", "SemaphoreNeighbor", "SwitchMonitored", "SwitchSet",
    };

    private final KieSession session;
    private final Map<String, int[]> counts = new LinkedHashMap<>();
    private final Map<String, FactHandle> handles = new HashMap<>();
    private final Map<String, String> attributes = new HashMap<>();

    private Main(KieSession session) {
        this.session = session;
        for (String query : QUERIES) {
            int[] counter = new int[1];
            counts.put(query, counter);
            session.openLiveQuery(query, new Object[0], new ViewChangedEventListener() {
                @Override
                public void rowInserted(Row row) {
                    counter[0]++;
                }

                @Override
                public void rowDeleted(Row row) {
                    counter[0]--;
                }

                @Override
                public void rowUpdated(Row row) {
                }
            });
        }
    }

    private static KieSession createSession() throws IOException {
        KieServices ks = KieServices.Factory.get();
        KieFileSystem kfs = ks.newKieFileSystem();
        String drl = new String(Main.class.getResourceAsStream("/tb/train.drl").readAllBytes());
        kfs.write("src/main/resources/tb/train.drl", drl);
        KieBuilder builder = ks.newKieBuilder(kfs).buildAll();
        if (builder.getResults().hasMessages(Message.Level.ERROR)) {
            throw new IllegalStateException(builder.getResults().toString());
        }
        KieContainer container = ks.newKieContainer(ks.getRepository().getDefaultReleaseId());
        return container.newKieSession();
    }

    private void apply(List<String[]> ops) {
        for (String[] op : ops) {
            switch (op[0]) {
                case "N" -> session.insert(new Node(op[1], Long.parseLong(op[2])));
                case "E" -> {
                    String key = op[1] + " " + op[2] + " " + op[3];
                    if (!handles.containsKey(key)) {
                        handles.put(key, session.insert(new Edge(op[1], Long.parseLong(op[2]), Long.parseLong(op[3]))));
                    }
                }
                case "R" -> {
                    FactHandle handle = handles.remove(op[1] + " " + op[2] + " " + op[3]);
                    if (handle != null) {
                        session.delete(handle);
                    }
                }
                case "A" -> {
                    String attrKey = op[1] + " " + op[2];
                    String old = attributes.remove(attrKey);
                    if (old != null) {
                        session.delete(handles.remove(old));
                    }
                    String key = "A " + attrKey + " " + op[3];
                    long id = Long.parseLong(op[2]);
                    String value = op[3].substring(2);
                    Object fact = switch (op[3].charAt(0)) {
                        case 'i' -> new IntAttr(op[1], id, Integer.parseInt(value));
                        case 'b' -> new BoolAttr(op[1], id, Boolean.parseBoolean(value));
                        default -> new StrAttr(op[1], id, value);
                    };
                    attributes.put(attrKey, key);
                    handles.put(key, session.insert(fact));
                }
                default -> throw new IllegalArgumentException(op[0]);
            }
        }
    }

    private Map<String, Integer> check() {
        session.fireAllRules();
        Map<String, Integer> result = new LinkedHashMap<>();
        counts.forEach((query, counter) -> result.put(query, counter[0]));
        return result;
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
                    case "LOAD", "TRANSFORM" -> phases.add(new ArrayList<>());
                    case "#", "CHECK" -> {
                    }
                    default -> phases.get(phases.size() - 1).add(parts);
                }
            }
        }
        return phases;
    }

    private static String json(Map<String, Integer> counts) {
        StringBuilder sb = new StringBuilder("{");
        counts.forEach((k, v) -> sb.append(sb.length() > 1 ? ", " : "").append('"').append(k).append("\": ").append(v));
        return sb.append('}').toString();
    }

    private static String jsonList(List<?> items) {
        StringBuilder sb = new StringBuilder("[");
        for (Object item : items) {
            sb.append(sb.length() > 1 ? ", " : "").append(item);
        }
        return sb.append(']').toString();
    }

    public static void main(String[] args) throws Exception {
        List<List<String[]>> phases = readTrace(args[0]);
        Main main = new Main(createSession());
        System.gc();
        long memBaseline = procStatusBytes("VmRSS");

        List<String> counts = new ArrayList<>();
        List<Double> transform = new ArrayList<>();
        List<Double> recheck = new ArrayList<>();

        long t0 = System.nanoTime();
        main.apply(phases.get(0));
        long t1 = System.nanoTime();
        counts.add(json(main.check()));
        long t2 = System.nanoTime();
        for (List<String[]> ops : phases.subList(1, phases.size())) {
            long a = System.nanoTime();
            main.apply(ops);
            long b = System.nanoTime();
            counts.add(json(main.check()));
            long c = System.nanoTime();
            transform.add((b - a) / 1e9);
            recheck.add((c - b) / 1e9);
        }
        long memPeak = procStatusBytes("VmHWM");

        System.out.println("{\"read\": " + (t1 - t0) / 1e9
            + ", \"check\": " + (t2 - t1) / 1e9
            + ", \"transform\": " + jsonList(transform)
            + ", \"recheck\": " + jsonList(recheck)
            + ", \"counts\": " + jsonList(counts)
            + ", \"mem_baseline\": " + memBaseline
            + ", \"mem_peak\": " + memPeak + "}");
        main.session.dispose();
    }
}
