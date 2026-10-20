package tb;

public final class Edge {
    private final String label;
    private final long from;
    private final long to;

    public Edge(String label, long from, long to) {
        this.label = label;
        this.from = from;
        this.to = to;
    }

    public String getLabel() {
        return label;
    }

    public long getFrom() {
        return from;
    }

    public long getTo() {
        return to;
    }
}
