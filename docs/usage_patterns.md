# Usage: patterns

Saengra's `Environment` provides pattern matching capabilities with a
regex-inspired domain specific language for patterns.
```python
# High-level API returning only named references:
env.match("user as u (all: -author_of> post -comments> comment)")
# [{"u": user(id=1)}, {"u": user(id=2)}, ...]

# Low-level API returning entire subgraphs:
env.match_subgraphs("user as u (all: -author_of> post -comments> comment)")
# [Subgraph(...), ...]
```
There is a number of supported expressions, but we'll have to figure out
what is the result of matching first.

## Subgraphs

All pattern matching revolves around the concept of **subgraph**. Normally in computer science
a subgraph would be a subset of vertices and edges of the graph. But that is
not enough for a practical pattern matching, the same way that substrings are
not enough for practical regex matching (we also need named groups, etc.). 

Let's take a look at Subgraph object:

```python
@dataclass(frozen=True, slots=True)
class Subgraph:
    start_position: Position
    end_positions: frozenset[Position]
    vertices: frozenset[Vertex]
    edges: frozenset[Edge]
    refs: Refs
```

::: saengra.graph.Subgraph
    handler: python
    options:
      members:
        - start_position
      show_root_heading: false
      show_root_toc_entry: false
      show_source: false
      heading_level: 3

**Start position** is, quite literally, the position in the graph where the
matching started to produce this subgraph. Subgraphs with same vertices/edges
matched from different start positions are considered different subgraphs (same goes
for all the other fields in the `Subgraph` object).

**Position** is a tuple `(vertex, point)`, where `vertex` is a vertex in the graph (that is, a primitive),
and `point` is either `"."` ("core") or `"o"` ("orbit"). Core position indicates that
matching started with a vertex, and orbit — that the matching started with an edge.
One can look at core position as position "within" the vertex and orbit position as
position "outside, at the boundary" of the vertex.

::: saengra.graph.Subgraph
    handler: python
    options:
      members:
        - end_positions
      show_root_heading: false
      show_root_toc_entry: false
      show_source: false
      heading_level: 3

**End positions** are a set of positions where the matching ended. They are required 
for pattern concatenation to work. For example, a simple expression like
```text
user as u -author_of> post
```
is a concatenation of three expressions — named typed vertex (`user as u`), unidirectional
edge (`-author_of>`) and unnamed typed vertex (`post`). Pattern matcher will first match
the first expression, receive matching subgraphs, and then use their end positions to start
matching the second expression.

::: saengra.graph.Subgraph
    handler: python
    options:
      members:
        - vertices
      show_root_heading: false
      show_root_toc_entry: false
      show_source: false
      heading_level: 3

**Vertices** are, straightforwardly, the vertices within the matched subgraph.
At the level of Python code those will be primitives, i.e., regular Python objects.
See [Vertices and Edges](usage_graphs.md#vertices-and-edges) for more details.

::: saengra.graph.Subgraph
    handler: python
    options:
      members:
        - edges
      show_root_heading: false
      show_root_toc_entry: false
      show_source: false
      heading_level: 3

**Edges** are the edges within the matched subgraph.

::: saengra.graph.Subgraph
    handler: python
    options:
      members:
        - refs
      show_root_heading: false
      show_root_toc_entry: false
      show_source: false
      heading_level: 3

**Refs** are named references to vertices within the matched subgraph.
Refs are produced by named vertex expressions, i.e.,
```text
user as u
```
— for all matched subgraphs their refs will be `{"u": user(...)}`.
Direct analogy with regular expressions would be the named groups, like `(?P<u>\w+)`.

## Expressions

Subgraph matching pattern consists of expressions including other expressions,
following this grammar:
```text
expression     ::= or
or             ::= and | or "|" and
and            ::= concatenation | and "&" concatenation
concatenation  ::= atom*
atom           ::= operation | "(" expression ")" | vertex | edge | "."
vertex         ::= IDENTIFIER
                 | IDENTIFIER "as" IDENTIFIER
                 | "?"
                 | "?" "as" IDENTIFIER
                 | "*"
                 | "*" "as" IDENTIFIER
edge           ::= edge_start edge_labels edge_end
                 | edge_start "*" edge_end
edge_start     ::= "<-" | "<" | "--" | "-"
edge_end       ::= "->" | ">" | "--" | "-"
edge_labels    ::= IDENTIFIER | edge_labels "|" IDENTIFIER
operation      ::= "(" operation_name expression ")"
operation_name ::= "all:" | "if:" | "?=" | "unless:" | "?!" | "skip:" | "?:" | "maybe:"
```

Operator precedence is similar to that in regular expressions or computer science in general.

Let's walk through all available expressions, starting with the simplest ones.

## Vertex expression

To match something, we need a way to match a vertex. There are many forms of vertex expression available:
```text
*
* as u
user
user as u
?
? as u
u
```

**Wildcard vertex expression** `*` matches any vertex. For every vertex present in the graph, the pattern
matcher will produce a subgraph consisting of that vertex.

**Typed vertex expression** `user` matches any vertex of type `user`.

**Placeholder vertex expression** `?` matches only specific vertex, which should be passed to the matcher
together with the pattern:
```python
env.match("? (all: -added_to_friends>)", user(id=1))
```
Multiple placeholders can be used:
```python
env.match("? (?= -name> ?) (all: -added_to_friends>)", user(id=1), "mike")
```

**Named vertex expression** `* as u` / `user as u` / `? as u` adds matched vertex to refs under specified name.
The matched vertex also becomes available in the rest of the pattern as **reference vertex expression** `u`.
Note that there is no syntactic difference between `user` and `u`. If you override type name with reference, e.g.
`user as user`, the results may be surprising, therefore it is recommended to use abbreviations for ref names.

## Edge expression

To jump from vertex to vertex, we need a way to match an edge. Edge expression comes in a variety of forms too:
```text
--label-- (or -label-)
--label-> (or -label>)
<-label-- (or <label-)
<-label-> (or <label>)
-- label1 | label2 | label3 ->
--*->
```

In the middle of the expression, either asterisk `*` or a list of `|`-separated labels is expected.
Asterisk matches edges with any label.

Direction of the edge is specified by the characters wrapping label specification. `--label->` means
outgoing edge, `<-label--` means incoming edge, `--label--` means any edge.

If the matcher starts matching edge expression at core position, nothing is returned. Otherwise, for
every matching edge a single subgraph consisting of that edge (but not its vertices!) is returned.

`<-label->` is a special case. It matches only if there are two edges between the same pair of vertices
going in both directions, and it returns a single subgraph with both edges.

## Noop expression

**Noop expression** `.` does nothing. For any start position the matcher returns one
empty subgraph — no vertices, no edges, single end position same as the start position.
A proper usage example would be in combination with OR operator:
```text
? (-added_to_friends> | .) -added_to_friends> user as f
# finds all friends and friends of friends of a specific user
```

## Concatenation expression

The heart of every pattern matching, the **concatenation expression**:
```text
EXPR1 EXPR2 ...
```

The algorithm for matching a concatenation expression is as follows:
1. Match the first expression from a given start position, get a list of matched subgraphs.
2. For every subgraph `sg1`, for every end position of the subgraph, match the second expression from that position.
3. "Concatenate" the subgraphs, i.e., for every subgraph `sg2` matched with the second expression, concatenate vertices
and edges of `sg1` and `sg2`, take start position of `sg1`, end positions of `sg2` and store a new subgraph.
4. With the new subgraphs, repeat steps 2 and 3 until there are no more expressions left.

By design, uncontrolled concatenation expression can cause combinatorial explosion, as all possible paths in the graph
are traversed and returned as separate subgraphs. To reduce the number of returned subgraphs, (all: ) expression should be
used — see below.

## OR expression

**OR expression** follows regex syntax and logic:
```text
EXPR1 | EXPR2 | ...
```
Pattern matching for all operands is invoked, and all the resulting subgraphs are returned.

## (all: ) expression

**(all: ) expression** is the main tool for controlling the shape of the resulting subgraphs,
which is critical for observers to be useful.

```text
(all: EXPR)
```

In a simple case, when no named references are used within the nested expression, (all: ) expression invokes pattern
matching on it, and
then merges all subgraphs matched from the same start position into a single subgraph. The closest analogy would be
SQL's `GROUP BY start_position` operator, except there's only one available aggregation function, and it's subgraph merging.

For example, while the plain concatenation expression
```text
user <added_to_friends> user 
```
returns a subgraph for every pair of befriended users, (all: ) expression
```text
user (all: <added_to_friends> user)
```
returns a single subgraph for every user, containing the user and all the users they are friends with.

When named references are used, the logic becomes somewhat more complicated. And by somewhat we mean a lot.
First, vertices referenced by names also become the `GROUP BY` key, that is, only subgraphs with the same
named reference values are going to be merged. For example, expression
```text
user (all: <added_to_friends> user as u)
```
will again return a subgraph for every pair of befriended users, since subgraphs with different `u` values are not
going to be merged. Think of it as `GROUP BY start_position, u`.

It gets trickier when multiple named references are used. Consider the following expression:
```text
user (all: -name> str as n | -number_of_comments> int as nc) 
```
The nested expression `-name> str as n | -number_of_comments> int as nc` returns two subgraphs for every user: one
with their name referenced by `n`, and one with their number of comments referenced by `nc`. (all: ) expression **will
still merge them** into a single subgraph, because their refs are **merge-compatible**, meaning that they have the same
start position and there are no two different vertices under the same name.

Generally speaking, (all: ) expression finds all maximal merge-compatible groups of subgraphs, merges each group, and
returns merged subgraphs. "Maximal" here means no more subgraphs can be added to the group while preserving 
its merge-compatibility.

## AND expression

**AND expression** 
```text
EXPR1 & EXPR2 & ...
```
invokes matching on all operands, finds all merge-compatible groups of subgraphs in the same way (all: ) expression does,
and then returns the subgraphs without merging, but only for groups containing at least one subgraph per operand.

Typical use cases would be in combination with (all: ) or (?= ) expressions:

```text
# Match user, name and number of comments as a single subgraph:
user (all: -name> str & -number_of_comments> int)

# Match user with a particular name and at least one mutual friend:
user (?= -name> ? & <added_to_friends>)
```

## (?= ) expression

(?= ) expression, also called **lookahead assertion**, works in a way similar to
that of regex lookahead assertion.

```text
(?= EXPR)
(if: EXPR)  # alternative syntax
```

It invokes matching for the nested expression, and then returns empty subgraphs for all
subgraphs matched by the nested expression, preserving only matched refs. So, if the nested expression doesn't match,
no subgraph is returned and matching stops there.

## (?! ) expression

(?! ) expression, also called **negative lookahead assertion**, is also similar to its
regex counterpart.

```text
(?! EXPR)
(unless: EXPR)  # alternative syntax
```

Matches nested expression, returns empty subgraph if nothing matched, and no subgraph otherwise.
So, if the nested expression matches, matching stops.

## (maybe: ) expression

(maybe: ) expression is Saengra's version of regex's optional group `(...)?`.

```text
(maybe: EXPR)
```

Matches nested expression, returns matched subgraphs, or an empty subgraph if nothing matched.

## (skip: ) expression

(skip: ) expression is inspired by regex's non-capturing groups.

```text
(skip: EXPR)
```

It matches nested expression, and for every matched subgraph returns it with no vertices or edges,
essentially only keeping end positions and refs. Typical use would be to reach some vertices disregarding
the way we reached them, useful for observers:
```text
# For each user, match them and the names of all their friends and friends of friends:
user (all: (skip: <added_to_friends> user (<added_to_friends> user | .)) -name> str)
```
In this example, we want the set of names but no other details, e.g., for updating the user's contact list.
