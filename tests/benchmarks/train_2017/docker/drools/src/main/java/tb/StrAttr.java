package tb;

public final class StrAttr {
    private final String name;
    private final long id;
    private final String value;

    public StrAttr(String name, long id, String value) {
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

    public String getValue() {
        return value;
    }
}
