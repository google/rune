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

#include "de.h"

// Create a Binding object representing the state of binding of a statement
// for a given function signature.
static deBinding bindingCreate(deSignature signature, deBindingType type, bool instantiating) {
  deBinding binding = deBindingAlloc();
  deBindingSetType(binding, type);
  deBindingSetInstantiated(binding, instantiating);
  if (signature != deSignatureNull) {
    deSignatureAppendBinding(signature, binding);
  }
  deRootAppendBinding(deTheRoot, binding);
  return binding;
}

// Create a Binding object representing the state of binding of a statement.
deBinding deBindingCreate(deSignature signature, deStatement statement, bool instantiating) {
  deBinding binding = bindingCreate(signature, DE_BIND_STATEMENT, instantiating);
  deStatementInsertBinding(statement, binding);
  return binding;
}

// Create a Binding object to help bind a default value initializer.
deBinding deVariableInitializerBindingCreate(deSignature signature,
    deVariable variable, bool instantiating) {
  deBinding binding = bindingCreate(signature, DE_BIND_DEFAULT_VALUE, instantiating);
  deVariableInsertInitializerBinding(variable, binding);
  return binding;
}

// Create a Binding object to help bind a variable type constraint.
deBinding deVariableConstraintBindingCreate(deSignature signature,
    deVariable variable) {
  deBinding binding = bindingCreate(signature, DE_BIND_VAR_CONSTRAINT, false);
  deVariableInsertTypeBinding(variable, binding);
  return binding;
}

// Create a Binding object to help bind a function type constraint.
deBinding deFunctionConstraintBindingCreate(deSignature signature,
    deFunction function) {
  deBinding binding = bindingCreate(signature, DE_BIND_FUNC_CONSTRAINT, false);
  deFunctionInsertTypeBinding(function, binding);
  return binding;
}

// Create a new event and add it to Root.
static deEvent createEvent(deEventType type) {
  deEvent event = deEventAlloc();
  deEventSetType(event, type);
  deRootInsertEvent(deTheRoot, event);
  return event;
}

// Create a Event that will keep track of all Bindings blocked on binding of
// signature.  If it already exists, just return it.
deEvent deSignatureEventCreate(deSignature signature) {
  deEvent event = deSignatureGetReturnEvent(signature);
  if (event != deEventNull) {
    return event;
  }
  event = createEvent(DE_EVENT_SIGNATURE);
  deSignatureInsertReturnEvent(signature, event);
  return event;
}

// Create a variable event that tracks variable assignments.
deEvent deVariableEventCreate(deVariable variable) {
  deEvent event = deVariableGetEvent(variable);
  if (event != deEventNull) {
    return event;
  }
  event = createEvent(DE_EVENT_VARIABLE);
  deVariableInsertEvent(variable, event);
  return event;
}

// Create a Event that tracks undefined identifiers.
deEvent deUndefinedIdentEventCreate(deIdent ident) {
  deEvent event = deIdentGetUndefinedEvent(ident);
  if (event != deEventNull) {
    return event;
  }
  event = createEvent(DE_EVENT_UNDEFINED);
  deIdentInsertUndefinedEvent(ident, event);
  return event;
}

// Return the scope block containing the binding.
deBlock deGetBindingBlock(deBinding binding) {
  deSignature signature = deBindingGetSignature(binding);
  if (signature != deSignatureNull) {
    return deSignatureGetBlock(signature);
  }
  switch (deBindingGetType(binding)) {
    case DE_BIND_STATEMENT:
      return deBlockGetScopeBlock(deStatementGetBlock(deBindingGetStatement(binding)));
    case DE_BIND_DEFAULT_VALUE:
      return deVariableGetBlock(deBindingGetInitializerVariable(binding));
    case DE_BIND_VAR_CONSTRAINT:
      return deVariableGetBlock(deBindingGetTypeVariable(binding));
    case DE_BIND_FUNC_CONSTRAINT:
      return deFunctionGetSubBlock(deBindingGetTypeFunction(binding));
  }
  return deBlockNull;  // Dummy return
}
