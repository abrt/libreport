#!/usr/bin/env python
from snack import *

from meh.dump import ReverseExceptionDump
from meh.handler import *
from meh.ui.text import *

class Config:
      def __init__(self):
          self.programName = "abrt"
          self.programVersion = "2.0"
          self.attrSkipList = []
          self.fileList = []
          self.config_value_one = 1
          self.config_value_two = 2

s = SnackScreen()
config = Config()
intf = TextIntf(screen=s)
handler = ExceptionHandler(config, intf, ReverseExceptionDump)
handler.install(None)

l = Button('divide by zero!')
gf = GridForm(s, 'test', 1, 1)
gf.add(l, 0, 0)
result = gf.run()
if result == l:
   1/0

s.popWindow()
s.finish()
