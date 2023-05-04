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
  are blocked on the recursive call since the return type is still unknown.
* Code transforms: While binding a function signature, there is an undefined
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
  cannot be resolved.  In this case, we destroy the contents of B, all relations
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
      a A("test")
      if false {
        // This is a type hint.
        a = A("test")
        b = B(a, 123u32)
      }
      ...  The rest is unit test code that does not use B.
    }

The problem is that the DoublyLinked code transformer added lines to A's
constructor:

    self.firstB = null(B)
    self.lastB = null(B)

In A's destructor, the code transformer added a loop to destroy all the B
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
list of "Binding" objects that represent partially bound expressions.  Functions
will be uniquified per signature, before binding, so binding can be done once.

Similarly a Binding has a list of Expression objects representing the queue
of expressions to be bound for a statement, default value expression, or type
expression.  The new scheme allows multiple expression trees to be bound in
parallel, and the assembly code transformer no longer has to rebind a function
signature before generating code.

Binding objects, when created, are appended to a global queue of binding objects
to be bound.  We repeatedly remove a binding object from the head of this queue,
and attempt to bind it.  This either succeeds, or we put the binding object into
a list of binding objects waiting for the same binding event, such as the type
of an identifier being found.

When an identifier is successfully bound, all binding objects blocking on this
event are appended to the queue of active bindings.

When the binding queue is finally empty, we destroy the contents of all
templates which were never instantiated by a constructor.  This also destroys
binding objects associated with the destroyed code.  If there are any binding
objects still waiting for binding events, these are reported as undefined or
uninitialized identifier errors.

Null expressions remain difficult, but manageable.  When called with a template,
null expressions, as in null(Foo), we still return DE_NULL_TYPE datatypes.  We
allow assignment of null datatypes to variables, but consider a variable bound
only when we know the specific class for the variable.  Note that null
expressions can be fully bound, such as null(Foo(123)), since we pass a specific
class returned by the constructor A(123), rather than a template.  The complex
task of type resolution of variables that have null types in their
datatype, such as (u32, [null(Foo)]), still remains.  It is OK to still have
null datatypes after binding so long as we do not try to access class methods
through a null datatype.

To report errors with proper stack-trace context, the StackTrace class will form
a tree instead of a stack.  Nodes in the tree correspond to call statements.
*/

#include "de.h"

// If the event exists, move all of its statbinds to the binding queue.
void deQueueEventBlockedBindings(deEvent event) {
  if (event == deEventNull) {
    return;
  }
  deBinding binding;
  deSafeForeachEventBinding(event, binding) {
    deEventRemoveBinding(event, binding);
    deRootAppendBinding(deTheRoot, binding);
  } deEndSafeEventBinding;
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
  case DE_EXPR_WIDTHOF:
    return false;
  default:
    return true;
  }
}

// For assignments, bind the access unless it is a lone identifier, or the
// identifier to the right of a dot at the end.  In these two special cases,
// the identifier expression will exist but will be removed from the expression
// queue.  The bind assignment handler needs to handle these two cases and create
// variables or class data members if needed, and update the variable datatype.
static void  postProcessAssignment(deExpression expression) {
  deExpression access = deExpressionGetFirstExpression(expression);
  deExpressionType type = deExpressionGetType(access);
  if (type == DE_EXPR_IDENT || type == DE_EXPR_DOT) {
    // If it is a dot expression, its ident expression was already removed.
    deBindingRemoveExpression(deExpressionGetBinding(expression), access);
  }
}

// Remove the identifier to the right of the dot from the binding queue.  The
// handler for expression dot expressions must bind this once the scope from the
// expression to the left is bound.
static void  postProcessDotExpression(deExpression expression) {
  deExpression identExpression = deExpressionGetLastExpression(expression);
  deBindingRemoveExpression(deExpressionGetBinding(expression), identExpression);
}

// Remove the identifier to the left of the named parameter expression from the
// binding queue.  The handler for expression named parameter expressions must bind
// this once the right hand side is bound.
static void  postProcessNamedParameterExpression(deExpression expression) {
  deExpression identExpression = deExpressionGetFirstExpression(expression);
  deBindingRemoveExpression(deExpressionGetBinding(expression), identExpression);
}

// Queue a modular expression of the form <expresion> mod <modulus>.  The
// modulus should be bound first, and in the handler for binding modint
// expressions, we should recursively bind through modular arithmetic operators.
static void queueModintExpression(deBinding binding, deExpression modExpr, bool instantiating) {
  deExpression child  = deExpressionGetFirstExpression(modExpr);
  deExpression modulus = deExpressionGetNextExpression(child);
  // Bind the modulus first so we know its type.
  deQueueExpression(binding, modulus, instantiating, false);
  // The modint handler expects the modint expression next, from which it will
  // recurse down to set the modint type.
  deBindingAppendExpression(binding, modExpr);
  deQueueExpression(binding, child, instantiating, false);
}

// Queue the expression for binding.
void deQueueExpression(deBinding binding, deExpression expression, bool instantiating, bool lhs) {
  deExpressionSetInstantiating(expression, instantiating);
  deExpressionSetLhs(expression, lhs);
  deExpressionType type = deExpressionGetType(expression);
  if (type == DE_EXPR_MODINT) {
    return queueModintExpression(binding, expression, instantiating);
    return;
  }
  bool firstTime = true;
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    // Only the first sub-expression is ever not instantiated.
    bool instantiateSubExpr = instantiating && (!firstTime || instantiateSubExpressions(type));
    bool childLhs = (type == DE_EXPR_EQUALS && firstTime) || lhs;
    firstTime = false;
    deQueueExpression(binding, child, instantiateSubExpr, childLhs);
  } deEndExpressionExpression;
  // All child expressions are queued before this one.
  deBindingAppendExpression(binding, expression);
  if (type == DE_EXPR_EQUALS) {
    postProcessAssignment(expression);
  } else if (type == DE_EXPR_DOT) {
    postProcessDotExpression(expression);
  } else if (type == DE_EXPR_NAMEDPARAM) {
    postProcessNamedParameterExpression(expression);
  }
}

// Forward reference for recursion.
void deQueueBlockStatements(deSignature signature, deBlock block, bool instantiating);

// We only bind the first typeswitch sub-block that matches.  It is queued as a
// post-process to binding the typeswitch statement.  However, before we can
// select the matching case, we must bind all the case expressions.  We add all
// of the case expressions to the same binding as the typeswitch statement so
// that we can select the matching case as soon as the binding is done.
static void queueTypeswitchCases(deBinding binding, deStatement typeswitchStatement) {
  deBlock subBlock = deStatementGetSubBlock(typeswitchStatement);
  deStatement caseStatement;
  deForeachBlockStatement(subBlock, caseStatement) {
    if (deStatementGetType(caseStatement) == DE_STATEMENT_CASE) {
      deExpression typeExpressionList = deStatementGetExpression(caseStatement);
      deExpression typeExpression;
      deForeachExpressionExpression(typeExpressionList, typeExpression) {
        deQueueExpression(binding, typeExpression, false, false);
      } deEndExpressionExpression;
    }
  } deEndBlockStatement;
}

// Queue the statement and its sub-block's statements.
void deQueueStatement(deSignature signature, deStatement statement, bool instantiating) {
  if (deStatementIsImport(statement)) {
    return;
  }
  deExpression expression = deStatementGetExpression(statement);
  deBinding binding = deStatementGetBinding(statement);
  if (binding != deBindingNull) {
    // Rebind the statement in case we've made changes.
    deBindingDestroy(binding);
  }
  // Bind return statements even if they have no expression.
  binding = deBindingCreate(signature, statement, instantiating);
  if (expression != deExpressionNull) {
    deQueueExpression(binding, expression, instantiating, false);
  }
  deBlock subBlock = deStatementGetSubBlock(statement);
  if (deStatementGetType(statement) == DE_STATEMENT_TYPESWITCH) {
    queueTypeswitchCases(binding, statement);
  } else if (subBlock != deBlockNull) {
    deQueueBlockStatements(signature, subBlock, instantiating);
  }
}

// Throw all the expressions in the block into the queue to be bound.
void deQueueBlockStatements(deSignature signature, deBlock block, bool instantiating) {
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deQueueStatement(signature, statement, instantiating);
  } deEndBlockStatement;
}

// Create a Binding for the variable's default initializer value.
static void createDefaultValueBinding(deSignature signature, deVariable var) {
  deBinding binding = deVariableGetInitializerBinding(var);
  if (binding != deBindingNull) {
    // Rebind the initializer in case we've made changes.
    deBindingDestroy(binding);
  }
  binding = deVariableInitializerBindingCreate(signature, var, true);
  deExpression expr = deVariableGetInitializerExpression(var);
  utAssert(expr != deExpressionNull);
  deQueueExpression(binding, expr, true, false);
}

// Create a Binding for a variable's type constraint.
void deCreateVariableConstraintBinding(deSignature signature, deVariable var) {
  deBinding binding = deVariableGetTypeBinding(var);
  if (binding != deBindingNull) {
    // Rebind the type constraint expression in case we've made changes.
    deBindingDestroy(binding);
  }
  binding = deVariableConstraintBindingCreate(signature, var);
  deExpression expr = deVariableGetTypeExpression(var);
  utAssert(expr != deExpressionNull);
  deQueueExpression(binding, expr, false, false);
}

// Create a Binding for a function's type constraint.
static void createFunctionConstraintBinding(deSignature signature, deFunction func) {
  deBinding binding = deFunctionGetTypeBinding(func);
  if (binding != deBindingNull) {
    // Rebind the type constraint expression in case we've made changes.
    deBindingDestroy(binding);
  }
  binding = deFunctionConstraintBindingCreate(signature, func);
  deExpression expr = deFunctionGetTypeExpression(func);
  utAssert(expr != deExpressionNull);
  deQueueExpression(binding, expr, false, false);
}

// Bind parameter variables from the signature.  For parameters with
// deNullDatatype, there must be a default value.  Create Binding objects for
// such variables.  Also create Binding objects for parameter type constraints.
static void bindSignatureParameters(deSignature signature) {
  deBlock block = deSignatureGetBlock(signature);
  deVariable variable = deBlockGetFirstVariable(block);
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    if (deParamspecGetDatatype(paramspec) == deDatatypeNull) {
      createDefaultValueBinding(signature, variable);
    } else {
      deVariableSetDatatype(variable, deParamspecGetDatatype(paramspec));
    }
    if (deVariableGetTypeExpression(variable) != deExpressionNull) {
      deCreateVariableConstraintBinding(signature, variable);
    }
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

// Find the struct datatype for the struct signature.  We have to wait until the
// signature for the struct call is bound so we know its variable types.
static deDatatype findStructDatatype(deSignature signature) {
  deBlock block = deSignatureGetBlock(signature);
  deDatatypeArray types = deDatatypeArrayAlloc();
  deVariable var;
  deForeachBlockVariable(block, var) {
    deDatatypeArrayAppendDatatype(types, deVariableGetDatatype(var));
  } deEndBlockVariable;
  return deStructDatatypeCreate(deSignatureGetUniquifiedFunction(signature), types,
      deSignatureGetLine(signature));
}

// Update an external signature.  Check that the return type and all variable
// parameters are concrete.  Mark all parameters as instantiated.
static void updateExternSignature(deSignature signature) {
  deFunction function = deGetSignatureFunction(signature);
  deExpression typeExpr = deFunctionGetTypeExpression(function);
  deBlock block = deSignatureGetBlock(signature);
  deVariable var = deBlockGetFirstVariable(block);
  deParamspec param;
  deForeachSignatureParamspec(signature, param) {
    deDatatype datatype = deVariableGetDatatype(var);
    utAssert(datatype != deDatatypeNull);
    if (!deDatatypeConcrete(datatype)) {
      printf("%s type: %s\n", deVariableGetName(var), deDatatypeGetTypeString(datatype));
      deSigError(signature, "Extern function parameter types must be concrete");
    }
    deVariableSetInstantiated(var, true);
    deParamspecSetInstantiated(param, true);
    var = deVariableGetNextBlockVariable(var);
  } deEndSignatureParamspec;
  if (typeExpr == deExpressionNull) {
    // If it exists, we wait until it is bound to set the return type.
    deSignatureSetReturnType(signature, deNoneDatatypeCreate());
    if (!deSignatureBound(signature)) {
      deSignatureSetBound(signature, true);
      deQueueEventBlockedBindings(deSignatureGetReturnEvent(signature));
    }
  }
}

// Verify that the cases in the switch statement have the same type as the
// switch expression.
static void verifyCaseTypes(deBlock block) {
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deBlock subBlock = deStatementGetSubBlock(statement);
    if (deStatementGetType(statement) == DE_STATEMENT_SWITCH) {
      deExpression switchExpr = deStatementGetExpression(statement);
      deDatatype datatype = deExpressionGetDatatype(switchExpr);
      if (deExpressionIsType(switchExpr)) {
        deExprError(switchExpr,
            "Cannot switch on a type.  Did you mean typeswitch?");
      }
      deStatement caseStatement;
      deForeachBlockStatement(subBlock, caseStatement) {
        if (deStatementGetType(caseStatement) == DE_STATEMENT_CASE) {
          deExpression listExpression = deStatementGetExpression(caseStatement);
          deExpression expression;
          deForeachExpressionExpression(listExpression, expression) {
            if (deExpressionGetDatatype(expression) != datatype) {
              deExprError(expression,
                  "Case expression has different type than switch expression:%s",
                  deGetOldVsNewDatatypeStrings(deExpressionGetDatatype(expression), datatype));
            }
          } deEndExpressionExpression;
        }
      } deEndBlockStatement;
    } else if(subBlock != deBlockNull) {
      verifyCaseTypes(subBlock);
    }
  } deEndBlockStatement;
}

// Once we finish binding a signature, update its paramspecs.
static void updateSignature(deSignature signature) {
  deBlock block = deSignatureGetBlock(signature);
  verifyCaseTypes(block);
  deVariable var = deBlockGetFirstVariable(block);
  deParamspec param;
  deForeachSignatureParamspec(signature, param) {
    utAssert(var != deVariableNull && deVariableGetType(var) == DE_VAR_PARAMETER);
    deParamspecSetIsType(param, deVariableIsType(var));
    if (!deVariableConst(var)) {
      // Mark all var parameters as instantiated.
      deVariableSetInstantiated(var, true);
    }
    deParamspecSetInstantiated(param, deVariableInstantiated(var));
    var = deVariableGetNextBlockVariable(var);
  } deEndSignatureParamspec;
  utAssert(var == deVariableNull || deVariableGetType(var) != DE_VAR_PARAMETER);
  deForeachBlockVariable(deSignatureGetBlock(signature), var) {
    if (deVariableIsType(var) && deVariableInstantiated(var)) {
      deSigError(signature, "Variable %s is assigned a type, but also instantiated",
              deVariableGetName(var));
    }
  } deEndBlockVariable;
  deFunction function = deGetSignatureFunction(signature);
  deFunctionType type = deFunctionGetType(function);
  if (type == DE_FUNC_DESTRUCTOR) {
    // The self variable of destructors needs to be marked as instantiated.
    deParamspecSetInstantiated(deSignatureGetiParamspec(signature, 0), true);
  } else if (type == DE_FUNC_STRUCT) {
    deSignatureSetReturnType(signature, findStructDatatype(signature));
    deQueueEventBlockedBindings(deSignatureGetReturnEvent(signature));
    deSignatureSetBound(signature, true);
  } else if (deFunctionExtern(function)) {
    updateExternSignature(signature);
  }
  // If a function ends in throw, it may not have a return statement to
  // determine the return type.
  if (deSignatureGetReturnType(signature) == deDatatypeNull) {
    deSignatureSetReturnType(signature, deNoneDatatypeCreate());
    deSignatureSetBound(signature, true);
    deQueueEventBlockedBindings(deSignatureGetReturnEvent(signature));
  }
}

// Add a signature to the binding queue.
void deQueueSignature(deSignature signature) {
  if (deSignatureQueued(signature) ||
      (deSignatureBound(signature) && !deSignatureInstantiated(signature))) {
    return;  // Already queued this signature.
  }
  deSignatureSetQueued(signature, true);
  deBlock block = deSignatureGetBlock(signature);
  deBlockComputeReachability(block);
  deFunction func = deGetSignatureFunction(signature);
  deFunctionType type = deFunctionGetType(func);
  if (deBlockCanContinue(block) && !deFunctionBuiltin(func) &&
      type != DE_FUNC_ITERATOR && type != DE_FUNC_STRUCT && !deFunctionExtern(func)) {
    addReturnIfMissing(block);
  }
  bindSignatureParameters(signature);
  if (deFunctionGetTypeExpression(func) != deExpressionNull) {
    createFunctionConstraintBinding(signature, func);
  }
  deQueueBlockStatements(signature, block, true);
  if (deSignatureGetFirstBinding(signature) == deBindingNull) {
    // This signature has nothing to bind.
    updateSignature(signature);
  }
}

// Bind signatures until done.  This can be called multiple times, to bind new
// statements and functions.
void deBindAllSignatures(void) {
  deBinding binding = deRootGetFirstBinding(deTheRoot);
  while (binding != deBindingNull) {
    deRootRemoveBinding(deTheRoot, binding);
    deBindStatement(binding);
    if (deBindingGetFirstExpression(binding) ==  deExpressionNull) {
      // The expression tree is now fully bound.
      deSignature signature = deBindingGetSignature(binding);
      if (signature != deSignatureNull) {
        deSignatureRemoveBinding(signature, binding);
        if (deSignatureGetFirstBinding(signature) == deBindingNull) {
          // The signature is now fully bound.
          updateSignature(signature);
          if (deBlockIsConstructor(deSignatureGetBlock(signature))) {
            deGenerateDefaultMethods(deDatatypeGetClass(deSignatureGetReturnType(signature)));
          }
        }
      }
    }
    binding = deRootGetFirstBinding(deTheRoot);
  }
}

// Destroy contents of templates that were never constructed.  Delete relations
// with the template, and generated all generated code from those relations.
static void destroyUnusedTemplateContents(void) {
  // This iterator is tricky because if we destroy an template, and it has an
  // inner template, we'll destroy that too, breaking the assumption made by the
  // auto-generated safe iterators.  Inner templates are always after their
  // outer templates, so it should be safe to destroy them in a backwards
  // traversal of templates.
  deTemplate templ, prevTemplate;
  for (templ = deRootGetLastTemplate(deTheRoot); templ != deTemplateNull;
       templ = prevTemplate) {
    prevTemplate = deTemplateGetPrevRootTemplate(templ);
    uint32 numClasses = deTemplateGetNumClasses(templ);
    if (!deTemplateBuiltin(templ) && (numClasses == 0 || (numClasses == 1 &&
        deClassGetFirstSignature(deTemplateGetFirstClass( templ)) == deSignatureNull))) {
      deDestroyTemplateContents(templ);
    }
  }
}

// Report the event and exit.
static void reportEvent(deEvent event) {
  deBinding binding = deEventGetFirstBinding(event);
  utAssert(binding != deBindingNull);
  deSignature signature = deBindingGetSignature(binding);
  if (signature != deSignatureNull) {
    deCurrentSignature = signature;
    deCurrentStatement = deSignatureGetCallStatement(signature);
  }
  signature = deEventGetReturnSignature(event);
  if (signature != deSignatureNull) {
    deDumpSignature(signature);
    putchar('\n');
    deReportError(deSignatureGetLine(signature), "Unable to determine return type");
    return;
  }
  deVariable variable = deEventGetVariable(event);
  if (variable != deVariableNull) {
     deReportError(deVariableGetLine(variable), "Could not determine type of variable %s",
         deVariableGetName(variable));
     return;
  }
  deIdent undefinedIdent = deEventGetUndefinedIdent(event);
  utAssert(undefinedIdent != deIdentNull);
  deExpression expression = deBindingGetFirstExpression(binding);
  deExprError(expression, "Undefined identifier %s", deIdentGetName(undefinedIdent));
}

// Report errors for any undefined or unbound identifiers that remain, and
// exit if any exist.
void deReportEvents(void) {
  deEvent event;
  deSafeForeachRootEvent(deTheRoot, event) {
    if (deEventGetFirstBinding(event) == deBindingNull) {
      // This can happen if we destroy the statements that were blocked.
      deEventDestroy(event);
    } else if (deEventGetType(event) == DE_EVENT_UNDEFINED) {
      reportEvent(event);
    }
  } deEndSafeRootEvent;
  deSafeForeachRootEvent(deTheRoot, event) {
    if (deEventGetType(event) != DE_EVENT_UNDEFINED) {
      reportEvent(event);
    }
  } deEndSafeRootEvent;
  if (deRootGetFirstEvent(deTheRoot) != deEventNull) {
    utExit("Exiting due to errors...");
  }
}

// Bind expressions everywhere.
void deBind(void) {
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  deFunction mainFunc = deBlockGetOwningFunction(rootBlock);
  deSignature mainSignature = deSignatureCreate(mainFunc,
      deDatatypeArrayAlloc(), deFunctionGetLine(mainFunc));
  deSignatureSetInstantiated(mainSignature, true);
  deQueueSignature(mainSignature);
  deBindAllSignatures();
  destroyUnusedTemplateContents();
  deReportEvents();
}

// Bind extern RPCs.  These have no implementation, but we need to generate code
// for them.
void deBindRPCs(void) {
  deFunction function;
  deForeachRootFunction(deTheRoot, function) {
    if (deFunctionGetLinkage(function) == DE_LINK_EXTERN_RPC &&
        deFunctionGetUniquifiedSignature(function) == deSignatureNull) {
      deCreateFullySpecifiedSignature(function);
    }
  } deEndRootFunction;
  deBindAllSignatures();
}
