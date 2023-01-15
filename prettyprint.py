#!/bin/bash
#  Copyright 2022 Google LLC.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import gdb.printing


class ClassPrinter:

  def __init__(self, val):
    self.val = val

  def to_string(self):
    className = self.val.type.name[6:].replace(".", "_")
    gdb.parse_and_eval("%s_show(%u)" % (className, self.val))
    return "%u" % self.val


def build_pretty_printer():
  pp = gdb.printing.RegexpCollectionPrettyPrinter("my_library")
  pp.add_printer("class", "^class", ClassPrinter)
  return pp


gdb.printing.register_pretty_printer(gdb.current_objfile(),
                                     build_pretty_printer())
