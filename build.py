#!/bin/python3
import sys
import os
import platform
import subprocess

sources = [
    "main.c",
    "bstrlib/bstrlib.c",
]
name = "prog"
objs: list[str] = []
firstArg = 1

bstrzip = "https://github.com/websnarf/bstrlib/archive/refs/tags/v1.0.0.zip"
bstrzipName = "bstrlib.zip"
bstrlibFolder = bstrzipName.replace(".zip", "")

normalAnsi = "\x1B[0m"
greenAnsi = "\x1B[32m"
redAnsi= "\x1B[31m"

EXIT_FAILURE: int = 1

if (platform.system() == "Linux"):
    cc = "cc"
    cflags = "-Wall -Wextra -pedantic -std=c99"
    cflagsDebug = "-g"
    cflagsRelease = "-O3 -march=native"
    ldflags = ""
    outTac = "-o"
    objectExt = ".o"
    compileTac = "-c"
    platformTac = ""
    rm = "rm -f"
    rmFolder = "rm -f -r"
    curl = "curl"
    unzip = "unzip"
    mv = "mv"
elif (platform.system() == "Windows"):
    name += ".exe"
    cc = "cl"
    cflags = "/W4"
    cflagsDebug = "/Zi"
    cflagsRelease = "/O2"
    ldflags = "ws2_32.lib"
    outTac = "/Fe:"
    objectExt = ".obj"
    compileTac = "/c"
    platformTac = "/DPLATFORM_WINDOWS"
    rm = "del"
    rmFolder = rm
    curl = "curl.exe"
    unzip = "tar -xf"
    mv = "move"
else:
    print("system type not detected")
    sys.exit(EXIT_FAILURE)

objRecompiled: bool = False

def runCommand(command: str):
    print(f"{greenAnsi}{command}{normalAnsi}")
    if (subprocess.run(command, shell=True).returncode != 0):
        print(f"{redAnsi}failed command:\n{command}{normalAnsi}")
        sys.exit(EXIT_FAILURE)

def compileSources():
    global objRecompiled
    for i, item in enumerate(sources):
        cfileTime = os.path.getmtime(item)
        try:
            ofileTime = os.path.getmtime(objs[i])
        except FileNotFoundError:
            ofileTime = 0
        if (cfileTime < ofileTime):
            continue
        objRecompiled = True
        command = " ".join([cc, compileTac, item, outTac, objs[i], cflags, platformTac])
        runCommand(command);

def link():
    global name, outTac
    compileSources()
    if (objRecompiled):
        command = " ".join([cc, " ".join(objs), outTac, name, ldflags])
        runCommand(command);
    else:
        print("no linking necesary")

def clean():
    command = " ".join([rm, " ".join(objs), name])
    runCommand(command);

def craftObjs():
    for i in sources:
        objs.append(i.replace(".c", objectExt))

def debug():
    global cflags
    cflags += f" {cflagsDebug}"
    link()

def release():
    global cflags
    cflags += f" {cflagsRelease}"
    link()

def default():
    debug()

def getbstr():
    command = " ".join([curl, "-L", bstrzip, ">", bstrzipName])
    runCommand(command)
    command = " ".join([unzip, bstrzipName])
    runCommand(command)
    command = " ".join([rm, bstrzipName]);
    runCommand(command)
    command = " ".join([mv, "bstrlib-1.0.0", bstrlibFolder]);
    runCommand(command)

def rmLibs():
    command = " ".join([rmFolder, bstrlibFolder]);
    runCommand(command)

def getLibs():
    getbstr()

def argumentCheck():
    if (len(sys.argv) > 1):
        for i in sys.argv[1:]:
            i = i.lower()
            if (i == "clean"):
                clean()
            elif (i in ["lib", "libs", "getlib", "getlibs"]):
                getLibs()
            elif (i in ["rmlib", "rmlibs"]):
                rmLibs()
            elif (i in ["default", "def"]):
                default()
            elif (i == "debug"):
                debug()
            elif (i == "release"):
                release()
            else:
                print(f"unkown target: {redAnsi}{i}{normalAnsi}")
                sys.exit(1)
    else:
        default()

if __name__ == "__main__":
    craftObjs()
    argumentCheck()
