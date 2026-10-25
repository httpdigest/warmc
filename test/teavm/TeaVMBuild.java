/*
 * Java sources to a TeaVM module, for the Java corpora (test/javabench,
 * test/javatests), many programs in one JVM.
 *
 *   java -cp TEAVM_DIR/'*':DRIVER warmc.teavm.TeaVMBuild
 *        --teavm TEAVM_DIR [--strict] [--main CLASS] [--classpath PATH]
 *        [--support SRC.java]... JOB...
 *
 * TEAVM_DIR is where `make teavm` put TeaVM's jars: the compiler runs from all
 * of them, and the program's classes come from its class library and runtime
 * (CLASSLIB below).  TEAVM_DIR/patched, test/teavm/patches compiled
 * (test/teavm/patch.py), comes before the jars on both class paths: TeaVM's,
 * here, and the JVM's, which the caller gives it ahead of the jars and this
 * checks.
 *
 * A JOB is SRC_DIR=OUT_DIR: every .java file in SRC_DIR is compiled, with javac
 * in this JVM, into OUT_DIR/classes, and TeaVM's wasm-gc target makes
 * OUT_DIR/main.wasm of it.  The entry point is class Main, or --main's.
 * --classpath's directories and jars (separated as the platform separates
 * them) are classes the program is compiled against and TeaVM reads beside
 * its own: lwjgl/build.py's LWJGL.  --support files are
 * compiled once and given to every job, and their classes are kept whether or
 * not the program reaches them: test/teavm/FdLibm.java, whose functions
 * test/teavm/wasi.py calls in place of JavaScript's Math.
 *
 * TeaVM runs at its FULL optimisation level, its setting for speed, and without
 * --strict, as a project builds with it by default: a null dereference or an
 * index out of bounds is a wasm trap rather than a Java exception.  --strict
 * makes them exceptions, which the test corpus needs to show them caught.
 *
 * One JVM for the whole batch because each start is most of a small program's
 * build: javac and TeaVM are both JIT-warm after the first job.  A job that
 * fails is reported and the rest still run; the exit status is 1 if any failed.
 */
package warmc.teavm;

import java.io.File;
import java.io.IOException;
import java.io.StringWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.tools.ToolProvider;
import org.teavm.tooling.TeaVMProblemRenderer;
import org.teavm.tooling.TeaVMTargetType;
import org.teavm.tooling.TeaVMTool;
import org.teavm.tooling.TeaVMToolLog;
import org.teavm.vm.TeaVMOptimizationLevel;

public final class TeaVMBuild {
    // The language level the kernels are written to.
    private static final String RELEASE = "21";

    // The jars a program's classes are looked up in: TeaVM's Java class
    // library, the interop annotations, and the runtime the back ends call.
    private static final String[] CLASSLIB = {"classlib", "interop", "platform", "jso", "jso-apis",
        "jso-impl", "core", "metaprogramming-api", "metaprogramming-impl"};

    private TeaVMBuild() {
    }

    /** Collects what TeaVM says, to print only when a build fails. */
    private static final class Log implements TeaVMToolLog {
        final StringBuilder text = new StringBuilder();

        private void line(String level, String s, Throwable e) {
            text.append(level).append(": ").append(s).append('\n');
            if (e != null) {
                var w = new StringWriter();
                e.printStackTrace(new java.io.PrintWriter(w));
                text.append(w);
            }
        }

        @Override public void info(String s) { }
        @Override public void debug(String s) { }
        @Override public void warning(String s) { line("WARNING", s, null); }
        @Override public void error(String s) { line("ERROR", s, null); }
        @Override public void info(String s, Throwable e) { }
        @Override public void debug(String s, Throwable e) { }
        @Override public void warning(String s, Throwable e) { line("WARNING", s, e); }
        @Override public void error(String s, Throwable e) { line("ERROR", s, e); }
    }

    private static boolean javac(List<Path> sources, Path out, String classpath, StringBuilder err)
            throws IOException {
        Files.createDirectories(out);
        var args = new ArrayList<String>(List.of("--release", RELEASE, "-nowarn", "-encoding", "UTF-8",
                "-d", out.toString()));
        if (!classpath.isEmpty()) {
            args.add("-cp");
            args.add(classpath);
        }
        for (var s : sources) {
            args.add(s.toString());
        }
        var compiler = ToolProvider.getSystemJavaCompiler();
        var buf = new java.io.ByteArrayOutputStream();
        int rc = compiler.run(null, buf, buf, args.toArray(new String[0]));
        if (rc != 0) {
            err.append(buf.toString());
        }
        return rc == 0;
    }

    private static void wipe(Path dir) throws IOException {
        if (!Files.exists(dir)) {
            return;
        }
        try (Stream<Path> s = Files.walk(dir)) {
            for (var p : s.sorted(Comparator.reverseOrder()).collect(Collectors.toList())) {
                Files.delete(p);
            }
        }
    }

    /** Stops unless the JVM loads every patched class from the patched ones. */
    private static void checkPatched(Path patched) throws Exception {
        var loader = TeaVMBuild.class.getClassLoader();
        patched = patched.toAbsolutePath().normalize();
        try (Stream<Path> s = Files.walk(patched)) {
            for (var p : s.filter(p -> p.toString().endsWith(".class")).collect(Collectors.toList())) {
                var name = patched.relativize(p).toString().replace(File.separatorChar, '/');
                var url = loader.getResource(name);
                if (url == null || !url.getProtocol().equals("file") || !Path.of(url.toURI()).equals(p)) {
                    System.err.println(name + " comes from " + url + ", not " + patched
                            + ": put it ahead of TeaVM's jars on the class path");
                    System.exit(1);
                }
            }
        }
    }

    public static void main(String[] args) throws Exception {
        var teavm = new ArrayList<File>();
        var support = new ArrayList<Path>();
        var jobs = new ArrayList<String>();
        boolean strict = false;
        String mainClass = "Main";
        var extra = new ArrayList<File>();
        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "--teavm": {
                    var dir = new File(args[++i]);
                    var jars = dir.list();
                    for (var name : CLASSLIB) {
                        var prefix = "teavm-" + name + "-";
                        for (var j : jars != null ? jars : new String[0]) {
                            if (j.startsWith(prefix) && j.endsWith(".jar")
                                    && Character.isDigit(j.charAt(prefix.length()))) {
                                teavm.add(new File(dir, j));
                            }
                        }
                    }
                    var patched = new File(dir, "patched");
                    if (teavm.size() != CLASSLIB.length || !new File(patched, ".complete").exists()) {
                        System.err.println("no TeaVM class library in " + dir + " (make teavm)");
                        System.exit(1);
                    }
                    teavm.add(0, patched);
                    checkPatched(patched.toPath());
                    break;
                }
                case "--support":
                    support.add(Path.of(args[++i]));
                    break;
                case "--main":
                    mainClass = args[++i];
                    break;
                case "--classpath":
                    for (var entry : args[++i].split(File.pathSeparator)) {
                        if (!entry.isEmpty()) {
                            extra.add(new File(entry));
                        }
                    }
                    break;
                case "--strict":
                    strict = true;
                    break;
                default:
                    jobs.add(args[i]);
            }
        }
        var classlib = teavm.stream().map(File::toString).collect(Collectors.joining(File.pathSeparator));

        Path supportClasses = null;
        var supportNames = new ArrayList<String>();
        if (!support.isEmpty()) {
            supportClasses = Files.createTempDirectory("teavm-support");
            var err = new StringBuilder();
            if (!javac(support, supportClasses, classlib, err)) {
                System.err.print("javac of the support classes failed:\n" + err);
                System.exit(1);
            }
            try (Stream<Path> s = Files.walk(supportClasses)) {
                for (var p : s.filter(p -> p.toString().endsWith(".class")).collect(Collectors.toList())) {
                    var rel = supportClasses.relativize(p).toString();
                    if (!rel.contains("$")) {
                        supportNames.add(rel.substring(0, rel.length() - 6).replace(File.separatorChar, '.'));
                    }
                }
            }
        }

        int failed = 0;
        for (var job : jobs) {
            int eq = job.indexOf('=');
            var src = Path.of(job.substring(0, eq));
            var out = Path.of(job.substring(eq + 1));
            var classes = out.resolve("classes");
            wipe(classes);
            List<Path> sources;
            try (Stream<Path> s = Files.list(src)) {
                sources = s.filter(p -> p.toString().endsWith(".java")).sorted().collect(Collectors.toList());
            }
            var err = new StringBuilder();
            var programCp = extra.stream().map(File::toString).collect(Collectors.joining(File.pathSeparator));
            if (!javac(sources, classes, programCp, err)) {
                System.err.print("javac " + src + " failed:\n" + err);
                failed++;
                continue;
            }

            var tool = new TeaVMTool();
            var log = new Log();
            tool.setLog(log);
            tool.setTargetType(TeaVMTargetType.WEBASSEMBLY_GC);
            tool.setOptimizationLevel(TeaVMOptimizationLevel.FULL);
            tool.setStrict(strict);
            tool.setObfuscated(false);
            tool.setMainClass(mainClass);
            tool.setTargetDirectory(out.toFile());
            tool.setTargetFileName("main.wasm");
            var cp = new ArrayList<File>();
            cp.add(classes.toFile());
            cp.addAll(extra);
            if (supportClasses != null) {
                cp.add(supportClasses.toFile());
                tool.getClassesToPreserve().addAll(supportNames);
            }
            cp.addAll(teavm);
            tool.setClassPath(cp);
            try {
                tool.generate();
            } catch (Exception e) {
                log.error("TeaVM threw", e);
            }
            var problems = tool.getProblemProvider();
            if (problems == null || !problems.getSevereProblems().isEmpty() || log.text.indexOf("ERROR") >= 0) {
                if (problems != null && tool.getDependencyInfo() != null) {
                    TeaVMProblemRenderer.describeProblems(tool.getDependencyInfo().getCallGraph(), problems, log);
                }
                System.err.print("teavm " + src + " failed:\n" + log.text);
                failed++;
            }
        }
        if (supportClasses != null) {
            wipe(supportClasses);
        }
        System.exit(failed == 0 ? 0 : 1);
    }
}
