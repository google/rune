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

// Create a StateBinding object representing the state of binding of a statement
// for a given function signature.
deStateBinding deStateBindingCreate(deSignature signature, deStatement statement, bool instantiating) {
  deStateBinding statebinding = deStateBindingAlloc();
  deStateBindingSetInstantiated(statebinding, true);
  deStatementAppendStateBinding(statement, statebinding);
  deSignatureAppendStateBinding(signature, statebinding);
  deSignatureAppendBindingStateBinding(signature, statebinding);
  return statebinding;
}

// Create a binding object representing an expression that is in-flight binding
// for a specific function signature.
deBinding deExpressionBindingCreate(deSignature signature, deBinding owningBinding,
    deExpression expression, bool instantiating) {
  deBinding binding = deBindingAlloc();
  deBindingSetInstantiating(binding, instantiating);
  if (owningBinding != deBindingNull) {
    deBindingAppendBinding(owningBinding, binding);
  }
  deExpressionAppendBinding(expression, binding);
  if (signature != deSignatureNull) {
    deSignatureAppendBinding(signature, binding);
  }
  return binding;
}

// Create a binding object representing an expression that is in-flight binding
// for a specific function signature.  Signature can be null, such as when the
// variable is part of a structure, enumerated type, or is a global.
deBinding deVariableBindingCreate(deSignature signature, deVariable variable) {
  deBinding binding = deBindingAlloc();
  deVariableAppendBinding(variable, binding);
  if (signature != deSignatureNull) {
    deSignatureAppendBinding(signature, binding);
  }
  return binding;
}

// Create a binding object representing an expression that is in-flight binding
// for a specific function signature.
deBinding deParameterBindingCreate(deSignature signature, deVariable variable,
    deParamspec paramspec) {
  deBinding binding = deVariableBindingCreate(signature, variable);
  deBindingSetDatatype(binding, deParamspecGetDatatype(paramspec));
  deBindingSetInstantiating(binding, deParamspecInstantiated(paramspec));
  return binding;
}

// Create a new event and add it to Root.
static deEvent createEvent(void) {
  deEvent event = deEventAlloc();
  deRootAppendEvent(deTheRoot, event);
  return event;
}

// Create a Event that will keep track of all StateBindings blocked on binding
// of signature.  If it already exists, just return it.
deEvent deSignatureEventCreate(deSignature signature) {
  deEvent event = deSignatureGetReturnEvent(signature);
  if (event != deEventNull) {
    return event;
  }
  event = createEvent();
  deSignatureInsertReturnEvent(signature, event);
  return event;
}

// Create a variable event that tracks variable assignments.
deEvent deVariableEventCreate(deBinding varBinding) {
  deEvent event = deBindingGetVariableEvent(varBinding);
  if (event != deEventNull) {
    return event;
  }
  event = createEvent();
  deBindingInsertVariableEvent(varBinding, event);
  return event;
}

// Create a Event that tracks undefined identifiers.
deEvent deUndefinedIdentEventCreate(deIdent ident) {
  deEvent event = deIdentGetUndefinedEvent(ident);
  if (event != deEventNull) {
    return event;
  }
  event = createEvent();
  deIdentInsertUndefinedEvent(ident, event);
  return event;
}

// Find the binding for the variable on the signature.
deBinding deFindVariableBinding(deSignature signature, deVariable variable) {
  deBinding binding;
  deForeachVariableBinding(variable, binding) {
    if (deBindingGetSignature(binding) == signature) {
      return binding;
    }
  } deEndVariableBinding;
  return deBindingNull;
}

// Find the binding for the identifier on the signature.
deBinding deFindIdentBinding(deSignature signature, deIdent ident) {
  deBinding binding;
  deForeachIdentBinding(ident, binding) {
    if (deBindingGetSignature(binding) == signature) {
      return binding;
    }
  } deEndIdentBinding;
  return deBindingNull;
}

// Find the statebinding owning this binding.  Bindings are in a tree, so recurse
// up until we find the root.
deStateBinding deFindBindingStateBinding(deBinding binding) {
  while (binding != deBindingNull && deBindingGetRootStateBinding(binding) == deStateBindingNull) {
    binding = deBindingGetBinding(binding);
  }
  if (binding == deBindingNull) {
    return deStateBindingNull;
  }
  return deBindingGetRootStateBinding(binding);
}

