//  Copyright 2021 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
TL;DR: This module implements a new type binding scheme using event-driven
binding of many function signatures, statements, and expressions in parallel.
First the problems of the original type binding scheme are described, and then
a new algorithm for solving most of them is described.

The problem
-----------
Binding in Rune is challenging.  The usual one-pass per function fails because:

* Recursive binding: While binding a function signature that calls itself, we
  are blocked on the undefined return type of the function.
* Code generation: While binding a function signature, there is an undefined
  identifier.  This may be real, or maybe it hasn't been generated yet.
* Undefined class data members: While binding class A, we refer to a method in
  class B that uses an undefined member variable on B.  These members are
  created when we bind an assignment to them in the constructor of the form
  self.x = value.  This is especially common in auto-generated destructors when
  there is a relation between A and B.
* null(A) expressions where A is a non-template class, having no template
  parameters.  We can create the default class for A, but until we bind its
  constructor call, A's data members will not exist.
* null(A) expressions where A is a template class.  This occurs in hashed
  relations and in the builtin Dict class.  This is particularly complex
  because the class won't exist until we bind a constructor call for it.  In
  generated code, we have self.table = arrayof(B), and if B is a template
  class, we cannot determine which class B refers to.
* Self null expressions in default parameters in constructors, like
  class Tree(self, <value>, left = null, right = null)
  The type of left and right are defined to be the same as self, which is
  not resolved until binding the constructor call is complete.  The
  DE_NULL_TYPE exists specifically for this case, so we can bind self.left =
  left and continue type binding.  A complex type unification scheme was
  written to eliminate null types once we see non-null assignments.
* In some cases, especially in unit tests for a single module, there may not be
  any assignment to a class variable that was originally set to a null type.
  For example, if a linked list relation from A to B is declared in B.rn, unit
  tests in A.rn may not refer to B at all, but the auto-generated destructor
  for A will try to either cascade-destroy B objects, or remove B objects from
  the linked list.  This auto-generated code will contain null types that
  cannot be resoled.  In this case, we destroy the contents of B, all relations
  of B, and all generated code from those relations, including statements in
  A's destructor.

Most of these situations can be improved with breadth-first binding, rather
than depth-first.  In the original binding algorithm, we gave an error if a
function signature cannot be fully bound for any reason.  The only solution in
Rune code was providing "type hints" that might look like:

in B.rn:
    use A
    class B(self, a: A, <value>) {
      self.value = value
      a.appendB(self)
    }
    relation DoublyLinked A B cascade

In A.rn:
    class A(self, name:string) {
      self.name = name
    }

    unittest {
      use B
      a A("test)
      if false {
        // This is a type hint.
        a = A("test")
        b = B(a, 123u32)
      }
      ...  The rest is unit test code that does not use B.
    }

The problem is that the DoublyLinked code generator added lines to A's
constructor:

    self.firstB = null(B)
    self.lastB = null(B)

In A's destructor, the code generator added a loop to destroy all the B
objects, and that loop refers to self.firstB.  The statement self.firstB =
null(B) does not provide enough information to fully specify the type of B,
which is a template class due to the <value> parameter.  The type hint gets
bound before a's destructor is called, providing the needed type information.

In general, figuring out what type hints are needed requires understanding
complex details of the binding algorithm.  The original binding algorithm was
fairly straight-forward, but to reduce the need for type hints, it became
overly complex.

The solution
------------
Most of these problems result from requiring type binding of the currently
binding statement to succeed before we can continue.  This scheme instead has a
list of "StateBinding" objects that represent partially bound statements for a
specific function signature.  A given statement can have multiple statebinding
objects in flight, one per different function signature being bound.  E.g.
max(1, 2) can bind in parallel with max("Alice", "Bob").

Similarly a StateBinding has a list of "Binding" objects representing the
bindings for the statement's expression.  Bindings form a tree matching the
statement's expression tree, and values that used to live on expressions, such
as its datatype, are moved to the Binding class.  This allows multiple bindings
of the same expression tree to be bound in parallel, and the assembly code
generator no longer has to rebind a function signature before generating code.
Once the bindings are fully bound, the statebinding object will have a
top-level binding object corresponding to the statement's expression tree.

StateBinding objects, when created, are appended to a global queue of
statebinding objects to be bound.  We repeatedly remove a statebinding object
from the head of this queue, and attempt to bind it.  This either succeeds, or
we put the statebinding object into a list of statebinding objects waiting for
the same identifier binding event.

When an identifier is successfully bound, all statebinding objects blocking on
this event are appended to the queue of active statebindings.

When active statebinding queue is finally empty, we destroy the contents of all
tclasses which were never instantiated by a constructor.  This also destroys
statebinding objects associated with the destroyed code, and generated
variables.  If there are any statebinding objects still waiting for identifier
binding events, these are reported as undefined or uninitialized identifier
errors.

When we finish binding a relation statement, append/prepend code statement, or
iterator, the corresponding generator is executed.  Newly generated statements
are assigned new statebinding objects in each function signature for that
function, and added to the queue of binding statebinding objects.

Null expressions remain diffucult, but manageble.  When called with a Tclass,
null expressions, as in null(Foo), we still return DE_NULL_TYPE datatypes.  We
allow assignment of null datatypes to variables, and consider a statebinding
bound even if we still do not know the specific class for the varialbe.  Note
that null expressions can be fully bound, such as null(Foo(123)), since we pass
a specific class returned by the constructor A(123), rather thanb a tclass.
The complex task of type resolution of variables that have null types in their
datatype, such as (u32, [null(Foo)]), still remains.  It is OK to still have
null datatypes after binding so long as we do not try to access class methods
through a null datatype.  The code generators only need to know the tclass to
generate null values, which are -1 values with width that depend only on the
tclass.

Variables also have bindings, which are created and bound when we create a new
unbound signature.  To report errors with proper stack-trace context, the
StackTrace class will form a tree instead of a stack.  Nodes in the tree
correspond to call statements in StateBinding objects.
*/

#include "de.h"

// If the event exists, move all of its statbinds to the binding queue.
void deQueueEventBlockedStateBindings(deEvent event) {
  if (event == deEventNull) {
    return;
  }
  deStateBinding statebinding;
  deSafeForeachEventStateBinding(event, statebinding) {
    deEventRemoveStateBinding(event, statebinding);
    deRootAppendBindingStateBinding(deTheRoot, statebinding);
  } deEndSafeEventStateBinding;
  deEventDestroy(event);
}

// Return false for expressions like typeof, arrayof, and null that do not
// instantiate their sub-expressions.
static bool instantiateSubExpressions(deExpressionType type) {
  switch(type) {
  case DE_EXPR_CAST:
  case DE_EXPR_CASTTRUNC:
  case DE_EXPR_NULL:
  case DE_EXPR_FUNCADDR:
  case DE_EXPR_ARRAYOF:
  case DE_EXPR_TYPEOF:
  case DE_EXPR_UNSIGNED:
  case DE_EXPR_SIGNED:
  case DE_EXPR_WIDTHOF:
    return false;
  default:
    return true;
  }
}

// Create an undefined identifier in the local scope if not already defined.
// This keeps locals from being shadowed by globals while the local assignments
// are still binding.
static void createUndefinedIdent(deBinding binding) {
  deSignature scopeSig = deBindingGetSignature(binding);
  deBlock block = deSignatureGetBlock(scopeSig);
  deExpression identExpr = deBindingGetExpression(binding);
  utSym name = deExpressionGetName(identExpr);
  utAssert(name != utSymNull);
  deIdent oldIdent = deBlockFindIdent(block, name);
  if (oldIdent == deIdentNull) {
    deUndefinedIdentCreate(block, name);
  }
}

// For assignments, bind the access unless it is a lone identifier, or the
// identifier to the right of a dot at the end.  In these two special cases,
// the identifier binding will exist but will be removed from the binding
// queue.  The bind assignment handler needs to handle these two cases and create
// variables or class data members if needed, and update the variable datatype.
static void  postProcessAssignment(deBinding binding) {
  deBinding access = deBindingGetFirstBinding(binding);
  deExpressionType type = deExpressionGetType(deBindingGetExpression(access));
  if (type == DE_EXPR_IDENT || type == DE_EXPR_DOT) {
    // If it is a dot expression, its ident binding was already removed.
    deStateBindingRemoveBinding(deBindingGetStateBinding(binding), access);
    if (type == DE_EXPR_IDENT) {
      createUndefinedIdent(access);
    }
  }
}

// Remove the identifier to the right of the dot from the binding queue.  The
// handler for binding dot expressions must bind this once the scope from the
// expression to the left is bound.
static void  postProcessDotExpression(deBinding binding) {
  deBinding identBinding = deBindingGetNextBinding(deBindingGetFirstBinding(binding));
  deStateBindingRemoveBinding(deBindingGetStateBinding(binding), identBinding);
}

// Queue the expression for binding.
static deBinding queueExpression(deSignature scopeSig, deStateBinding statebinding,
    deBinding owningBinding, deExpression expression, bool instantiating) {
  deBinding binding = deExpressionBindingCreate(scopeSig, owningBinding, expression, instantiating);
  deExpressionType type = deExpressionGetType(expression);
  bool firstTime = true;
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    // Only the first sub-expression is ever not instantiated.
    bool instantiateSubExpr = instantiating && (!firstTime || instantiateSubExpressions(type));
    firstTime = false;
    queueExpression(scopeSig, statebinding, binding, child, instantiateSubExpr);
  } deEndExpressionExpression;
  // All child bindings are queued before this one.
  deStateBindingAppendBinding(statebinding, binding);
  if (type == DE_EXPR_EQUALS) {
    postProcessAssignment(binding);
  } else if (type == DE_EXPR_DOT) {
    postProcessDotExpression(binding);
  }
  return binding;
}

// Throw all the expressions in the block into the queue to be bound.
static void queueBlockStatements(deSignature signature, deBlock block, bool instantiating) {
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deStateBinding statebinding = deStateBindingCreate(signature, statement, instantiating);
    deExpression expression = deStatementGetExpression(statement);
    if (expression != deExpressionNull) {
      deBinding rootBinding = queueExpression(signature, statebinding,
          deBindingNull, expression, instantiating);
      deStateBindingInsertRootBinding(statebinding, rootBinding);
    }
    deRootAppendBindingStateBinding(deTheRoot, statebinding);
    deBlock subBlock = deStatementGetSubBlock(statement);
    if (subBlock != deBlockNull) {
      queueBlockStatements(signature, subBlock, instantiating);
    }
  } deEndBlockStatement;
}

// Set bound bindings on narameters from the signature.
static void bindSignatureParameters(deSignature signature) {
  deBlock block = deSignatureGetBlock(signature);
  deVariable variable = deBlockGetFirstVariable(block);
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    deParameterBindingCreate(signature, variable, paramspec);
    variable = deVariableGetNextBlockVariable(variable);
  } deEndSignatureParamspec;
}

// Add a return statement if it is missing.
static void addReturnIfMissing(deBlock block) {
  deStatement statement = deBlockGetLastStatement(block);
  if (statement == deStatementNull || deStatementGetType(statement) != DE_STATEMENT_RETURN) {
    deLine line = statement == deStatementNull? deBlockGetLine(block) :
        deStatementGetLine(statement);
    deStatement returnState = deStatementCreate(block, DE_STATEMENT_RETURN, line);
    if (block == deRootGetBlock(deTheRoot)) {
      // Force main to return 0;
      deBigint bigint = deUint32BigintCreate(0);
      deExpression zeroExpr = deIntegerExpressionCreate(bigint, line);
      deStatementInsertExpression(returnState, zeroExpr);
    } else if (deFunctionGetType(deBlockGetOwningFunction(block)) == DE_FUNC_CONSTRUCTOR) {
      // Constructors return their first self parameter.
      deVariable variable = deBlockGetFirstVariable(block);
      deStatementInsertExpression(returnState,
          deIdentExpressionCreate(deVariableGetSym(variable), line));
    }
  }
}

// Add a signature to the binding queue.
void deQueueSignature(deSignature signature) {
  deBlock block = deSignatureGetBlock(signature);
  if (!deFunctionBuiltin(deSignatureGetFunction(signature))) {
    addReturnIfMissing(block);
  }
  queueBlockStatements(signature, block, true);
  bindSignatureParameters(signature);
}

// Once we finish binding a signature, update its paramspecs.
static void updateSignatureFromVariableBindings(deSignature signature) {
  deBlock block = deSignatureGetBlock(signature);
  deVariable var = deBlockGetFirstVariable(block);
  deParamspec param;
  deForeachSignatureParamspec(signature, param) {
    utAssert(var != deVariableNull && deVariableGetType(var) == DE_VAR_PARAMETER);
    deBinding varBinding = deFindVariableBinding(signature, var);
    utAssert(varBinding != deBindingNull);
    deParamspecSetIsType(param, deBindingIsType(varBinding));
    deParamspecSetInstantiated(param, deBindingInstantiating(varBinding));
    var = deVariableGetNextBlockVariable(var);
  } deEndSignatureParamspec;
  utAssert(var == deVariableNull || deVariableGetType(var) != DE_VAR_PARAMETER);
  if (deBlockIsDestructor(block)) {
    // The self variable of destructors needs to be marked as instantiated
    // because the destructor is the same function for each class, and the code
    // generator calls a class-specific free at the end.
    deParamspecSetInstantiated(deSignatureGetiParamspec(signature, 0), true);
  }
}

// Bind signatures until binding is done.
static void bindAllSignatures(void) {
  deStateBinding statebinding = deRootGetFirstBindingStateBinding(deTheRoot);
  while (statebinding != deStateBindingNull) {
    deRootRemoveBindingStateBinding(deTheRoot, statebinding);
    deBindStatement2(statebinding);
    if (deStateBindingGetFirstBinding(statebinding) ==  deBindingNull) {
      // The statement is now fully bound.
      deSignature signature = deStateBindingGetSignature(statebinding);
      deSignatureRemoveBindingStateBinding(signature, statebinding);
      if (deSignatureGetFirstBindingStateBinding(signature) == deStateBindingNull) {
        // The signature is now fully bound.
        updateSignatureFromVariableBindings(signature);
      }
    }
    statebinding = deRootGetFirstBindingStateBinding(deTheRoot);
  }
}

// Destroy contents of tclasses that were never consttructed.  Delete relations
// with the tclass, and generated all generated code from those relations.
static void destroyUnusedTclassesContents(void) {
  // This iterator is tricky because if we destroy an tclass, and it has an
  // inner tclass, we'll destroy that too, breaking the assumption made by the
  // auto-generated safe iterators.  Inner tclasses are always after their
  // outer tclasses, so it should be safe to destroy them in a backwards
  // traversal of tclasses.
  deTclass tclass, prevTclass;
  for (tclass = deRootGetLastTclass(deTheRoot); tclass != deTclassNull;
       tclass = prevTclass) {
    prevTclass = deTclassGetPrevRootTclass(tclass);
    if (!deTclassBuiltin(tclass) && deTclassGetNumClasses(tclass) == 0) {
      deDestroyTclassContents(tclass);
    }
  }
}

// Report the event and exit.
static void reportEvent(deEvent event) {
  deStateBinding statebinding = deEventGetFirstStateBinding(event);
  utAssert(statebinding != deStateBindingNull);
  deSignature signature = deEventGetReturnSignature(event);
  if (signature != deSignatureNull) {
    deDumpSignature(signature);
    putchar('\n');
    deError(deSignatureGetLine(signature), "Unable to determine return type");
  }
  deBinding varBinding = deEventGetVariableBinding(event);
  if (varBinding != deBindingNull) {
     deVariable variable = deBindingGetVariable(varBinding);
     deError(deVariableGetLine(variable), "Could not determine type of variable %s",
         deVariableGetName(variable));
  }
  deIdent undefinedIdent = deEventGetUndefinedIdent(event);
  utAssert(undefinedIdent != deIdentNull);
  deError(deBindingGetLine(deIdentGetFirstBinding(undefinedIdent)),
     "Undefined identifier %s", deIdentGetName(undefinedIdent));
}

// Report errors for any undefined or unbound identifiers that remain, and
// exit if any exist.
static void reportUnboundStateBindings(void) {
  deEvent event;
  deForeachRootEvent(deTheRoot, event) {
    reportEvent(event);
  } deEndRootEvent;
}

// Bind expressions everywhere.
void deBind2(void) {
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  deFunction mainFunc = deBlockGetOwningFunction(rootBlock);
  deSignature mainSignature = deSignatureCreate(mainFunc,
      deDatatypeArrayAlloc(), deFunctionGetLine(mainFunc));
  deVariable argv = deIdentGetVariable(deBlockFindIdent(rootBlock, utSymCreate("argv")));
  deBinding argvBinding = deVariableBindingCreate(mainSignature, argv);
  deBindingSetDatatype(argvBinding, deVariableGetDatatype(argv));
  deBindingSetInstantiating(argvBinding, true);
  deQueueSignature(mainSignature);
  bindAllSignatures();
  destroyUnusedTclassesContents();
  reportUnboundStateBindings();
}

// Return true for packages and modules, where variables are global.
static bool signatureIsModuleOrPackage(deSignature signature) {
  deFunctionType type = deFunctionGetType(deSignatureGetFunction(signature));
  return type == DE_FUNC_PACKAGE || type == DE_FUNC_MODULE;
}

// Apply variable bindings to the variables.
static void applyVariableBindings(deSignature signature) {
  bool isGlobal = signatureIsModuleOrPackage(signature);
  deVariable var;
  deForeachBlockVariable(deSignatureGetBlock(signature), var) {
    deBinding varBinding = deFindVariableBinding(signature, var);
    if (varBinding != deBindingNull) {
      deVariableSetDatatype(var, deBindingGetDatatype(varBinding));
      deVariableSetIsType(var, deBindingIsType(varBinding));
      deVariableSetInstantiated(var, isGlobal || deBindingInstantiating(varBinding));
    }
  } deEndBlockVariable;
}

// Apply expression bindings to expressions recursively.
static void applyExpressionBinding(deBinding binding) {
  deExpression expr = deBindingGetExpression(binding);
  deExpressionSetDatatype(expr, deBindingGetDatatype(binding));
  deExpressionSetIsType(expr, deBindingIsType(binding));
  deExpressionSetSignature(expr, deBindingGetCallSignature(binding));
  deBinding child;
  deForeachBindingBinding(binding, child) {
    applyExpressionBinding(child);
  } deEndBindingBinding;
  if (deExpressionGetType(expr) == DE_EXPR_IDENT) {
    deIdent oldIdent = deExpressionGetIdent(expr);
    if (oldIdent != deIdentNull) {
      deIdentRemoveExpression(oldIdent, expr);
    }
    deIdent ident = deBindingGetIdent(binding);
    utAssert(ident != deIdentNull);
    deIdentAppendExpression(ident, expr);
  }
}

// Reset bindings for the signature.
static void resetBinding(deSignature signature) {
  deStateBinding statebinding;
  deSafeForeachSignatureStateBinding(signature, statebinding) {
    deStateBindingDestroy(statebinding);
  } deEndSafeSignatureStateBinding;
}

// Apply a signature's binding to its variables and expressions so we can use
// the existing code generator.
void deApplySignatureBindings(deSignature signature) {
  // Reset bindings and rebind in case anything has changed.
  resetBinding(signature);
  deQueueSignature(signature);
  bindAllSignatures();
  reportUnboundStateBindings();
  applyVariableBindings(signature);
  deStateBinding statebinding;
  deForeachSignatureStateBinding(signature, statebinding) {
    deStatement statement = deStateBindingGetStatement(statebinding);
    deStatementSetInstantiated(statement, deStateBindingInstantiated(statebinding));
    deBinding binding = deStateBindingGetRootBinding(statebinding);
    if (binding != deBindingNull) {
      applyExpressionBinding(binding);
    }
  } deEndSignatureStateBinding;
}
