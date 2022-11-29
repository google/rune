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
