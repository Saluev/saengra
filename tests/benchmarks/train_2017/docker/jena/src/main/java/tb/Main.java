package tb;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.apache.jena.datatypes.xsd.XSDDatatype;
import org.apache.jena.graph.Graph;
import org.apache.jena.graph.Node;
import org.apache.jena.graph.NodeFactory;
import org.apache.jena.graph.Triple;
import org.apache.jena.graph.GraphMemFactory;
import org.apache.jena.reasoner.InfGraph;
import org.apache.jena.reasoner.rulesys.GenericRuleReasoner;
import org.apache.jena.reasoner.rulesys.Rule;
import org.apache.jena.vocabulary.RDF;

/**
 * Replays a Train Benchmark trace (see workload.py) on Apache Jena's forward RETE rule engine
 * and prints timings as JSON, in the same format as the Python runner.
 */
public final class Main {

    private static final String NS = "urn:tb:";
    private static final String[] QUERIES = {
        "ConnectedSegments", "PosLength", "RouteSensor", "SemaphoreNeighbor", "SwitchMonitored", "SwitchSet",
    };

    private final InfGraph graph;
    private final Map<String, Node> matchTypes = new LinkedHashMap<>();
    /** Queries with negation: candidate's subject variable, property, object variable that must not exist. */
    private static final Map<String, String[]> NEGATIONS = Map.of(
        "SwitchMonitored", new String[] {"sw", "monitoredBy", null},
        "RouteSensor", new String[] {"r", "requires", "sen"},
        "SemaphoreNeighbor", new String[] {"r2", "entry", "sem"}
    );
    private final Map<String, Triple> attributes = new HashMap<>();

    private Main() throws IOException {
        List<Rule> rules;
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(Main.class.getResourceAsStream("/tb/train.rules")))) {
            rules = Rule.parseRules(Rule.rulesParserFromReader(reader));
        }
        GenericRuleReasoner reasoner = new GenericRuleReasoner(rules);
        reasoner.setMode(GenericRuleReasoner.FORWARD_RETE);
        Graph base = GraphMemFactory.createDefaultGraph();
        graph = reasoner.bind(base);
        graph.prepare();
        for (String query : QUERIES) {
            String suffix = NEGATIONS.containsKey(query) ? "Candidate" : "Match";
            matchTypes.put(query, NodeFactory.createURI(NS + query + suffix));
        }
    }

    private static Node vertex(String id) {
        return NodeFactory.createURI(NS + "v" + id);
    }

    private static Node property(String name) {
        return NodeFactory.createURI(NS + name);
    }

    private static Node literal(String value) {
        String text = value.substring(2);
        return switch (value.charAt(0)) {
            case 'i' -> NodeFactory.createLiteralDT(text, XSDDatatype.XSDinteger);
            case 'b' -> NodeFactory.createLiteralDT(text, XSDDatatype.XSDboolean);
            default -> NodeFactory.createLiteralString(text);
        };
    }

    private void apply(List<String[]> ops) {
        for (String[] op : ops) {
            switch (op[0]) {
                case "N" -> graph.add(Triple.create(vertex(op[2]), RDF.type.asNode(), property(op[1])));
                case "E" -> graph.add(Triple.create(vertex(op[2]), property(op[1]), vertex(op[3])));
                case "R" -> graph.delete(Triple.create(vertex(op[2]), property(op[1]), vertex(op[3])));
                case "A" -> {
                    String key = op[1] + " " + op[2];
                    Triple old = attributes.remove(key);
                    if (old != null) {
                        graph.delete(old);
                    }
                    Triple triple = Triple.create(vertex(op[2]), property(op[1]), literal(op[3]));
                    attributes.put(key, triple);
                    graph.add(triple);
                }
                default -> throw new IllegalArgumentException(op[0]);
            }
        }
    }

    private Map<String, Integer> check() {
        Graph deductions = graph.getDeductionsGraph();
        Map<String, Integer> result = new LinkedHashMap<>();
        Graph base = graph.getRawGraph();
        matchTypes.forEach((query, type) -> {
            String[] negation = NEGATIONS.get(query);
            int count = 0;
            for (Triple t : deductions.find(Node.ANY, RDF.type.asNode(), type).toList()) {
                if (negation != null) {
                    Node m = t.getSubject();
                    Node subject = deductions.find(m, property(negation[0]), Node.ANY).next().getObject();
                    Node object = negation[2] == null
                        ? Node.ANY
                        : deductions.find(m, property(negation[2]), Node.ANY).next().getObject();
                    if (base.contains(subject, property(negation[1]), object)) {
                        continue;
                    }
                }
                count++;
            }
            result.put(query, count);
        });
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
        Main main = new Main();
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
    }
}
