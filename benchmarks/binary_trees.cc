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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <memory>

class Node {
 public:
  uint32_t check() {
    uint32_t sum = 1;
    if (left_) {
      sum += left_->check();
      sum += right_->check();
    }
    return sum;
  }

  static std::unique_ptr<Node> makeTree(uint32_t depth) {
    auto node = std::make_unique<Node>();
    if (depth != 0) {
      node->left_ = makeTree(depth - 1);
      node->right_ = makeTree(depth - 1);
    }
    return node;
  }

 private:
  std::unique_ptr<Node> left_;
  std::unique_ptr<Node> right_;
};

int main(int argc, char **argv) {
  uint32_t minDepth = 4;
  uint32_t maxDepth = 10;
  if (argc > 1) {
    maxDepth = atoi(argv[1]);
  }
  uint32_t stretchDepth = maxDepth + 1;
  auto tree = Node::makeTree(stretchDepth);
  printf("stretch tree of depth %u\t check:%u\n", stretchDepth, tree->check());
  tree.reset();
  auto longLivedTree = Node::makeTree(maxDepth);
  uint32_t iterations = 1 << maxDepth;
  for (uint32_t depth = minDepth; depth < stretchDepth; depth += 2) {
    uint32_t checkTotal = 0;
    for (uint32_t i = 1; i < iterations + 1; i++) {
      auto tree = Node::makeTree(depth);
      checkTotal += tree->check();
      tree.reset();
    }
    printf("%u\t trees of depth %u\t check:%u\n", iterations, depth,
           checkTotal);
    iterations >>= 2;
  }
  printf("long lived tree of depth %u\t check:%u\n", maxDepth,
         longLivedTree->check());
  return 0;
}
