#!/usr/bin/env python3
"""Write the Xcode and Visual Studio projects for shc, the Shalimar compiler.

The twin of Compiler-Ci's and Compiler-Cppi's ide/generate.py, laid out the same
way, with the two things Shalimar has and they do not:

    ./generate.py            regenerate both projects
    ./generate.py --check    say whether they are up to date, and change nothing

**Its runtime.** A shalimar.exe with no lib/ beside it compiles, writes correct
assembly, and dies at the link, so both projects build the runtime archives
from runtime/ after the compiler - RUNTIME_SOURCES and DEBUG_RUNTIME_SOURCES,
as the Makefile names them - and the C6000 runtime, lib/shmrt-tms6747/*.s,
which is cpp11's output. **So each depends on cpp11**: the Xcode project on
../../Compiler-Cppi/ide/cxx1.xcodeproj's target, and ide/shc.sln holds
../../Compiler-Cppi/ide/cxx1.vcxproj and builds it first. Built from here, both
land in this solution's output; built from RIDE's, beside RIDE's others.
"""
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))            # the checkout
NAME = "shc"                 # the project files keep their name...
PRODUCT = "shalimar"         # ...and build the program the Makefile's PROGRAM names
EXE = PRODUCT + ".exe"
UP = ".."                    # from ide/ to the tree, in both project dialects
SHARED_BUILD = "$(TMPDIR)/ride-xcode"   # the build folder RIDE's projects share
RUNTIME_TARGET = "arm64-darwin"         # the Makefile's TARGET on a Darwin host
RUNTIME_FLAGS = "-std=c++14 -Wall -Wextra -Werror -pedantic -O2"   # its CXXFLAGS

# cpp11, which writes the C6000 runtime: its projects, from here, and its product.
DEP_EXE = "cpp11.exe"
DEP_XCODE = "../../Compiler-Cppi/ide/cxx1.xcodeproj"
DEP_VS = "..\\..\\Compiler-Cppi\\ide\\cxx1.vcxproj"


def rid(exe, *parts):
    """RIDE's rule, sha1("<exe>:<part>"): its workspace points at this project's
    target and product by these, and this one at cpp11's the same way."""
    return hashlib.sha1((exe + ":" + ":".join(parts)).encode()).hexdigest()[:24].upper()


def uid(text):
    """A stable 24-hex-digit id, from a path, so a regenerated project is diffable."""
    return hashlib.sha1(text.encode()).hexdigest()[:24].upper()


def makefile_list(variable):
    """The files a Makefile variable names, `VAR := \\` and one per line."""
    text = open(os.path.join(ROOT, "Makefile")).read()
    m = re.search(r"^%s\s*:=\s*\\\n((?:\s+\S+\s*\\?\n)+)" % variable, text, re.M)
    if not m:
        sys.exit("generate.py: no %s in the Makefile" % variable)
    return [w for w in m.group(1).split() if w != "\\"]


def sources():
    return makefile_list("SOURCES")


def runtime_sources():
    release = makefile_list("RUNTIME_SOURCES")
    text = open(os.path.join(ROOT, "Makefile")).read()
    extra = re.search(r"^DEBUG_RUNTIME_SOURCES\s*:=\s*\$\(RUNTIME_SOURCES\)\s*(.*)$", text, re.M)
    debug = release + (extra.group(1).split() if extra else [])
    bare = lambda names: [n[len("runtime/"):-len(".cpp")] for n in names]
    return bare(release), bare(debug)


def headers():
    out = []
    for d in ("src", "src/backend", "runtime"):
        base = os.path.join(ROOT, d)
        for f in sorted(os.listdir(base)):
            if f.endswith(".h") and " " not in f:
                out.append(d + "/" + f)
    return out


def dep_vs_guid():
    """cpp11's ProjectGuid, read out of its project so the two cannot disagree."""
    path = os.path.join(HERE, *DEP_VS.split("\\"))
    m = re.search(r"<ProjectGuid>(\{[0-9A-Fa-f-]+\})</ProjectGuid>", open(path).read())
    if not m:
        sys.exit("generate.py: no ProjectGuid in %s" % path)
    return m.group(1).upper()


def vs_guid(product):
    """RIDE's GUID rule, with its fixed seed: RIDE.sln names shalimar by it."""
    d = hashlib.sha1(("rstudio-vcxproj:" + product).encode()).hexdigest().upper()
    return "{%s-%s-%s-%s-%s}" % (d[0:8], d[8:12], d[12:16], d[16:20], d[20:32])


def pbx_quoted(text):
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


# ---------------------------------------------------------------- Xcode

def runtime_shell(release, debug):
    """The phase that puts the runtime archives and the C6000 runtime in lib/ beside shalimar.exe."""
    def archive(names, objects, extra, leaf):
        return ('mkdir -p "%s"\n' % objects +
                "".join('"$cxx" $flags %s-c "$SRCROOT/%s/runtime/%s.cpp" -o "%s/%s.o"\n'
                        % (extra, UP, n, objects, n) for n in names) +
                'rm -f "$lib/%s"\n' % leaf +                   # ar rcs keeps a deleted member
                '"$ar" rcs "$lib/%s" "%s"/*.o\n' % (leaf, objects))
    c6000 = ('cpp11="$BUILT_PRODUCTS_DIR/%s"\n' % DEP_EXE +
             'test -x "$cpp11" || { echo "shc.xcodeproj: no cpp11.exe beside the output - '
             'the C6000 runtime is its output" >&2; exit 1; }\n'
             'rm -rf "$lib/shmrt-tms6747"\nmkdir -p "$lib/shmrt-tms6747"\n' +
             "".join('"$cpp11" -S -arch tms6747 -nologo "$SRCROOT/%s/runtime/%s.cpp" '
                     '-o "$lib/shmrt-tms6747/%s.s"\n' % (UP, n, n) for n in release))
    return ('set -e\ncxx="$(xcrun --find clang++)"\nar="$(xcrun --find ar)"\n'
            'flags="%s"\nlib="$BUILT_PRODUCTS_DIR/lib"\nmkdir -p "$lib"\n' % RUNTIME_FLAGS +
            archive(release, "$DERIVED_FILE_DIR/runtime", "", "shmrt-%s.a" % RUNTIME_TARGET) +
            archive(debug, "$DERIVED_FILE_DIR/runtime-debug", "-DSHM_DEBUG=1 ",
                    "shmrt-%s-debug.a" % RUNTIME_TARGET) + c6000)


def write_xcode(srcs, hdrs, check):
    release, debug = runtime_sources()
    ids = {k: uid(k) for k in ("project", "productgroup", "mainGroup", "sources", "script",
                               "cfgProject", "cfgTarget", "dbgP", "relP", "dbgT", "relT",
                               "depfile", "depgroup", "refproxy", "prodproxy", "depproxy", "dependency")}
    ids["target"], ids["product"] = rid(EXE, "target"), rid(EXE, "product")

    files, builds, groups = [], [], {}
    for p in srcs + hdrs:
        fid, bid = uid("f:" + p), uid("b:" + p)
        kind = "sourcecode.cpp.cpp" if p.endswith(".cpp") else "sourcecode.c.h"
        files.append('\t\t%s /* %s */ = {isa = PBXFileReference; lastKnownFileType = %s; '
                     'name = %s; path = "%s/%s"; sourceTree = "<group>"; };'
                     % (fid, os.path.basename(p), kind, os.path.basename(p), UP, p))
        if p in srcs:
            builds.append('\t\t%s /* %s in Sources */ = {isa = PBXBuildFile; fileRef = %s /* %s */; };'
                          % (bid, os.path.basename(p), fid, os.path.basename(p)))
        groups.setdefault(os.path.dirname(p), []).append((fid, os.path.basename(p)))
    group_secs, group_children = [], []
    for d in sorted(groups):
        gid = uid("g:" + d)
        kids = "\n".join('\t\t\t\t%s /* %s */,' % (f, n) for f, n in sorted(groups[d], key=lambda x: x[1]))
        group_secs.append('\t\t%s /* %s */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n%s\n\t\t\t);\n'
                          '\t\t\tname = "%s";\n\t\t\tsourceTree = "<group>";\n\t\t};' % (gid, d, kids, d))
        group_children.append('\t\t\t\t%s /* %s */,' % (gid, d))
    src_phase = "\n".join('\t\t\t\t%s /* %s in Sources */,' % (uid("b:" + p), os.path.basename(p)) for p in srcs)

    inputs = (["$(SRCROOT)/%s/runtime/%s.cpp" % (UP, n) for n in debug] +
              ["$(SRCROOT)/%s/runtime/%s.h" % (UP, n) for n in ("shmrt", "Internal", "Shortest", "Debug")] +
              ["$(BUILT_PRODUCTS_DIR)/%s" % DEP_EXE])
    outputs = (["$(BUILT_PRODUCTS_DIR)/lib/shmrt-%s.a" % RUNTIME_TARGET,
                "$(BUILT_PRODUCTS_DIR)/lib/shmrt-%s-debug.a" % RUNTIME_TARGET] +
               ["$(BUILT_PRODUCTS_DIR)/lib/shmrt-tms6747/%s.s" % n for n in release])
    listed = lambda xs: "".join('\t\t\t\t"%s",\n' % x for x in xs)

    common = ('\t\t\t\tALWAYS_SEARCH_USER_PATHS = NO;\n'
              '\t\t\t\tGCC_WARN_64_TO_32_BIT_CONVERSION = NO;\n'   # the Makefile's warnings and no more
              '\t\t\t\tONLY_ACTIVE_ARCH = YES;\n'
              '\t\t\t\tCLANG_CXX_LANGUAGE_STANDARD = "c++14";\n'
              '\t\t\t\tCLANG_CXX_LIBRARY = "libc++";\n'
              '\t\t\t\tCODE_SIGN_STYLE = Automatic;\n'
              '\t\t\t\tGCC_TREAT_WARNINGS_AS_ERRORS = YES;\n'
              '\t\t\t\tWARNING_CFLAGS = (\n\t\t\t\t\t"-Wall",\n\t\t\t\t\t"-Wextra",\n\t\t\t\t\t"-pedantic",\n\t\t\t\t);\n'
              '\t\t\t\tPRODUCT_NAME = "%s";\n'
              '\t\t\t\tHEADER_SEARCH_PATHS = "$(SRCROOT)/%s/src $(SRCROOT)/%s/runtime";\n'
              '\t\t\t\tSYMROOT = "%s";\n\t\t\t\tOBJROOT = "%s";\n'
              '\t\t\t\tMACOSX_DEPLOYMENT_TARGET = 12.0;\n' % (EXE, UP, UP, SHARED_BUILD, SHARED_BUILD))

    text = f"""// !$*UTF8*$!
{{
	archiveVersion = 1;
	classes = {{}};
	objectVersion = 54;
	objects = {{

/* Begin PBXBuildFile section */
{chr(10).join(builds)}
/* End PBXBuildFile section */

/* Begin PBXContainerItemProxy section */
		{ids['prodproxy']} /* PBXContainerItemProxy */ = {{
			isa = PBXContainerItemProxy;
			containerPortal = {ids['depfile']} /* cxx1.xcodeproj */;
			proxyType = 2;
			remoteGlobalIDString = {rid(DEP_EXE, 'product')};
			remoteInfo = {DEP_EXE};
		}};
		{ids['depproxy']} /* PBXContainerItemProxy */ = {{
			isa = PBXContainerItemProxy;
			containerPortal = {ids['depfile']} /* cxx1.xcodeproj */;
			proxyType = 1;
			remoteGlobalIDString = {rid(DEP_EXE, 'target')};
			remoteInfo = {DEP_EXE};
		}};
/* End PBXContainerItemProxy section */

/* Begin PBXFileReference section */
{chr(10).join(files)}
		{ids['product']} /* {EXE} */ = {{isa = PBXFileReference; explicitFileType = "compiled.mach-o.executable"; includeInIndex = 0; path = {EXE}; sourceTree = BUILT_PRODUCTS_DIR; }};
		{ids['depfile']} /* cxx1.xcodeproj */ = {{isa = PBXFileReference; lastKnownFileType = "wrapper.pb-project"; name = cxx1.xcodeproj; path = "{DEP_XCODE}"; sourceTree = "<group>"; }};
/* End PBXFileReference section */

/* Begin PBXGroup section */
		{ids['mainGroup']} = {{
			isa = PBXGroup;
			children = (
{chr(10).join(group_children)}
				{ids['depfile']} /* cxx1.xcodeproj */,
				{ids['productgroup']} /* Products */,
			);
			sourceTree = "<group>";
		}};
		{ids['productgroup']} /* Products */ = {{
			isa = PBXGroup;
			children = (
				{ids['product']} /* {EXE} */,
			);
			name = Products;
			sourceTree = "<group>";
		}};
		{ids['depgroup']} /* Products */ = {{
			isa = PBXGroup;
			children = (
				{ids['refproxy']} /* {DEP_EXE} */,
			);
			name = Products;
			sourceTree = "<group>";
		}};
{chr(10).join(group_secs)}
/* End PBXGroup section */

/* Begin PBXNativeTarget section */
		{ids['target']} /* {NAME} */ = {{
			isa = PBXNativeTarget;
			buildConfigurationList = {ids['cfgTarget']};
			buildPhases = (
				{ids['sources']} /* Sources */,
				{ids['script']} /* the Shalimar runtime, beside shalimar.exe */,
			);
			buildRules = ();
			dependencies = (
				{ids['dependency']} /* PBXTargetDependency */,
			);
			name = {NAME};
			productName = {EXE};
			productReference = {ids['product']} /* {EXE} */;
			productType = "com.apple.product-type.tool";
		}};
/* End PBXNativeTarget section */

/* Begin PBXProject section */
		{ids['project']} /* Project object */ = {{
			isa = PBXProject;
			attributes = {{
				BuildIndependentTargetsInParallel = 1;
				LastUpgradeCheck = 2600;
			}};
			buildConfigurationList = {ids['cfgProject']};
			compatibilityVersion = "Xcode 14.0";
			developmentRegion = en;
			hasScannedForEncodings = 0;
			knownRegions = (en, Base);
			mainGroup = {ids['mainGroup']};
			productRefGroup = {ids['productgroup']} /* Products */;
			projectDirPath = "";
			projectReferences = (
				{{
					ProductGroup = {ids['depgroup']} /* Products */;
					ProjectRef = {ids['depfile']} /* cxx1.xcodeproj */;
				}},
			);
			projectRoot = "";
			targets = (
				{ids['target']} /* {NAME} */,
			);
		}};
/* End PBXProject section */

/* Begin PBXReferenceProxy section */
		{ids['refproxy']} /* {DEP_EXE} */ = {{
			isa = PBXReferenceProxy;
			fileType = "compiled.mach-o.executable";
			path = {DEP_EXE};
			remoteRef = {ids['prodproxy']} /* PBXContainerItemProxy */;
			sourceTree = BUILT_PRODUCTS_DIR;
		}};
/* End PBXReferenceProxy section */

/* Begin PBXShellScriptBuildPhase section */
		{ids['script']} /* the Shalimar runtime, beside shalimar.exe */ = {{
			isa = PBXShellScriptBuildPhase;
			buildActionMask = 2147483647;
			files = ();
			inputPaths = (
{listed(inputs)}			);
			name = "the Shalimar runtime, beside shalimar.exe";
			outputPaths = (
{listed(outputs)}			);
			runOnlyForDeploymentPostprocessing = 0;
			shellPath = /bin/sh;
			shellScript = {pbx_quoted(runtime_shell(release, debug))};
		}};
/* End PBXShellScriptBuildPhase section */

/* Begin PBXSourcesBuildPhase section */
		{ids['sources']} /* Sources */ = {{
			isa = PBXSourcesBuildPhase;
			buildActionMask = 2147483647;
			files = (
{src_phase}
			);
			runOnlyForDeploymentPostprocessing = 0;
		}};
/* End PBXSourcesBuildPhase section */

/* Begin PBXTargetDependency section */
		{ids['dependency']} /* PBXTargetDependency */ = {{
			isa = PBXTargetDependency;
			name = {DEP_EXE};
			targetProxy = {ids['depproxy']} /* PBXContainerItemProxy */;
		}};
/* End PBXTargetDependency section */

/* Begin XCBuildConfiguration section */
		{ids['dbgP']} /* Debug */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
{common}				GCC_OPTIMIZATION_LEVEL = 0;
			}};
			name = Debug;
		}};
		{ids['relP']} /* Release */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
{common}				GCC_OPTIMIZATION_LEVEL = 2;
			}};
			name = Release;
		}};
		{ids['dbgT']} /* Debug */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
				PRODUCT_NAME = "{EXE}";
			}};
			name = Debug;
		}};
		{ids['relT']} /* Release */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
				PRODUCT_NAME = "{EXE}";
			}};
			name = Release;
		}};
/* End XCBuildConfiguration section */

/* Begin XCConfigurationList section */
		{ids['cfgProject']} = {{
			isa = XCConfigurationList;
			buildConfigurations = (
				{ids['dbgP']} /* Debug */,
				{ids['relP']} /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		}};
		{ids['cfgTarget']} = {{
			isa = XCConfigurationList;
			buildConfigurations = (
				{ids['dbgT']} /* Debug */,
				{ids['relT']} /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		}};
/* End XCConfigurationList section */
	}};
	rootObject = {ids['project']} /* Project object */;
}}
"""
    proj = os.path.join(HERE, NAME + ".xcodeproj")
    pb = os.path.join(proj, "project.pbxproj")
    ws = os.path.join(HERE, NAME + ".xcworkspace")
    # cpp11's project is in the workspace too, so Xcode shows and builds both.
    wsdata = ('<?xml version="1.0" encoding="UTF-8"?>\n<Workspace version = "1.0">\n'
              '   <FileRef location = "group:%s.xcodeproj"></FileRef>\n'
              '   <FileRef location = "group:%s"></FileRef>\n</Workspace>\n' % (NAME, DEP_XCODE))
    ref = ('<BuildableReference BuildableIdentifier = "primary" BlueprintIdentifier = "%s" '
           'BuildableName = "%s" BlueprintName = "%s" ReferencedContainer = "container:%s.xcodeproj">'
           '</BuildableReference>' % (ids['target'], EXE, NAME, NAME))
    scheme = f"""<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion = "2600" version = "1.7">
   <BuildAction parallelizeBuildables = "YES" buildImplicitDependencies = "YES">
      <BuildActionEntries>
         <BuildActionEntry buildForTesting = "YES" buildForRunning = "YES" buildForProfiling = "YES" buildForArchiving = "YES" buildForAnalyzing = "YES">
            {ref}
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <LaunchAction buildConfiguration = "Release" selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB" launchStyle = "0" useCustomWorkingDirectory = "NO" ignoresPersistentStateOnLaunch = "NO" debugDocumentVersioning = "YES" debugServiceExtension = "internal" allowLocationSimulation = "YES">
      <BuildableProductRunnable runnableDebuggingMode = "0">
         {ref}
      </BuildableProductRunnable>
   </LaunchAction>
   <AnalyzeAction buildConfiguration = "Release"></AnalyzeAction>
   <ArchiveAction buildConfiguration = "Release" revealArchiveInOrganizer = "YES"></ArchiveAction>
</Scheme>
"""
    schemedir = os.path.join(proj, "xcshareddata", "xcschemes")
    spath = os.path.join(schemedir, NAME + ".xcscheme")
    wpath = os.path.join(ws, "contents.xcworkspacedata")
    if check:
        return all(os.path.exists(p) and open(p).read() == t
                   for p, t in ((pb, text), (spath, scheme), (wpath, wsdata)))
    os.makedirs(schemedir, exist_ok=True)
    os.makedirs(ws, exist_ok=True)
    open(pb, "w").write(text)
    open(spath, "w").write(scheme)
    open(wpath, "w").write(wsdata)
    return True


# ------------------------------------------------------- Visual Studio 2022

def validXml(text, what):
    """A project file is XML, and MSBuild will not read a control byte."""
    import xml.dom.minidom
    xml.dom.minidom.parseString(text.encode("utf-8"))
    for ch in text:
        if ord(ch) < 9 or ord(ch) in (11, 12) or 14 <= ord(ch) <= 31:
            raise ValueError("%s holds a control byte 0x%02X" % (what, ord(ch)))
    return text


def write_vs(srcs, hdrs, check):
    """shc.vcxproj and shc.sln: build.bat's flags, and the runtime as a post-build step."""
    release, debug = runtime_sources()
    guid, dep = vs_guid(PRODUCT), dep_vs_guid()

    def win(p):
        return "..\\" + p.replace("/", "\\")

    cl = "\n".join('    <ClCompile Include="%s" />' % win(p) for p in srcs)
    hd = "\n".join('    <ClInclude Include="%s" />' % win(p) for p in hdrs)
    flags = "/nologo /std:c++14 /W4 /WX /EHsc /permissive- /O2 /D_CRT_SECURE_NO_WARNINGS"
    rsrc = lambda ns: " ".join('"$(ProjectDir)..\\runtime\\%s.cpp"' % n for n in ns)
    robj = lambda ns, d: " ".join('"$(IntDir)%s\\%s.obj"' % (d, n) for n in ns)
    c6000 = "".join('"$(OutDir)%s" -S -arch tms6747 -nologo "$(ProjectDir)..\\runtime\\%s.cpp" '
                    '-o "$(OutDir)lib\\shmrt-tms6747\\%s.s"\nif errorlevel 1 exit /b 1\n'
                    % (DEP_EXE, n, n) for n in release)
    step = ('if not exist "$(OutDir)lib" mkdir "$(OutDir)lib"\n'
            'if not exist "$(IntDir)rt" mkdir "$(IntDir)rt"\n'
            'if not exist "$(IntDir)rtd" mkdir "$(IntDir)rtd"\n'
            'cl %s /Fo"$(IntDir)rt\\\\" /c %s\nif errorlevel 1 exit /b 1\n'
            'lib /nologo /out:"$(OutDir)lib\\shmrt-x86_64-windows.lib" %s\nif errorlevel 1 exit /b 1\n'
            'cl %s /DSHM_DEBUG=1 /Fo"$(IntDir)rtd\\\\" /c %s\nif errorlevel 1 exit /b 1\n'
            'lib /nologo /out:"$(OutDir)lib\\shmrt-x86_64-windows-debug.lib" %s\nif errorlevel 1 exit /b 1\n'
            'if not exist "$(OutDir)%s" echo shc.vcxproj: no cpp11.exe in $(OutDir) - the C6000 runtime is its output\n'
            'if not exist "$(OutDir)%s" exit /b 1\n'
            'if not exist "$(OutDir)lib\\shmrt-tms6747" mkdir "$(OutDir)lib\\shmrt-tms6747"\n%s'
            % (flags, rsrc(release), robj(release, "rt"), flags, rsrc(debug), robj(debug, "rtd"),
               DEP_EXE, DEP_EXE, c6000)).rstrip("\n")
    step = step.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

    proj = f"""<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>{guid}</ProjectGuid>
    <RootNamespace>{NAME}</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Release'" Label="Configuration">
    <UseDebugLibraries>false</UseDebugLibraries>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Debug'" Label="Configuration">
    <UseDebugLibraries>true</UseDebugLibraries>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <!-- **The output is the solution's**, its own ide\\{NAME}.sln or RIDE's: the post-build step
       wants cpp11.exe beside {EXE}, and both solutions build cpp11 into the same place. Built
       alone, with no solution, it goes to build\\<config>\\. The objects stay here. -->
  <PropertyGroup>
    <OutDir Condition="'$(SolutionDir)'=='' Or '$(SolutionDir)'=='*Undefined*'">$(ProjectDir)build\\$(Configuration)\\</OutDir>
    <OutDir Condition="'$(SolutionDir)'!='' And '$(SolutionDir)'!='*Undefined*'">$(SolutionDir)$(Platform)\\$(Configuration)\\</OutDir>
    <IntDir>$(ProjectDir)build\\$(Configuration)\\obj\\</IntDir>
    <TargetName>{PRODUCT}</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <LanguageStandard>stdcpp14</LanguageStandard>
      <ConformanceMode>true</ConformanceMode>
      <ExceptionHandling>Sync</ExceptionHandling>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
      <AdditionalIncludeDirectories>$(ProjectDir)..\\src;$(ProjectDir)..\\runtime;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>_CRT_SECURE_NO_WARNINGS;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
    </ClCompile>
    <Link><SubSystem>Console</SubSystem></Link>
    <PostBuildEvent>
      <Message>building the Shalimar runtime beside {EXE}</Message>
      <Command>{step}</Command>
    </PostBuildEvent>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <RuntimeLibrary>MultiThreaded</RuntimeLibrary>
      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
    </ClCompile>
    <Link><GenerateDebugInformation>true</GenerateDebugInformation></Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
    <ClCompile>
      <Optimization>Disabled</Optimization>
      <RuntimeLibrary>MultiThreadedDebug</RuntimeLibrary>
      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
    </ClCompile>
    <Link><GenerateDebugInformation>true</GenerateDebugInformation></Link>
  </ItemDefinitionGroup>
  <ItemGroup>
{cl}
  </ItemGroup>
  <ItemGroup>
{hd}
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
"""
    sln = f"""Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.0.31903.59
MinimumVisualStudioVersion = 10.0.40219.1
Project("{{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}}") = "cpp11", "{DEP_VS}", "{dep}"
EndProject
Project("{{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}}") = "{NAME}", "{NAME}.vcxproj", "{guid}"
\tProjectSection(ProjectDependencies) = postProject
\t\t{dep} = {dep}
\tEndProjectSection
EndProject
Global
\tGlobalSection(SolutionConfigurationPlatforms) = preSolution
\t\tDebug|x64 = Debug|x64
\t\tRelease|x64 = Release|x64
\tEndGlobalSection
\tGlobalSection(ProjectConfigurationPlatforms) = postSolution
\t\t{dep}.Debug|x64.ActiveCfg = Debug|x64
\t\t{dep}.Debug|x64.Build.0 = Debug|x64
\t\t{dep}.Release|x64.ActiveCfg = Release|x64
\t\t{dep}.Release|x64.Build.0 = Release|x64
\t\t{guid}.Debug|x64.ActiveCfg = Debug|x64
\t\t{guid}.Debug|x64.Build.0 = Debug|x64
\t\t{guid}.Release|x64.ActiveCfg = Release|x64
\t\t{guid}.Release|x64.Build.0 = Release|x64
\tEndGlobalSection
EndGlobal
"""
    def folder(p, top):
        parts = p.split("/")
        return "\\".join([top] + parts[:-1])

    dirs = sorted({folder(p, "Source Files") for p in srcs} | {folder(p, "Header Files") for p in hdrs})
    unique = []
    for d in dirs:
        parts = d.split("\\")
        for i in range(1, len(parts) + 1):
            j = "\\".join(parts[:i])
            if j not in unique:
                unique.append(j)
    fid = lambda d: "{%s-%s-%s-%s-%s}" % (uid("filt:" + d)[:8], uid("f1:" + d)[:4], uid("f2:" + d)[:4],
                                          uid("f3:" + d)[:4], uid("f4:" + d)[:12])
    filters = ('<?xml version="1.0" encoding="utf-8"?>\n'
               '<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
               '  <ItemGroup>\n' +
               "".join('    <Filter Include="%s"><UniqueIdentifier>%s</UniqueIdentifier></Filter>\n' % (d, fid(d)) for d in unique) +
               '  </ItemGroup>\n  <ItemGroup>\n' +
               "".join('    <ClCompile Include="%s"><Filter>%s</Filter></ClCompile>\n' % (win(p), folder(p, "Source Files")) for p in srcs) +
               '  </ItemGroup>\n  <ItemGroup>\n' +
               "".join('    <ClInclude Include="%s"><Filter>%s</Filter></ClInclude>\n' % (win(p), folder(p, "Header Files")) for p in hdrs) +
               '  </ItemGroup>\n</Project>\n')

    pp = os.path.join(HERE, NAME + ".vcxproj")
    sp = os.path.join(HERE, NAME + ".sln")
    fp = pp + ".filters"
    if check:
        for path, want in ((pp, proj), (sp, sln), (fp, filters)):
            if not os.path.exists(path) or open(path, newline="").read().replace("\r\n", "\n") != want:
                return False
        return True
    validXml(proj, NAME + ".vcxproj")
    validXml(filters, NAME + ".vcxproj.filters")
    for path, want in ((pp, proj), (sp, sln), (fp, filters)):
        open(path, "w", newline="\r\n").write(want)
    return True


if __name__ == "__main__":
    check = "--check" in sys.argv
    s, h = sources(), headers()
    x, v = write_xcode(s, h, check), write_vs(s, h, check)
    if check:
        print("up to date" if (x and v) else "STALE - run ./generate.py")
        sys.exit(0 if (x and v) else 1)
    print("wrote %s.xcodeproj, %s.xcworkspace, %s.vcxproj and %s.sln for %d sources"
          % (NAME, NAME, NAME, NAME, len(s)))
