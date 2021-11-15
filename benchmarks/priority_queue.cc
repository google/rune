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

#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <vector>

uint32_t hashValues(uint32_t val1, uint32_t val2) {
  return 0xdeadbeef*val1 ^ val2;
}

class Element {
public:
  explicit Element(const std::string &name, uint32_t cost): cost_(cost), name_(name) {
    // std::cout << "Creating element with cost " << cost << '\n';
  }
  uint32_t cost_;
  std::string name_;
};

template<typename T> void hash_queue(T& q) {
  uint32_t total_hash = 0;
  while(!q.empty()) {
    Element *element = q.top();
    // std::cout << "Popped element with cost " << element.cost_ << '\n';
    total_hash = hashValues(total_hash, element->cost_);
    q.pop();
  }
  std::cout << total_hash << '\n';
}

int main() {
  // Using lambda to compare elements.
  auto cmp = [](const Element *left, const Element *right) { return left->cost_ > right->cost_; };
  std::priority_queue<Element, std::vector<Element*>, decltype(cmp)> q(cmp);
  uint32_t cost = 1;
  // Currently, std::priority_queue does not work with unque pointers, so we
  // have to use this unsafe hack, which makes the benchmark faster for C++,
  // since it is sqapping pointers, rather than unique pointers.
  std::vector<std::unique_ptr<Element>> elements;
  for (uint32_t i = 0; i < 1 << 20; i++) {
    cost = hashValues(cost, i);
    auto element = std::make_unique<Element>("foo" + std::to_string(i), cost);
    elements.push_back(std::move(element));
  }
  for (uint32_t i = 0; i < 10; i++) {
    for (uint32_t j = 0; j < 1 << 20; j++) {
      Element *element = elements[j].get();
      q.push(element);
    }
    hash_queue(q);
  }
}
