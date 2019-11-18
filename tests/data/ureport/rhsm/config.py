import os

def initConfig():
    return myConfig()

class myConfig():
    def get(self, key, value):
        return os.path.abspath("data/ureport/certs/correct")
