#!/bin/python3
import sys
import os
import platform
import subprocess

class Builder():

    def __init__(self):
        self.sources = [
            "main.c",
            "bstrlib/bstrlib.c",
        ]
        self.name = "prog"
        self.objs: list[str] = []
        self.firstArg = 1

        self.bstrzip = "https://github.com/websnarf/bstrlib/archive/refs/tags/v1.0.0.zip"
        self.bstrzipName = "bstrlib.zip"
        self.bstrlibFolder = self.bstrzipName.replace(".zip", "")

        self.normalAnsi = "\x1B[0m"
        self.greenAnsi = "\x1B[32m"
        self.redAnsi= "\x1B[31m"

        self.EXIT_FAILURE: int = 1

        if (platform.system() == "Linux"):
            self.cc = "cc"
            self.cflags = "-Wall -Wextra -pedantic -std=c99"
            self.cflagsDebug = "-g"
            self.cflagsRelease = "-O3 -march=native"
            self.ldflags = ""
            self.outTac = "-o"
            self.objectExt = ".o"
            self.compileTac = "-c"
            self.platformTac = ""
            self.rm = "rm -f"
            self.rmFolder = "rm -f -r"
            self.curl = "curl"
            self.unzip = "unzip"
            self.mv = "mv"
        elif (platform.system() == "Windows"):
            self.name += ".exe"
            self.cc = "cl"
            self.cflags = "/W4"
            self.cflagsDebug = "/Zi"
            self.cflagsRelease = "/O2"
            self.ldflags = "ws2_32.lib"
            self.outTac = "/Fe:"
            self.objectExt = ".obj"
            self.compileTac = "/c"
            self.platformTac = "/DPLATFORM_WINDOWS"
            self.rm = "del"
            self.rmFolder = "del"
            self.curl = "curl.exe"
            self.unzip = "tar -xf"
            self.mv = "move"
        else:
            print("system type not detected")
            sys.exit(self.EXIT_FAILURE)

        self.objRecompiled: bool = False

    def runCommand(self, command: str):
        print(f"{self.greenAnsi}{command}{self.normalAnsi}")
        if (subprocess.run(command, shell=True).returncode != 0):
            print(f"{self.redAnsi}failed command:\n{command}{self.normalAnsi}")
            sys.exit(self.EXIT_FAILURE)

    def compileSources(self):
        global objRecompiled
        for i, item in enumerate(self.sources):
            cfileTime = os.path.getmtime(item)
            try:
                ofileTime = os.path.getmtime(self.objs[i])
            except FileNotFoundError:
                ofileTime = 0
            if (cfileTime < ofileTime):
                continue
            objRecompiled = True
            command = " ".join([self.cc, self.compileTac, item, self.outTac, self.objs[i], self.cflags, self.platformTac])
            self.runCommand(command);

    def link(self):
        self.compileSources()
        if (objRecompiled):
            command = " ".join([self.cc, " ".join(self.objs), self.outTac, self.name, self.ldflags])
            self.runCommand(command);
        else:
            print("no linking necesary")

    def clean(self):
        command = " ".join([self.rm, " ".join(self.objs), self.name])
        self.runCommand(command);

    def craftObjs(self):
        for i in self.sources:
            self.objs.append(i.replace(".c", self.objectExt))

    def debug(self):
        self.cflags += f" {self.cflagsDebug}"
        self.link()

    def release(self):
        self.cflags += f" {self.cflagsRelease}"
        self.link()

    def default(self):
        self.debug()

    def getbstr(self):
        command = " ".join([self.curl, "-L", self.bstrzip, ">", self.bstrzipName])
        self.runCommand(command)
        command = " ".join([self.unzip, self.bstrzipName])
        self.runCommand(command)
        command = " ".join([self.rm, self.bstrzipName]);
        self.runCommand(command)
        command = " ".join([self.mv, "bstrlib-1.0.0", self.bstrlibFolder]);
        self.runCommand(command)

    def rmLibs(self):
        command = " ".join([self.rmFolder, self.bstrlibFolder]);
        self.runCommand(command)

    def getLibs(self):
        self.getbstr()

    def argumentCheck(self):
        if (len(sys.argv) > 1):
            for i in sys.argv[1:]:
                i = i.lower()
                if (i == "clean"):
                    self.clean()
                elif (i in ["lib", "libs", "getlib", "getlibs"]):
                    self.getLibs()
                elif (i in ["rmlib", "rmlibs"]):
                    self.rmLibs()
                elif (i in ["default", "def"]):
                    self.default()
                elif (i == "debug"):
                    self.debug()
                elif (i == "release"):
                    self.release()
                else:
                    print(f"unkown target: {self.redAnsi}{i}{self.normalAnsi}")
                    sys.exit(1)
        else:
            self.default()

if __name__ == "__main__":
    builder = Builder()
    builder.craftObjs()
    builder.argumentCheck()
