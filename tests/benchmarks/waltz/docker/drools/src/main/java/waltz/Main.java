package waltz;

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
 * Runs the Waltz benchmark on Drools and prints the result as JSON, in the same format as
 * waltz/runner.py.
 *
 *     java -jar waltz-drools.jar LINES_PATH
 *
 * The lines file has a line "P1 P2" for every line of the drawing.
 */
public final class Main {

    private static KieSession createSession() throws IOException {
        KieServices ks = KieServices.Factory.get();
        KieFileSystem kfs = ks.newKieFileSystem();
        String resource = "/waltz/waltz.drl";
        String drl = new String(Main.class.getResourceAsStream(resource).readAllBytes());
        kfs.write("src/main/resources" + resource, drl);
        KieBuilder builder = ks.newKieBuilder(kfs).buildAll();
        if (builder.getResults().hasMessages(Message.Level.ERROR)) {
            throw new IllegalStateException(builder.getResults().toString());
        }
        KieContainer container = ks.newKieContainer(ks.getRepository().getDefaultReleaseId());
        return container.newKieSession();
    }

    private static long procStatusBytes(String field) throws IOException {
        for (String line : Files.readAllLines(Path.of("/proc/self/status"))) {
            if (line.startsWith(field + ":")) {
                return Long.parseLong(line.replaceAll("[^0-9]", "")) * 1024;
            }
        }
        return 0;
    }

    public static void main(String[] args) throws Exception {
        List<int[]> lines = new ArrayList<>();
        for (String line : Files.readAllLines(Path.of(args[0]))) {
            String[] parts = line.split(" ");
            lines.add(new int[] {Integer.parseInt(parts[0]), Integer.parseInt(parts[1])});
        }
        KieSession session = createSession();
        System.gc();
        long memBaseline = procStatusBytes("VmRSS");

        long t = System.nanoTime();
        session.insert(new Facts.Stage("duplicate"));
        for (int[] line : lines) {
            session.insert(new Facts.Line(line[0], line[1]));
        }
        int firings = session.fireAllRules();
        double elapsed = (System.nanoTime() - t) / 1e9;
        long memPeak = procStatusBytes("VmHWM");

        StringBuilder edges = new StringBuilder("[");
        StringBuilder junctions = new StringBuilder("[");
        for (Object fact : session.getObjects()) {
            if (fact instanceof Facts.Edge e) {
                edges.append(edges.length() > 1 ? ", " : "")
                    .append('[').append(e.getP1()).append(", ").append(e.getP2())
                    .append(", \"").append(e.getLabel()).append("\"]");
            } else if (fact instanceof Facts.Junction j) {
                junctions.append(junctions.length() > 1 ? ", " : "").append('"').append(j.getType()).append('"');
            }
        }
        System.out.println("{\"time\": " + elapsed
            + ", \"firings\": " + firings
            + ", \"edges\": " + edges.append(']')
            + ", \"junctions\": " + junctions.append(']')
            + ", \"mem_baseline\": " + memBaseline
            + ", \"mem_peak\": " + memPeak + "}");
        session.dispose();
    }
}
