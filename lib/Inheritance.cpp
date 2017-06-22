// Copyright 2016-2017 The RamFuzz contributors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Inheritance.hpp"

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/Tooling.h"

using namespace std;

using namespace clang;
using namespace ast_matchers;
using namespace tooling;

namespace {

/// True iff C is visible outside all its parent contexts.
bool globally_visible(const CXXRecordDecl *C) {
  if (!C || !C->getIdentifier())
    // Anonymous classes may technically be visible, but only through tricks
    // like decltype.  Skip until there's a compelling use-case.
    return false;
  const auto acc = C->getAccess();
  if (acc == AS_private || acc == AS_protected)
    return false;
  const DeclContext *ctx = C->getLookupParent();
  while (!isa<TranslationUnitDecl>(ctx)) {
    if (auto ns = dyn_cast<NamespaceDecl>(ctx)) {
      if (ns->isAnonymousNamespace())
        return false;
      ctx = ns->getLookupParent();
      continue;
    } else
      return globally_visible(dyn_cast<CXXRecordDecl>(ctx));
  }
  return true;
}

} // anonymous namespace

namespace ramfuzz {

void InheritanceBuilder::tackOnto(clang::ast_matchers::MatchFinder &MF) {
  MF.addMatcher(cxxRecordDecl(isDefinition(),
                              unless(hasAncestor(namespaceDecl(isAnonymous()))))
                    .bind("class"),
                this);
}

void InheritanceBuilder::run(const MatchFinder::MatchResult &Result) {
  if (const auto *C = Result.Nodes.getNodeAs<CXXRecordDecl>("class"))
    for (const auto &base : C->bases()) {
      const auto qname = C->getQualifiedNameAsString();
      if (base.getAccessSpecifier() == AS_public) {
        PrintingPolicy prtpol((LangOptions()));
        prtpol.SuppressTagKeyword = true;
        prtpol.SuppressScope = false;
        inh[base.getType()
                .getDesugaredType(*Result.Context)
                .getAsString(prtpol)]
            .insert(qname);
      }
      cdetails.set(qname, ClassDetails::is_template,
                   C->getDescribedClassTemplate());
      cdetails.set(qname, ClassDetails::is_visible, globally_visible(C));
    }
}

void InheritanceBuilder::process(const llvm::Twine &Code) {
  MatchFinder MF;
  tackOnto(MF);
  runToolOnCode(newFrontendActionFactory(&MF)->create(), Code);
}

} // namespace ramfuzz
