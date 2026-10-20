package tb;

public final class BoolAttr {
    private final String name;
    private final long id;
    private final boolean value;

    public BoolAttr(String name, long id, boolean value) {
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

    public boolean isValue() {
        return value;
    }
}
