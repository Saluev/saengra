package ttc;

import java.util.HashMap;
import java.util.Map;

/** Q2: per comment, sum of squared sizes of friendship components among its likers. */
public final class ComponentsScorer extends Scorer {
    private record Node(long comment, long user) {}

    private final Map<Node, Node> parent = new HashMap<>();
    private final Map<Node, Long> size = new HashMap<>();

    public ComponentsScorer() {
        super("Comment");
    }

    private Node find(Node node) {
        if (!parent.containsKey(node)) {
            parent.put(node, node);
            size.put(node, 1L);
            add(node.comment(), 1);
            return node;
        }
        while (!parent.get(node).equals(node)) {
            Node grandparent = parent.get(parent.get(node));
            parent.put(node, grandparent);
            node = grandparent;
        }
        return node;
    }

    public void liker(long comment, long user) {
        find(new Node(comment, user));
    }

    public void friends(long comment, long u1, long u2) {
        Node a = find(new Node(comment, u1));
        Node b = find(new Node(comment, u2));
        if (a.equals(b)) {
            return;
        }
        if (size.get(a) < size.get(b)) {
            Node t = a;
            a = b;
            b = t;
        }
        parent.put(b, a);
        // (x + y)² = x² + y² + 2xy
        add(comment, 2 * size.get(a) * size.get(b));
        size.put(a, size.get(a) + size.get(b));
    }
}
