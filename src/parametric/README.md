<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# fos::parametric — expression and dependency engine

UI-independent core that furniture definitions are built on. Depends only on `fos::core`.
All values are `double` (millimetres, degrees, plain numbers; booleans are `0` / `1`).

## Concepts

| Concept | Meaning |
|---|---|
| **Expression** (`expression.hpp`) | Immutable shared AST (`ExprPtr`); `parseExpression`, `evaluate`, `toString`, `referencedIdentifiers`, `renameIdentifiers`. |
| **Parameter** (`ParameterDef`) | Unique name + either a constant `value` or an `expression` text; kind tag (`Number, Length, Count, Angle, Ratio, Boolean`), description, inclusive bounds, `integer` flag. |
| **Constraint** (`ConstraintDef`) | Named boolean expression + message template (`{param}` placeholders) + severity. `Error` blocks a change, `Warning` is only reported. |
| **Transaction** | Plain list of steps (`define`, `redefine`, `setValue`, `setExpression`, `setBounds`, `remove`, `rename`, `addConstraint`, …) applied atomically by `ParameterGraph::apply`. Every single-call mutator is a one-step transaction. |
| **ChangeSet** | Result of a successful change: `changed`, `added`, `removed`, `renamed`, `warnings`. Also sent to observers. |
| **RecalcStats** | What the last recalculation did: `evaluated`, `changed`, `visited`, `constraintsChecked`. |
| **Snapshot** | O(1) immutable copy of the whole state (copy-on-write) for undo/redo. |
| **ParameterTemplate** (`scope.hpp`) | Definitions with local names, instantiated under prefixes (`cab1.width`, `cab2.width`). |

## Expression grammar (EBNF)

```ebnf
expression     = or ;
or             = and , { "||" , and } ;
and            = equality , { "&&" , equality } ;
equality       = relational , { ( "==" | "!=" ) , relational } ;
relational     = additive , { ( "<" | "<=" | ">" | ">=" ) , additive } ;
additive       = multiplicative , { ( "+" | "-" ) , multiplicative } ;
multiplicative = unary , { ( "*" | "/" ) , unary } ;
unary          = ( "-" | "!" ) , unary | power ;
power          = primary , [ "^" , unary ] ;            (* right-associative: 2^3^2 = 2^9; -2^2 = -4 *)
primary        = number | identifier | call | "(" , expression , ")" ;
call           = function , "(" , expression , { "," , expression } , ")" ;
function       = "min" | "max" | "abs" | "floor" | "ceil" | "round" | "sqrt" | "clamp" | "if" ;
identifier     = segment , { "." , segment } ;          (* cab.inner_width, shelf_count *)
segment        = ( letter | "_" ) , { letter | digit | "_" } ;
number         = ( digit , { digit } , [ "." , { digit } ] | "." , digit , { digit } ) ,
                 [ ( "e" | "E" ) , [ "+" | "-" ] , digit , { digit } ] ;
```

* `min`/`max` take ≥ 1 argument; `abs floor ceil round sqrt` take 1; `clamp(x, lo, hi)` and `if(cond, a, b)` take 3.
* Comparisons and logic yield `1`/`0`; any non-zero value is true. Comparisons are **exact** (no tolerance).
* `&&`, `||` short-circuit; `if` evaluates only the selected branch. `round` rounds half away from zero.
* Evaluation errors (never exceptions): division by zero, `sqrt` of a negative, `clamp` with `lo > hi`,
  any non-finite intermediate or final result, unknown identifier.
* Limits: text ≤ 16384 characters, AST depth ≤ 256 (so at most ~255 chained binary operators).
* Parse errors carry the 0-based position, what was expected and what was found (`ParseFailure`).

## Usage

```cpp
using namespace fos::parametric;
ParameterGraph g;
g.defineConstant("outer_width", 800, ParameterKind::Length);
g.defineConstant("left_thickness", 18, ParameterKind::Length);
g.defineConstant("right_thickness", 18, ParameterKind::Length);
g.defineExpression("inner_width", "outer_width - left_thickness - right_thickness", ParameterKind::Length);
g.addConstraint({"shelf fits", "inner_width > 0", "Inner width must be positive (is {inner_width} mm)",
                 ConstraintSeverity::Error});
g.value("inner_width");                        // 764

auto r = g.setValue("outer_width", 900);       // Result<ChangeSet>
// r.value().changed == {"inner_width", "outer_width"}; g.lastRecalculation().evaluated == {"inner_width"}

Transaction tx;                                // atomic batch; order of definitions is free
tx.setValue("left_thickness", 16).setValue("right_thickness", 16);
g.apply(tx);                                   // inner_width == 868

auto bad = g.setValue("outer_width", 20);      // fails, graph unchanged:
// "Constraint 'shelf fits' violated: Inner width must be positive (is -12 mm) (inner_width > 0 is false; inner_width = -12)."

Snapshot s = g.snapshot();                     // undo point
g.restore(s);                                  // returns/notifies the differences

// Serialisation: g.definitions() + g.constraints()  <->  ParameterGraph::load(defs, constraints)

// Scoping: `outer.x` refers to the enclosing scope, template-local names get the prefix.
ParameterTemplate shelf;
shelf.parameters = {ParameterDef::constant("gap", 1), ParameterDef::formula("length", "outer.inner_width - 2 * gap")};
auto inst = instantiate(shelf, "shelf1");      // shelf1.gap, shelf1.length = "inner_width - 2 * shelf1.gap"
Transaction add; appendTo(add, inst.value()); g.apply(add);
```

## Guarantees

* **Atomicity** — a mutation either commits completely or leaves definitions, values, dependency index
  and constraints exactly as before; the `core::Error` (and `lastDiagnostics()`) say why. Never throws on bad input.
* **Validation at commit** — unknown references (names the missing parameter), cycles (lists the path
  `a -> c -> b -> a`, each parameter depends on the next), removal of a parameter still in use (names
  the users), bounds, integer flag (whole number within 1e-9, then snapped), Boolean kind (0 or 1),
  constraints (messages include the actual values).
* **Incremental recalculation** — only changed parameters and their transitive dependents are visited,
  each at most once, dependencies first; a parameter is re-evaluated only if one of its inputs actually
  changed value (early cut-off). Only constraints reading a changed value (or newly added ones) are re-checked.
* **Determinism** — ordered containers only; `names()`/`definitions()` are sorted by name, evaluation
  order and all messages are reproducible.
* **Cost** — change: O(affected sub-graph · log n); snapshot/copy: O(1), plus one O(n) clone on the
  first mutation after a snapshot. 10,000-parameter chain: load ≈ 40 ms, full recalculation ≈ 10 ms (Release).

## Limitations

* `rename` and `instantiate` store rewritten expressions in canonical `toString` form (original spacing
  and redundant parentheses are lost).
* An evaluation failure in a constraint blocks the change even for `Warning` severity.
* On failure only the first blocking problem is returned as `core::Error`; constraint violations found
  in the same pass are all listed in `lastDiagnostics()`.
* `ParameterKind` is a tag: no unit conversion or dimensional analysis. `Count` does not imply `integer`.
* No user-defined functions, strings or vectors. Not thread-safe.
* `outer` is reserved as the first name segment inside templates passed to `instantiate`.
