#!/bin/python3
import sys
import os
import platform
import subprocess

class Builder():

    def __init__(self):
        self.sources = [
            "main.c",
        ]
        self.libSources = [
            "bstrlib/bstrlib.c",
            "stb_ds.c",
        ]
        self.name = "server"
        self.objs: list[str] = []
        self.libObjs: list[str] = []
        self.firstArg = 1

        self.bstrzip = "https://github.com/websnarf/bstrlib/archive/refs/tags/v1.0.0.zip"
        self.bstrzipName = "bstrlib.zip"
        self.bstrlibFolder = self.bstrzipName.replace(".zip", "")

        self.pthreadsLinks = [
            "https://ftp.funet.fi/pub/mirrors/sources.redhat.com/pub/pthreads-win32/prebuilt-dll-2-8-0-release/lib/pthreadVC2.lib", # lib needs to be first
            "https://ftp.funet.fi/pub/mirrors/sources.redhat.com/pub/pthreads-win32/prebuilt-dll-2-8-0-release/lib/pthreadVC2.dll",
            "https://ftp.funet.fi/pub/mirrors/sources.redhat.com/pub/pthreads-win32/prebuilt-dll-2-8-0-release/include/pthread.h",
            "https://ftp.funet.fi/pub/mirrors/sources.redhat.com/pub/pthreads-win32/prebuilt-dll-2-8-0-release/include/sched.h",
            "https://ftp.funet.fi/pub/mirrors/sources.redhat.com/pub/pthreads-win32/prebuilt-dll-2-8-0-release/include/semaphore.h",
        ]
        self.pthreadsFiles = [
            "pthreadVC2.lib", # lib needs to be first
            "pthreadVC2.dll",
            "pthread.h",
            "sched.h",
            "semaphore.h",
        ]

        # might need to update this in the future
        self.stbDsLink = "https://raw.githubusercontent.com/nothings/stb/31707d14fdb75da66b3eed52a2236a70af0d0960/stb_ds.h"
        self.stbDsName = "stb_ds.h"

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
            self.objOutTac = self.outTac
            self.objectExt = ".o"
            self.compileTac = "-c"
            self.rm = "rm -f"
            self.rmFolder = "rm -f -r"
            self.curl = "curl"
            self.unzip = "unzip"
            self.mv = "mv"
        elif (platform.system() == "Windows"):
            self.name += ".exe"
            self.cc = "cl"
            self.cflags = "/W4 /I."
            self.cflagsDebug = "/Zi"
            self.cflagsRelease = "/O2"
            self.ldflags = "ws2_32.lib "+self.pthreadsFiles[0]
            self.outTac = "/Fe:"
            self.objOutTac = "/Fo:"
            self.objectExt = ".obj"
            self.compileTac = "/c"
            self.rm = "del"
            self.rmFolder = "del"
            self.curl = "curl.exe"
            self.unzip = "tar -xf"
            self.mv = "move"
            for i, item in enumerate(self.libSources):
                self.libSources[i] = item.replace("/", "\\")
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
        sources = self.sources + self.libSources
        objs = self.objs + self.libObjs
        for i, item in enumerate(sources):
            cfileTime = os.path.getmtime(item)
            try:
                ofileTime = os.path.getmtime(objs[i])
            except FileNotFoundError:
                ofileTime = 0
            if (cfileTime < ofileTime):
                continue
            self.objRecompiled = True
            if (item in self.libSources and platform.system() == "Linux"):
                cflags = self.cflags.replace("-Wall -Wextra -pedantic -std=c99 ", "")
            else:
                cflags = self.cflags
            command = " ".join([self.cc, self.compileTac, item, self.objOutTac, objs[i], cflags])
            self.runCommand(command);

    def link(self):
        self.compileSources()
        if (self.objRecompiled):
            command = " ".join([self.cc, " ".join(self.objs + self.libObjs), self.outTac, self.name, self.ldflags])
            self.runCommand(command);
        else:
            print("no linking necesary")

    def clean(self):
        command = " ".join([self.rm, " ".join(self.objs), self.name])
        self.runCommand(command);

    def craftObjs(self):
        for i in self.sources:
            self.objs.append(i.replace(".c", self.objectExt))
        for i in self.libSources:
            self.libObjs.append(i.replace(".c", self.objectExt))

    def debug(self):
        self.cflags += f" {self.cflagsDebug}"
        self.link()

    def release(self):
        self.cflags += f" {self.cflagsRelease}"
        self.link()

    def default(self):
        self.debug()

    def getStbDs(self):
        command = " ".join([self.curl, "-L", self.stbDsLink, ">", self.stbDsName])
        self.runCommand(command)

    def getbstr(self):
        command = " ".join([self.curl, "-L", self.bstrzip, ">", self.bstrzipName])
        self.runCommand(command)
        command = " ".join([self.unzip, self.bstrzipName])
        self.runCommand(command)
        command = " ".join([self.rm, self.bstrzipName]);
        self.runCommand(command)
        command = " ".join([self.mv, "bstrlib-1.0.0", self.bstrlibFolder]);
        self.runCommand(command)

    def getWin32Pthread(self):
        for i, item in enumerate(self.pthreadsLinks):
            command = " ".join([self.curl, "-L", item, ">", self.pthreadsFiles[i]])
            self.runCommand(command)

    def rmLibs(self):
        command = " ".join([self.rmFolder, self.bstrlibFolder, " ".join(self.pthreadsFiles), self.stbDsName]);
        self.runCommand(command)

    def getLibs(self):
        self.getbstr()
        self.getStbDs()
        if (platform.system() == "Windows"):
            self.getWin32Pthread;

    def ensureLibs(self):
        if (platform.system() == "Windows"):
            for i in self.pthreadsFiles:
                if (not os.path.exists(i)):
                    print(f"{self.redAnsi}could not find pthread files{self.normalAnsi}\nattempting installation")
                    self.getWin32Pthread()
                    for i in self.pthreadsFiles:
                        if (not os.path.exists(i)):
                            print(f"{self.redAnsi}un able to get pthread libs{self.normalAnsi}")
                            sys.exit(self.EXIT_FAILURE)
            print(f"{self.greenAnsi}succesfully found pthread files{self.normalAnsi}")
        if (not(os.path.exists(self.bstrlibFolder+"/bstrlib.c"))):
            print(f"{self.redAnsi}could not find bstrlib file{self.normalAnsi}\nattempting installation")
            self.getbstr()
            if (not(os.path.exists(self.bstrlibFolder+"/bstrlib.c"))):
                print(f"{self.redAnsi}could not find bstrlib file second time failing build{self.normalAnsi}\n")
                sys.exit(self.EXIT_FAILURE)
        print(f"{self.greenAnsi}succesfully found bstrlib{self.normalAnsi}")
        if (not(os.path.exists(self.stbDsName))):
            print(f"{self.redAnsi}could not find stb ds file{self.normalAnsi}\nattempting installation")
            self.getStbDs()
            if (not(os.path.exists(self.stbDsName))):
                print(f"{self.redAnsi}could not find stbds file second time failing build{self.normalAnsi}\n")
                sys.exit(self.EXIT_FAILURE)
        print(f"{self.greenAnsi}succesfully found stb ds{self.normalAnsi}")

    def cleanLibs(self):
        command = " ".join([self.rm, " ".join(self.libObjs)])
        self.runCommand(command)

    def argumentCheck(self):
        if (len(sys.argv) > 1):
            for i in sys.argv[1:]:
                i = i.lower()
                if (i == "cleanall"):
                    self.clean()
                    self.cleanLibs()
                elif (i == "clean"):
                    self.clean()
                elif (i in ["lib", "libs", "getlib", "getlibs"]):
                    self.getLibs()
                elif (i in ["rmlib", "rmlibs", "removelib", "removelibs"]):
                    self.rmLibs()
                elif (i in ["default", "def"]):
                    self.default()
                elif (i in ["ensurelib", "ensurelibs"]):
                    self.ensureLibs()
                elif (i in ["libclean", "cleanlib", "libsclean", "cleanlibs"]):
                    self.cleanLibs()
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
    b = Builder()
    b.craftObjs()
    b.argumentCheck()
