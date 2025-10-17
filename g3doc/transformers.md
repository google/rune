# Proposal for Transformers in Rune

For the bootstrap compiler, we should cmpletely rethink transformers, which in
the C compiler are a code-generation hack.  In the bootstrap compiler,
transformers are compiled and dynamically linked into the bootstrap compiler,
and have full access to the HIR.  They are called once per relation statement,
but can also be called directly with a transform statement.

Currently in the HIR, a scope is a Function, which can be a regular function,
method, or class constructor.  Maybe rename to Scope?  Values passed to
transformers must resolve to constants via constant propagation.  HIR values
passed must resolve to specific values in the HIR, such as classes.

An additional upgrade to Runs could be to support typed pointers, like in Go.
This is important for transformers, because it gives the compiler flexibility
to instantiate functions like insert everywhere linked lists are used, or it
could instantiate a generic version that takes a struct of pointers to arrays
of fields such as prev an next when using SoA layout, or a pointer to a struct
of offsets into the struct for the class used to access prev, next, and such.

A relationship would look similar to what hey look like now:

```
relation DoublyLinked Graph Node cascade
relation DoublyLinked Node.out Edge.from cascade
relation DoublyLinked Node.in Edge.to cascade
```

The main difference is the dotted path names declare named scopes within
classes, where transformes can add methods and field members without name
collisions.  Accesing this data changes:

```
Old:
  firstOutEdge = node.firstOutEdge
  destNode = firstOutEdge.toNode

New:
  firstOutEdge = node.out.first
  destNode = firstOutEdge.to.parent
```

Instead of hachish string manipulation and inerpreted code, transformers coul
be compiled directly against the HIR, and loaded into the compiler.  They would
be executed once per relation or transform statement.

As the canonical example, consider the DoublyLinked transoformer.

```
transformer DoublyLinked(Parent: Scope, Child: Scope, cascadeDelete: bool = false) {
  preppendscope Parent {
    self.first = null(Child)
    self.last = null(Child)

    func insert(self, child: Child) {
      first = self.first
      if isnull(first) {
        self.last = child
      } else {
        first.prev = child
        child.next = first!
      }
      self.first = child
      child.parent = self
      ref child
    }

    iterator forward(self) {
      for child = self.first, !isnull(child), child = child.next {
        yield child!
      }
    }
  }

  if cascadeDelete {
    // If this is a cascade-delete relationship, destroy children in the destructor.
    appendscope Parent.destroy {
      do {
        child = self.first
      } while !isnull(child) {
        child.destroy()
      }
    }
  } else {
    // Remove all children.
    prependscope Parent.destroy {
      do {
        child = self.first
      } while !isnull(child) {
        self.remove(child)
      }
    }
  }

  prependscope Child {
    self.parent = null(Parent)
    self.prev = null(self)
    self.next = null(self)
  }

  // Remove self from A on destruction.
  prependscope Child.destroy {
    if !isnull(self.parent) {
      self.parent.remove(self)
    }
  }
}
```

While the initial use of transformers will be to support releationships between
classes, it opens up a lot of other potential use cases, especially when
combined with having the ability to extend Rune's syntax.  Just as a trivial
example, we can make a new statement in Rune that simply causes the compiler to
print "Hello, World!" when the statement is exected.

```
// With DSLs we add new types of Node objects to the AST.  How this works is
TBD, but there needs to be handlers for the new node types.
transformer HellowStatement(node: Node) {
  // Just destroy the node and print "Hello, world!"
  println "Hello, world!"
  node.destroy()
}
```

A transformer can do anything to the HIR.  This shojuld in theory give Rune
power similar to Rust's macros.
