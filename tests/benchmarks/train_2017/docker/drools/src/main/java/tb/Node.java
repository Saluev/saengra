package tb;

public final class Node {
    private final String type;
    private final long id;

    public Node(String type, long id) {
        this.type = type;
        this.id = id;
    }

    public String getType() {
        return type;
    }

    public long getId() {
        return id;
    }
}
