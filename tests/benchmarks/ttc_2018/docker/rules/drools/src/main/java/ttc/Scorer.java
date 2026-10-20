package ttc;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

/** Port of scoring.py: scores and top-3 results, fed by match events from the rules. */
public class Scorer {
    private final String entityType;
    private final Map<Long, String> timestamps = new HashMap<>();
    protected final Map<Long, Long> scores = new HashMap<>();
    private List<Long> top = new ArrayList<>();
    private Set<Long> changed = new HashSet<>();

    public Scorer(String entityType) {
        this.entityType = entityType;
    }

    /** Registers scored entities (posts or comments) with their timestamps. */
    public void observe(String[] op) {
        if (op[0].equals("N") && op[1].equals(entityType)) {
            long id = Long.parseLong(op[2]);
            scores.putIfAbsent(id, 0L);
            changed.add(id);
        } else if (op[0].equals("A") && op[1].equals("timestamp")) {
            long id = Long.parseLong(op[2]);
            if (scores.containsKey(id)) {
                timestamps.put(id, op[3]);
            }
        }
    }

    public void add(long entity, long delta) {
        scores.merge(entity, delta, Long::sum);
        changed.add(entity);
    }

    public String result() {
        Set<Long> candidates = new HashSet<>(top);
        candidates.addAll(changed);
        changed = new HashSet<>();
        Comparator<Long> order = Comparator.<Long>comparingLong(scores::get)
            .thenComparing(e -> timestamps.getOrDefault(e, ""))
            .thenComparingLong(e -> e);
        top = candidates.stream().sorted(order.reversed()).limit(3).collect(Collectors.toList());
        return top.stream().map(String::valueOf).collect(Collectors.joining("|"));
    }
}
