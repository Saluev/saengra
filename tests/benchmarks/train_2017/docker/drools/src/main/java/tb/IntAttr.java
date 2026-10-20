package tb;

public final class IntAttr {
    private final String name;
    private final long id;
    private final int value;

    public IntAttr(String name, long id, int value) {
        this.name = name;
        this.id = id;
        this.value = value;
    }

    public String getName() {
        return name;
    }

    public long getId() {
        return id;
    }

    public int getValue() {
        return value;
    }
}
