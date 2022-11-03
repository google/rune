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
  deTclass parent = deRelationGetParentTclass(relation);
  deTclass child = deRelationGetChildTclass(relation);
  deStringSprintf(string, " %s", deTclassGetName(parent));
  deString parentLabel = deRelationGetParentLabel(relation);
  if (deStringGetNumText(parentLabel) != 0) {
    deStringSprintf(string, ":%s", deStringGetCstr(parentLabel));
  }
  deStringSprintf(string, " %s", deTclassGetName(child));
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
  deStringSprintf(string, "member %s.%s -> %s\n", deTclassGetName(deClassGetTclass(parentClass)),
      deVariableGetName(var), deTclassGetName(deClassGetTclass(childClass)));
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
  deTclass tclass;
  deForeachRootTclass(deTheRoot, tclass) {
    deClass theClass;
    deForeachTclassClass(tclass, theClass) {
      deMemberRel memberRel;
      deForeachClassChildMemberRel(theClass, memberRel) {
        deDumpMemberRel(memberRel);
      } deEndClassChildMemberRel;
    } deEndTclassClass;
    deRelation relation;
    deForeachTclassChildRelation(tclass, relation) {
      deDumpRelation(relation);
    } deEndTclassChildRelation;
  } deEndRootTclass;
  fflush(stdout);
}

// Create a new relationship object between two tclasses.
deRelation deRelationCreate(deGenerator generator, deTclass parent, deString parentLabel,
    deTclass child, deString childLabel, bool cascadeDelete) {
  deRelation relation = deRelationAlloc();
  deRelationSetCascadeDelete(relation, cascadeDelete);
  deRelationSetParentLabel(relation, parentLabel);
  deRelationSetChildLabel(relation, childLabel);
  deTclassAppendChildRelation(parent, relation);
  deTclassAppendParentRelation(child, relation);
  deGeneratorAppendRelation(generator, relation);
  return relation;
}

// Return true if the tclass has a cascade-delete parent relationship.
static bool tclassHasCascadDeleteParent(deTclass tclass) {
  deRelation rel;
  deForeachTclassParentRelation(tclass, rel) {
    if (deRelationCascadeDelete(rel)) {
      return true;
    }
  } deEndTclassParentRelation;
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
        memberRelCreate(var, parentClass, childClass);
      }
    }
  } deEndBlockVariable;
}

// Set tclasses that are owned by a cascade-delete relationship as owned.
static void setRefCountedTclasses(void) {
  deTclass tclass;
  deForeachRootTclass(deTheRoot, tclass) {
    deTclassSetRefCounted(tclass, !tclassHasCascadDeleteParent(tclass));
  } deEndRootTclass;
}

// Visite tclasses reachable by traversing only child relationships.  If
// |targetTclass| is reached, report the loop as an error.
static bool visitReachableChildTclasses(deTclass targetTclass, deTclass tclass,
      deTclassArray visitedTclasses) {
  deTclassSetVisited(tclass, true);
  deTclassArrayAppendTclass(visitedTclasses, tclass);
  deRelation rel;
  deForeachTclassChildRelation(tclass, rel) {
    deTclass child = deRelationGetChildTclass(rel);
    if (child == targetTclass) {
      printf("Error: Relationship loop contains reference-counted class %s\n",
          deTclassGetName(targetTclass));
      deDumpRelation(rel);
      return true;
    } else if (!deTclassVisited(child) &&
        visitReachableChildTclasses(targetTclass, child, visitedTclasses)) {
      deDumpRelation(rel);
      return true;
    }
  } deEndTclassChildRelation;
  deClass parentClass;
  deForeachTclassClass(tclass, parentClass) {
    deMemberRel memberRel;
    deForeachClassChildMemberRel(parentClass, memberRel) {
      deClass childClass = deMemberRelGetChildClass(memberRel);
      deTclass child = deClassGetTclass(childClass);
      if (child == targetTclass) {
        printf("Error: Relationship loop contains reference-counted class %s\n",
               deTclassGetName(targetTclass));
        deDumpMemberRel(memberRel);
        return true;
      } else if (!deTclassVisited(child) &&
          visitReachableChildTclasses(targetTclass, child, visitedTclasses)) {
        deDumpMemberRel(memberRel);
        return true;
      }
    } deEndClassChildMemberRel;
  } deEndTclassClass;
  return false;
}

// Clear visited flags on all tclasses in the array.
static void clearVisitedFlags(deTclassArray visitedTclasses) {
  deTclass tclass;
  deForeachTclassArrayTclass(visitedTclasses, tclass) {
    deTclassSetVisited(tclass, false);
  } deEndTclassArrayTclass;
}

// Verify the relationship graph.  Mark Tclasses not in cascade-delete
// relationships as reference-counted.  Generate an error for reference-counted
// class loops.
void deVerifyRelationshipGraph(void) {
  setRefCountedTclasses();
  deTclassArray visitedTclasses = deTclassArrayAlloc();
  deTclass tclass;
  deForeachRootTclass(deTheRoot, tclass) {
    if (deTclassRefCounted(tclass)) {
      if (visitReachableChildTclasses(tclass, tclass, visitedTclasses)) {
        utExit("Exiting due to error...");
      }
      clearVisitedFlags(visitedTclasses);
    }
  } deEndRootTclass;
  deTclassArrayFree(visitedTclasses);
}
