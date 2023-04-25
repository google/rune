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

// Dump a relationship to the end of |string| for debugging.
void deDumpRelationStr(deString string, deRelation relation) {
  deStringSprintf(string, "relation %s", deGeneratorGetName(deRelationGetGenerator(relation)));
  deTemplate parent = deRelationGetParentTemplate(relation);
  deTemplate child = deRelationGetChildTemplate(relation);
  deStringSprintf(string, " %s", deTemplateGetName(parent));
  deString parentLabel = deRelationGetParentLabel(relation);
  if (deStringGetNumText(parentLabel) != 0) {
    deStringSprintf(string, ":%s", deStringGetCstr(parentLabel));
  }
  deStringSprintf(string, " %s", deTemplateGetName(child));
  deString childLabel = deRelationGetChildLabel(relation);
  if (deStringGetNumText(childLabel) != 0) {
    deStringSprintf(string, ":%s", deStringGetCstr(childLabel));
  }
  if (deRelationCascadeDelete(relation)) {
    deStringPuts(string, " cascade");
  }
  deStringPuts(string, "\n");
}

// Dump a relationship to stdout for debugging.
void deDumpRelation(deRelation relation) {
  deString string = deMutableStringCreate();
  deDumpRelationStr(string, relation);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Dump a MemberRel object to the end of |string| for debugging.
void deDumpMemberRelStr(deString string, deMemberRel memberRel) {
  deVariable var = deMemberRelGetVariable(memberRel);
  deClass parentClass = deMemberRelGetParentClass(memberRel);
  deClass childClass = deMemberRelGetChildClass(memberRel);
  deStringSprintf(string, "member %s.%s -> %s\n", deTemplateGetName(deClassGetTemplate(parentClass)),
      deVariableGetName(var), deTemplateGetName(deClassGetTemplate(childClass)));
}

// Dump a MemberRel object to stdout for debugging.
void deDumpMemberRel(deMemberRel memberRel) {
  deString string = deMutableStringCreate();
  deDumpMemberRelStr(string, memberRel);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Dump all relations.
void deDumpRelations(void) {
  deTemplate templ;
  deForeachRootTemplate(deTheRoot, templ) {
    deClass theClass;
    deForeachTemplateClass(templ, theClass) {
      deMemberRel memberRel;
      deForeachClassChildMemberRel(theClass, memberRel) {
        deDumpMemberRel(memberRel);
      } deEndClassChildMemberRel;
    } deEndTemplateClass;
    deRelation relation;
    deForeachTemplateChildRelation(templ, relation) {
      deDumpRelation(relation);
    } deEndTemplateChildRelation;
  } deEndRootTemplate;
  fflush(stdout);
}

// Create a new relationship object between two templates.
deRelation deRelationCreate(deGenerator generator, deTemplate parent, deString parentLabel,
    deTemplate child, deString childLabel, bool cascadeDelete) {
  deRelation relation = deRelationAlloc();
  deRelationSetCascadeDelete(relation, cascadeDelete);
  deRelationSetParentLabel(relation, parentLabel);
  deRelationSetChildLabel(relation, childLabel);
  deTemplateAppendChildRelation(parent, relation);
  deTemplateAppendParentRelation(child, relation);
  deGeneratorAppendRelation(generator, relation);
  return relation;
}

// Return true if the template has a cascade-delete parent relationship.
static bool templHasCascadDeleteParent(deTemplate templ) {
  deRelation rel;
  deForeachTemplateParentRelation(templ, rel) {
    if (deRelationCascadeDelete(rel)) {
      return true;
    }
  } deEndTemplateParentRelation;
  return false;
}

// Create a member relationship, which represents a class member of class type.
// The child must be a reference counted class.
static deMemberRel memberRelCreate(deVariable variable, deClass parentClass, deClass childClass) {
  deMemberRel memberRel = deMemberRelAlloc();
  deClassAppendChildMemberRel(parentClass, memberRel);
  deClassAppendParentMemberRel(childClass, memberRel);
  deVariableInsertMemberRel(variable, memberRel);
  return memberRel;
}

// Add member relations for each class-type member in the class.
void deAddClassMemberRelations(deClass parentClass) {
  deBlock block = deClassGetSubBlock(parentClass);
  deVariable var;
  deForeachBlockVariable(block, var) {
    if (deVariableGetType(var) == DE_VAR_LOCAL && !deVariableGenerated(var)) {
      deDatatype datatype = deVariableGetDatatype(var);
      if (deDatatypeGetType(datatype) == DE_TYPE_CLASS) {
        deClass childClass = deDatatypeGetClass(datatype);
        if (!deTemplateRefCounted(deClassGetTemplate(childClass))) {
          deError(deVariableGetLine(var), "Viariable %s instantiates a cascade-delete class",
              deVariableGetName(var));
        }
        memberRelCreate(var, parentClass, childClass);
      }
    }
  } deEndBlockVariable;
}

// Add class member relations for all classes.
static void addMemberRels(void) {
  deClass theClass;
  deForeachRootClass(deTheRoot, theClass) {
    deAddClassMemberRelations(theClass);
  } deEndRootClass;
}

// Set templates that are owned by a cascade-delete relationship as owned.
static void setRefCountedTemplates(void) {
  deTemplate templ;
  deForeachRootTemplate(deTheRoot, templ) {
    deTemplateSetRefCounted(templ, !templHasCascadDeleteParent(templ));
  } deEndRootTemplate;
}

// Check that destuctors for ref-counted classes are never called other than
// from generated code.
static void checkDestroyCalls(void) {
  deSignature signature;
  deForeachRootSignature(deTheRoot, signature) {
    deFunction function = deSignatureGetFunction(signature);
    if (deFunctionGetType(function) == DE_FUNC_DESTRUCTOR) {
      deBlock owningBlock = deFunctionGetBlock(function);
      utAssert(deBlockGetType(owningBlock) == DE_BLOCK_FUNCTION);
      deFunction templFunc = deBlockGetOwningFunction(owningBlock);
      utAssert(deFunctionGetType(templFunc) == DE_FUNC_CONSTRUCTOR);
      deTemplate templ = deFunctionGetTemplate(templFunc);
      if (deTemplateRefCounted(templ)) {
        deIdent ident;
        deForeachFunctionIdent(function, ident) {
          deExpression expression;
          deForeachIdentExpression(ident, expression) {
            if (!deStatementGenerated(deFindExpressionStatement(expression))) {
              deError(deExpressionGetLine(expression),
                  "Referenced destroy method ref-counted class %s from non-genereated code",
                  deTemplateGetName(templ));
            }
          } deEndIdentExpression;
        } deEndFunctionIdent;
      }
    }
  } deEndRootSignature;
}

// Visite templates reachable by traversing only child relationships.  If
// |targetTemplate| is reached, report the loop as an error.
static bool visitReachableChildTemplates(deTemplate targetTemplate, deTemplate templ,
      deTemplateArray visitedTemplates) {
  deTemplateSetVisited(templ, true);
  deTemplateArrayAppendTemplate(visitedTemplates, templ);
  deRelation rel;
  deForeachTemplateChildRelation(templ, rel) {
    deTemplate child = deRelationGetChildTemplate(rel);
    if (child == targetTemplate) {
      printf("Error: Relationship loop contains reference-counted class %s\n",
          deTemplateGetName(targetTemplate));
      deDumpRelation(rel);
      return true;
    } else if (!deTemplateVisited(child) &&
        visitReachableChildTemplates(targetTemplate, child, visitedTemplates)) {
      deDumpRelation(rel);
      return true;
    }
  } deEndTemplateChildRelation;
  deClass parentClass;
  deForeachTemplateClass(templ, parentClass) {
    deMemberRel memberRel;
    deForeachClassChildMemberRel(parentClass, memberRel) {
      deClass childClass = deMemberRelGetChildClass(memberRel);
      deTemplate child = deClassGetTemplate(childClass);
      if (child == targetTemplate) {
        printf("Error: Relationship loop contains reference-counted class %s\n",
               deTemplateGetName(targetTemplate));
        deDumpMemberRel(memberRel);
        return true;
      } else if (!deTemplateVisited(child) &&
          visitReachableChildTemplates(targetTemplate, child, visitedTemplates)) {
        deDumpMemberRel(memberRel);
        return true;
      }
    } deEndClassChildMemberRel;
  } deEndTemplateClass;
  return false;
}

// Clear visited flags on all templates in the array.
static void clearVisitedFlags(deTemplateArray visitedTemplates) {
  deTemplate templ;
  deForeachTemplateArrayTemplate(visitedTemplates, templ) {
    deTemplateSetVisited(templ, false);
  } deEndTemplateArrayTemplate;
}

// Verify the relationship graph.  Mark templates not in cascade-delete
// relationships as reference-counted.  Generate an error for reference-counted
// class loops.
void deVerifyRelationshipGraph(void) {
  setRefCountedTemplates();
  checkDestroyCalls();
  addMemberRels();
  deTemplateArray visitedTemplates = deTemplateArrayAlloc();
  deTemplate templ;
  deForeachRootTemplate(deTheRoot, templ) {
    if (deTemplateRefCounted(templ)) {
      if (visitReachableChildTemplates(templ, templ, visitedTemplates)) {
        deError(deTemplateGetLine(templ),
            "To avoid potential memory leaks, consider using cascade-delete relations.");
      }
      clearVisitedFlags(visitedTemplates);
    }
  } deEndRootTemplate;
  deTemplateArrayFree(visitedTemplates);
}
