/*
 * LWJGL's classes, as its jars have them, made into what TeaVM compiles for
 * warmc: lwjgl/build.py hands TeaVM this transformer.
 *
 * A native method of LWJGL's becomes an import of module "lwjgl", which
 * warmc's --native-module makes a direct call of the C function of the
 * import's name, its arguments where C's convention has them: an OpenGL
 * function's (GL11C.glClear, GL15C.nglGenBuffers) the function itself, a C
 * library function's (LibCString.nmemcpy, DynamicLinkLoader.ndlopen) the
 * function, and any other's the C function lwjgl/native/lwjgl.c defines in
 * its place, lwjgl_CLASS_METHOD.  org.lwjgl.system.JNI, whose natives call
 * the function at an address, is lwjgl/build.py's to generate; its imports
 * are already there.  A native taking an array or an object is left as it
 * is, for TeaVM to report: C cannot be handed either.
 *
 * A call through JNI of a function whose address is one LWJGL looked up by
 * name is a direct call of that function too: the call gets a method of its
 * own, JNI's for it with the function's import in place of the call of the
 * address, and is no longer handed the address.  That is an OpenGL
 * function's that takes a Java array
 * (JNI.callPV(n, buffers, GL.getICD().glGenBuffers)), whose address is the
 * GLCapabilities field of the function's name, and a GLFW function's
 * (JNI.invokeV(GLFW.Functions.PollEvents)), whose address a Functions class's
 * initializer looks up by the name it gives APIUtil.apiGetFunctionAddress --
 * when the call is all that reads it.  The read then goes, and with it the
 * Functions class's initialization, which would look every function up by
 * name the first time one is called.  An optional function's address LWJGL
 * checks before the call, and the call goes through it.
 *
 * GLCapabilities looks every OpenGL function LWJGL knows up by name, over
 * 2000 of them, a check of each version and extension handing
 * Checks.checkFunctions the names of its functions.  Given the names macOS's
 * OpenGL framework exports (system property warmc.lwjgl.gl-exports, a file
 * of them, which lwjgl/build.py writes), a check none of whose functions the
 * framework has, which looks each up for nothing, is false, as its lookups
 * would make it: one of them, at least, is not one the check skips.  The
 * arrays of names and indices go with it, and so does what made the indices
 * of functions a check needs only with another extension: a flag of that
 * extension, ARB_foo(ext) ? 0 : Integer.MIN_VALUE, whose helper only asks the
 * set of extensions what it contains.  A check whose every lookup goes is
 * false whatever the set holds, but for the debug message it gives, with
 * Configuration.DEBUG, of an extension the set holds and the framework has
 * no function of; a call of it is false, and the check goes.
 *
 * And two things of LWJGL's TeaVM does not have: MethodHandles.lookup(),
 * which a callback's descriptor keeps only for LWJGL's FFM backend, and is
 * null here, where upcalls are libffi's (lwjgl/platform's Upcalls); and
 * Runtime.addShutdownHook, which the debug allocator reports leaks from, and
 * is warmc's Setup.addShutdownHook, whose hooks run when main returns.
 */
package warmc.lwjgl;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.teavm.model.AccessLevel;
import org.teavm.model.AnnotationHolder;
import org.teavm.model.AnnotationValue;
import org.teavm.model.BasicBlock;
import org.teavm.model.ClassHolder;
import org.teavm.model.ClassHolderTransformer;
import org.teavm.model.ClassHolderTransformerContext;
import org.teavm.model.ClassReader;
import org.teavm.model.ElementModifier;
import org.teavm.model.FieldReference;
import org.teavm.model.Incoming;
import org.teavm.model.Instruction;
import org.teavm.model.MethodDescriptor;
import org.teavm.model.MethodHolder;
import org.teavm.model.MethodReader;
import org.teavm.model.MethodReference;
import org.teavm.model.Phi;
import org.teavm.model.Program;
import org.teavm.model.ValueType;
import org.teavm.model.Variable;
import org.teavm.model.instructions.BinaryInstruction;
import org.teavm.model.instructions.BranchingCondition;
import org.teavm.model.instructions.BranchingInstruction;
import org.teavm.model.instructions.ConstructArrayInstruction;
import org.teavm.model.instructions.EmptyInstruction;
import org.teavm.model.instructions.ExitInstruction;
import org.teavm.model.instructions.GetFieldInstruction;
import org.teavm.model.instructions.IntegerConstantInstruction;
import org.teavm.model.instructions.InvocationType;
import org.teavm.model.instructions.InvokeInstruction;
import org.teavm.model.instructions.JumpInstruction;
import org.teavm.model.instructions.NullConstantInstruction;
import org.teavm.model.instructions.PutElementInstruction;
import org.teavm.model.instructions.PutFieldInstruction;
import org.teavm.model.instructions.StringConstantInstruction;
import org.teavm.model.instructions.UnwrapArrayInstruction;
import org.teavm.model.util.DefinitionExtractor;
import org.teavm.model.util.ProgramUtils;
import org.teavm.model.util.UsageExtractor;

public class LwjglTransformer implements ClassHolderTransformer {
    private static final String IMPORT = "org.teavm.interop.Import";
    private static final String JNI = "org.lwjgl.system.JNI";
    private static final String CAPABILITIES = "org.lwjgl.opengl.GLCapabilities";
    private static final String SETUP = "org.lwjgl.system.warmc.Setup";
    private static final String CHECKS = "org.lwjgl.system.Checks";
    // the OpenGL functions macOS's framework exports, or null to look every
    // one up
    private static final Set<String> GL_EXPORTS = glExports();

    // the functions a Functions class's fields hold the addresses of, by class
    private final Map<String, Map<String, String>> functions = new HashMap<>();

    @Override
    public void transformClass(ClassHolder cls, ClassHolderTransformerContext context) {
        String name = cls.getName();
        if (!name.startsWith("org.lwjgl.")) {
            return;
        }
        Direct direct = new Direct(cls, context);
        boolean capabilities = GL_EXPORTS != null && name.equals(CAPABILITIES);
        for (MethodHolder m : new ArrayList<>(cls.getMethods())) {
            if (m.hasModifier(ElementModifier.NATIVE)) {
                if (m.getAnnotations().get(IMPORT) == null) {
                    String symbol = symbol(name, m);
                    if (symbol != null) {
                        m.getAnnotations().add(importOf(symbol));
                    }
                }
                continue;
            }
            Program program = m.getProgram();
            if (program != null) {
                rewrite(program);
                if (capabilities) {
                    foldChecks(cls, program);
                }
                if (!name.equals(JNI)) {
                    direct.calls(program);
                }
            }
        }
        for (MethodHolder m : direct.added) {
            cls.addMethod(m);
        }
        if (capabilities) {
            foldFalseChecks(cls);
        }
    }

    private static Set<String> glExports() {
        String path = System.getProperty("warmc.lwjgl.gl-exports");
        if (path == null) {
            return null;
        }
        try {
            return new HashSet<>(Files.readAllLines(Paths.get(path)));
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    // Each variable's definition and how many uses it has.
    private static final class Flow {
        final Instruction[] definition;
        final int[] uses;

        Flow(Program program) {
            definition = new Instruction[program.variableCount()];
            uses = new int[program.variableCount()];
            DefinitionExtractor defs = new DefinitionExtractor();
            UsageExtractor used = new UsageExtractor();
            for (int i = 0; i < program.basicBlockCount(); i++) {
                BasicBlock block = program.basicBlockAt(i);
                for (Phi phi : block.getPhis()) {
                    for (Incoming in : phi.getIncomings()) {
                        uses[in.getValue().getIndex()]++;
                    }
                }
                for (Instruction insn : block) {
                    insn.acceptVisitor(defs);
                    for (Variable v : defs.getDefinedVariables()) {
                        definition[v.getIndex()] = insn;
                    }
                    insn.acceptVisitor(used);
                    for (Variable v : used.getUsedVariables()) {
                        uses[v.getIndex()]++;
                    }
                }
            }
        }

        Instruction def(Variable v) {
            return definition[v.getIndex()];
        }
    }

    // An array made for one call alone: where it is made, and its elements'
    // stores, each into the array's data, which TeaVM unwraps from it first.
    private static final class Made {
        final Instruction construct;
        final List<Instruction> unwraps = new ArrayList<>();
        final List<PutElementInstruction> puts = new ArrayList<>();

        Made(Instruction construct) {
            this.construct = construct;
        }

        void delete() {
            puts.forEach(Instruction::delete);
            unwraps.forEach(Instruction::delete);
            construct.delete();
        }
    }

    // The array v, if it is made in this program and used only by one call
    // and to store its elements, or null.
    private static Made madeFor(Flow flow, Variable v, Map<Integer, List<UnwrapArrayInstruction>> unwraps,
            Map<Integer, List<PutElementInstruction>> puts) {
        if (!(flow.def(v) instanceof ConstructArrayInstruction)) {
            return null;
        }
        Made made = new Made(flow.def(v));
        for (UnwrapArrayInstruction unwrap : unwraps.getOrDefault(v.getIndex(), List.of())) {
            List<PutElementInstruction> stores = puts.getOrDefault(unwrap.getReceiver().getIndex(), List.of());
            if (flow.uses[unwrap.getReceiver().getIndex()] != stores.size()) {
                return null;
            }
            made.unwraps.add(unwrap);
            made.puts.addAll(stores);
        }
        return flow.uses[v.getIndex()] == 1 + made.unwraps.size() ? made : null;
    }

    // A call of Checks.checkFunctions(provider, caps, indices, names) with
    // arrays made for it alone is false, and goes with them, when the
    // framework has none of the names and an index, at least, is a constant
    // not below 0, which the check does not skip.
    private static void foldChecks(ClassHolder cls, Program program) {
        Flow flow = new Flow(program);
        Map<Integer, List<UnwrapArrayInstruction>> unwraps = new HashMap<>();
        Map<Integer, List<PutElementInstruction>> puts = new HashMap<>();
        List<InvokeInstruction> checks = new ArrayList<>();
        for (int i = 0; i < program.basicBlockCount(); i++) {
            for (Instruction insn : program.basicBlockAt(i)) {
                if (insn instanceof UnwrapArrayInstruction) {
                    UnwrapArrayInstruction unwrap = (UnwrapArrayInstruction) insn;
                    unwraps.computeIfAbsent(unwrap.getArray().getIndex(), k -> new ArrayList<>()).add(unwrap);
                } else if (insn instanceof PutElementInstruction) {
                    PutElementInstruction put = (PutElementInstruction) insn;
                    puts.computeIfAbsent(put.getArray().getIndex(), k -> new ArrayList<>()).add(put);
                } else if (insn instanceof InvokeInstruction) {
                    InvokeInstruction invoke = (InvokeInstruction) insn;
                    MethodReference m = invoke.getMethod();
                    if (m.getClassName().equals(CHECKS) && m.getName().equals("checkFunctions")
                            && m.parameterCount() == 4
                            && m.parameterType(3).equals(ValueType.arrayOf(ValueType.object("java.lang.String")))) {
                        checks.add(invoke);
                    }
                }
            }
        }
        List<Variable> values = new ArrayList<>();
        for (InvokeInstruction check : checks) {
            Made indices = madeFor(flow, check.getArguments().get(2), unwraps, puts);
            Made names = madeFor(flow, check.getArguments().get(3), unwraps, puts);
            if (indices == null || names == null) {
                continue;
            }
            boolean found = false;
            for (PutElementInstruction put : names.puts) {
                Instruction value = flow.def(put.getValue());
                found |= !(value instanceof StringConstantInstruction)
                        || GL_EXPORTS.contains(((StringConstantInstruction) value).getConstant());
            }
            boolean counted = false;
            for (PutElementInstruction put : indices.puts) {
                Instruction value = flow.def(put.getValue());
                counted |= value instanceof IntegerConstantInstruction
                        && ((IntegerConstantInstruction) value).getConstant() >= 0;
            }
            if (found || !counted) {
                continue;
            }
            if (check.getReceiver() != null) {
                IntegerConstantInstruction no = new IntegerConstantInstruction();
                no.setConstant(0);
                no.setReceiver(check.getReceiver());
                no.setLocation(check.getLocation());
                check.replace(no);
            } else {
                check.delete();
            }
            for (PutElementInstruction put : indices.puts) {
                values.add(put.getValue());
            }
            names.delete();
            indices.delete();
        }
        if (!values.isEmpty()) {
            dropFlags(cls, program, values);
        }
    }

    // A call of a check of the class's that is false whatever it is asked
    // (alwaysFalse) is false.
    private static void foldFalseChecks(ClassHolder cls) {
        Set<MethodDescriptor> falses = new HashSet<>();
        for (MethodHolder m : cls.getMethods()) {
            if (m.hasModifier(ElementModifier.STATIC) && m.getResultType() == ValueType.BOOLEAN
                    && m.getProgram() != null && alwaysFalse(cls, m.getProgram())) {
                falses.add(m.getDescriptor());
            }
        }
        for (MethodHolder m : cls.getMethods()) {
            Program program = m.getProgram();
            if (program == null) {
                continue;
            }
            for (int i = 0; i < program.basicBlockCount(); i++) {
                for (Instruction insn : program.basicBlockAt(i)) {
                    if (!(insn instanceof InvokeInstruction)) {
                        continue;
                    }
                    InvokeInstruction call = (InvokeInstruction) insn;
                    MethodReference ref = call.getMethod();
                    if (!ref.getClassName().equals(cls.getName()) || !falses.contains(ref.getDescriptor())) {
                        continue;
                    }
                    if (call.getReceiver() == null) {
                        call.delete();
                    } else {
                        IntegerConstantInstruction no = new IntegerConstantInstruction();
                        no.setConstant(0);
                        no.setReceiver(call.getReceiver());
                        no.setLocation(call.getLocation());
                        call.replace(no);
                    }
                }
            }
        }
    }

    // Whether a method returns false on every way through it, asking only
    // sets what they contain, helpers that do no more (asksOnly), and
    // Checks.reportMissing, which is false and gives a debug message: the
    // values known to be false, and the blocks a branch on one leaves
    // reachable, grown together till they settle.
    private static boolean alwaysFalse(ClassHolder cls, Program program) {
        Flow flow = new Flow(program);
        boolean[] zero = new boolean[program.variableCount()];
        for (int i = 0; i < program.basicBlockCount(); i++) {
            for (Instruction insn : program.basicBlockAt(i)) {
                if (insn instanceof IntegerConstantInstruction) {
                    IntegerConstantInstruction c = (IntegerConstantInstruction) insn;
                    zero[c.getReceiver().getIndex()] = c.getConstant() == 0;
                } else if (insn instanceof InvokeInstruction) {
                    InvokeInstruction call = (InvokeInstruction) insn;
                    MethodReference m = call.getMethod();
                    boolean missing = m.getClassName().equals(CHECKS) && m.getName().equals("reportMissing");
                    if (missing) {
                        zero[call.getReceiver().getIndex()] = true;
                    } else if (!(m.getClassName().equals("java.util.Set") && m.getName().equals("contains"))
                            && !asksOnly(cls, call)) {
                        return false;
                    }
                } else if (!(insn instanceof StringConstantInstruction || insn instanceof BinaryInstruction
                        || insn instanceof BranchingInstruction || insn instanceof JumpInstruction
                        || insn instanceof ExitInstruction || insn instanceof EmptyInstruction)) {
                    return false;
                }
            }
        }
        boolean[] reachable = new boolean[program.basicBlockCount()];
        reachable[0] = true;
        boolean changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < program.basicBlockCount(); i++) {
                BasicBlock block = program.basicBlockAt(i);
                if (!reachable[i]) {
                    continue;
                }
                for (Phi phi : block.getPhis()) {
                    boolean all = true;
                    for (Incoming in : phi.getIncomings()) {
                        all &= !reachable[in.getSource().getIndex()] || zero[in.getValue().getIndex()];
                    }
                    if (all && !zero[phi.getReceiver().getIndex()]) {
                        zero[phi.getReceiver().getIndex()] = true;
                        changed = true;
                    }
                }
                List<BasicBlock> next = new ArrayList<>();
                Instruction last = block.getLastInstruction();
                if (last instanceof JumpInstruction) {
                    next.add(((JumpInstruction) last).getTarget());
                } else if (last instanceof BranchingInstruction) {
                    BranchingInstruction branch = (BranchingInstruction) last;
                    boolean known = zero[branch.getOperand().getIndex()];
                    if (!known || branch.getCondition() == BranchingCondition.EQUAL) {
                        next.add(branch.getConsequent());
                    }
                    if (!known || branch.getCondition() == BranchingCondition.NOT_EQUAL) {
                        next.add(branch.getAlternative());
                    }
                    if (known && branch.getCondition() != BranchingCondition.EQUAL
                            && branch.getCondition() != BranchingCondition.NOT_EQUAL) {
                        return false;
                    }
                }
                for (BasicBlock b : next) {
                    if (!reachable[b.getIndex()]) {
                        reachable[b.getIndex()] = true;
                        changed = true;
                    }
                }
            }
        }
        boolean exits = false;
        for (int i = 0; i < program.basicBlockCount(); i++) {
            Instruction last = program.basicBlockAt(i).getLastInstruction();
            if (reachable[i] && last instanceof ExitInstruction) {
                Variable value = ((ExitInstruction) last).getValueToReturn();
                if (value == null || !zero[value.getIndex()]) {
                    return false;
                }
                exits = true;
            }
        }
        return exits && flow.def(program.variableAt(0)) == null;
    }

    // What made the folded checks' indices: a flag plus the function's place,
    // which goes, and the flag, ARB_foo(ext) ? 0 : Integer.MIN_VALUE, whose
    // helper's answer, if it only chose the flag, is false, the helper only
    // asking ext what it contains.
    private static void dropFlags(ClassHolder cls, Program program, List<Variable> values) {
        Flow flow = new Flow(program);
        for (Variable v : values) {
            if (flow.uses[v.getIndex()] == 0 && flow.def(v) instanceof BinaryInstruction) {
                flow.def(v).delete();
            }
        }
        flow = new Flow(program);
        for (int i = 0; i < program.basicBlockCount(); i++) {
            BasicBlock block = program.basicBlockAt(i);
            if (!(block.getLastInstruction() instanceof BranchingInstruction)) {
                continue;
            }
            BranchingInstruction branch = (BranchingInstruction) block.getLastInstruction();
            Variable answer = branch.getOperand();
            if (flow.uses[answer.getIndex()] != 1 || !(flow.def(answer) instanceof InvokeInstruction)
                    || !asksOnly(cls, (InvokeInstruction) flow.def(answer))) {
                continue;
            }
            BasicBlock join = constantsTo(flow, branch.getConsequent());
            if (join == null || join != constantsTo(flow, branch.getAlternative())) {
                continue;
            }
            boolean chosen = true;
            for (Phi phi : join.getPhis()) {
                for (Incoming in : phi.getIncomings()) {
                    if (in.getSource() == branch.getConsequent() || in.getSource() == branch.getAlternative()) {
                        chosen &= flow.uses[phi.getReceiver().getIndex()] == 0;
                    }
                }
            }
            if (chosen) {
                Instruction call = flow.def(answer);
                IntegerConstantInstruction no = new IntegerConstantInstruction();
                no.setConstant(0);
                no.setReceiver(answer);
                no.setLocation(call.getLocation());
                call.replace(no);
            }
        }
    }

    // The block a block jumps to, with nothing but constants before, or null.
    private static BasicBlock constantsTo(Flow flow, BasicBlock block) {
        for (Instruction insn : block) {
            if (insn instanceof JumpInstruction) {
                return ((JumpInstruction) insn).getTarget();
            }
            if (!(insn instanceof IntegerConstantInstruction)) {
                return null;
            }
        }
        return null;
    }

    // Whether a call is of a helper of the class's that only asks a set what
    // it contains: ARB_foo(ext), which is ext.contains("OpenGL30") ||
    // ext.contains("GL_ARB_foo").
    private static boolean asksOnly(ClassHolder cls, InvokeInstruction call) {
        MethodReference ref = call.getMethod();
        MethodHolder m = ref.getClassName().equals(cls.getName()) ? cls.getMethod(ref.getDescriptor()) : null;
        if (m == null || !m.hasModifier(ElementModifier.STATIC) || m.getProgram() == null) {
            return false;
        }
        Program program = m.getProgram();
        for (int i = 0; i < program.basicBlockCount(); i++) {
            for (Instruction insn : program.basicBlockAt(i)) {
                if (insn instanceof InvokeInstruction) {
                    MethodReference called = ((InvokeInstruction) insn).getMethod();
                    if (!called.getClassName().equals("java.util.Set") || !called.getName().equals("contains")) {
                        return false;
                    }
                } else if (!(insn instanceof StringConstantInstruction || insn instanceof IntegerConstantInstruction
                        || insn instanceof BranchingInstruction || insn instanceof JumpInstruction
                        || insn instanceof ExitInstruction || insn instanceof EmptyInstruction)) {
                    return false;
                }
            }
        }
        return true;
    }

    private static AnnotationHolder importOf(String symbol) {
        AnnotationHolder a = new AnnotationHolder(IMPORT);
        a.getValues().put("module", new AnnotationValue("lwjgl"));
        a.getValues().put("name", new AnnotationValue(symbol));
        return a;
    }

    // The C function a native method of class cls calls, or null for one C
    // cannot be handed the arguments of.
    static String symbol(String cls, MethodHolder m) {
        for (int i = 0; i < m.parameterCount(); i++) {
            if (!(m.parameterType(i) instanceof ValueType.Primitive)) {
                return null;
            }
        }
        ValueType result = m.getResultType();
        if (!(result instanceof ValueType.Primitive) && result != ValueType.VOID) {
            return null;
        }
        String method = m.getName();
        String simple = cls.substring(cls.lastIndexOf('.') + 1);
        if (cls.startsWith("org.lwjgl.opengl.")) {
            if (method.startsWith("ngl")) {
                return method.substring(1);
            }
            if (method.startsWith("gl")) {
                return method;
            }
        } else if (cls.startsWith("org.lwjgl.system.libc.") || cls.equals("org.lwjgl.system.macosx.DynamicLinkLoader")) {
            String function = method.startsWith("n") ? method.substring(1) : method;
            // what aligned_alloc returns is free's to free outside Windows
            return function.equals("aligned_free") ? "free" : function;
        }
        return "lwjgl_" + simple + "_" + method;
    }

    // The calls through JNI of one class whose functions are known by name, and
    // the methods made for them.
    private final class Direct {
        final ClassHolder cls;
        final ClassHolderTransformerContext context;
        final List<MethodHolder> added = new ArrayList<>();
        // the method made for a JNI method and a function
        final Map<String, MethodReference> made = new HashMap<>();

        Direct(ClassHolder cls, ClassHolderTransformerContext context) {
            this.cls = cls;
            this.context = context;
        }

        void calls(Program program) {
            Flow flow = new Flow(program);
            List<InvokeInstruction> invokes = new ArrayList<>();
            for (int i = 0; i < program.basicBlockCount(); i++) {
                for (Instruction insn : program.basicBlockAt(i)) {
                    if (insn instanceof InvokeInstruction
                            && ((InvokeInstruction) insn).getMethod().getClassName().equals(JNI)) {
                        invokes.add((InvokeInstruction) insn);
                    }
                }
            }
            for (InvokeInstruction invoke : invokes) {
                List<? extends Variable> args = invoke.getArguments();
                if (args.isEmpty()) {
                    continue;
                }
                Variable address = args.get(args.size() - 1);
                if (!(flow.def(address) instanceof GetFieldInstruction)) {
                    continue;
                }
                GetFieldInstruction get = (GetFieldInstruction) flow.def(address);
                FieldReference field = get.getField();
                String symbol = null;
                // not an optional function's, whose address LWJGL checks before
                // the call, as one GLFW lacks is NULL there
                if (get.getInstance() == null && field.getClassName().endsWith("$Functions")
                        && flow.uses[address.getIndex()] == 1) {
                    symbol = functionsOf(field.getClassName(), context).get(field.getFieldName());
                } else if (get.getInstance() != null && field.getClassName().equals(CAPABILITIES)) {
                    symbol = field.getFieldName();
                }
                if (symbol == null) {
                    continue;
                }
                MethodReference target = made(invoke.getMethod(), symbol);
                if (target != null) {
                    invoke.setMethod(target);
                    invoke.setArguments(args.subList(0, args.size() - 1).toArray(new Variable[0]));
                    // the read of a Functions field, which nothing else uses, and
                    // with it the class's initialization
                    if (get.getInstance() == null) {
                        get.delete();
                    }
                }
            }
        }

        // The method of this class that does what JNI's method jni does with the
        // function symbol in place of the one at its last argument, or null.
        MethodReference made(MethodReference jni, String symbol) {
            String key = jni.getDescriptor() + " " + symbol;
            if (made.containsKey(key)) {
                return made.get(key);
            }
            MethodReference result = null;
            ClassReader jniClass = context.getHierarchy().getClassSource().get(JNI);
            MethodReader method = jniClass != null ? jniClass.getMethod(jni.getDescriptor()) : null;
            if (method != null && method.getProgram() != null) {
                result = wrapper("warmc$" + symbol + "$" + made.size(), method, symbol);
            }
            made.put(key, result);
            return result;
        }

        // A native of this class, calling symbol, for JNI's native of descriptor
        // desc, which calls the address its last argument holds.
        MethodReference function(String name, MethodDescriptor desc, String symbol) {
            ValueType[] sig = desc.getSignature();
            ValueType[] params = Arrays.copyOf(sig, sig.length - 2);
            MethodHolder m = new MethodHolder(new MethodDescriptor(name, append(params, sig[sig.length - 1])));
            m.setLevel(AccessLevel.PRIVATE);
            m.getModifiers().add(ElementModifier.STATIC);
            m.getModifiers().add(ElementModifier.NATIVE);
            m.getAnnotations().add(importOf(symbol));
            added.add(m);
            // the method has no reference till it is the class's
            return new MethodReference(cls.getName(), m.getDescriptor());
        }

        // JNI's method, which calls its native of the address, copied into this
        // class with a call of symbol in its place.
        MethodReference wrapper(String base, MethodReader method, String symbol) {
            Program program = ProgramUtils.copy(method.getProgram());
            InvokeInstruction call = null;
            for (int i = 0; i < program.basicBlockCount() && call == null; i++) {
                for (Instruction insn : program.basicBlockAt(i)) {
                    if (insn instanceof InvokeInstruction
                            && ((InvokeInstruction) insn).getMethod().getClassName().equals(JNI)) {
                        call = (InvokeInstruction) insn;
                        break;
                    }
                }
            }
            if (call == null) {
                return null;
            }
            MethodReference native_ = function(base + "$c", call.getMethod().getDescriptor(), symbol);
            List<? extends Variable> args = call.getArguments();
            call.setMethod(native_);
            call.setArguments(args.subList(0, args.size() - 1).toArray(new Variable[0]));
            ValueType[] sig = method.getDescriptor().getSignature();
            ValueType[] params = Arrays.copyOf(sig, sig.length - 2);
            MethodHolder m = new MethodHolder(new MethodDescriptor(base, append(params, sig[sig.length - 1])));
            m.setLevel(AccessLevel.PRIVATE);
            m.getModifiers().add(ElementModifier.STATIC);
            m.setProgram(program);
            added.add(m);
            return new MethodReference(cls.getName(), m.getDescriptor());
        }
    }

    private static ValueType[] append(ValueType[] a, ValueType last) {
        ValueType[] r = Arrays.copyOf(a, a.length + 1);
        r[a.length] = last;
        return r;
    }

    // The function each field of a Functions class holds the address of, by the
    // name its initializer looks it up by.
    private Map<String, String> functionsOf(String className, ClassHolderTransformerContext context) {
        Map<String, String> known = functions.get(className);
        if (known != null) {
            return known;
        }
        known = new HashMap<>();
        functions.put(className, known);
        ClassReader cls = context.getHierarchy().getClassSource().get(className);
        MethodReader init = cls != null ? cls.getMethod(new MethodDescriptor("<clinit>", ValueType.VOID)) : null;
        if (init == null || init.getProgram() == null) {
            return known;
        }
        Program program = ProgramUtils.copy(init.getProgram());
        Map<Integer, String> strings = new HashMap<>();
        for (int i = 0; i < program.basicBlockCount(); i++) {
            for (Instruction insn : program.basicBlockAt(i)) {
                if (insn instanceof StringConstantInstruction) {
                    StringConstantInstruction s = (StringConstantInstruction) insn;
                    strings.put(s.getReceiver().getIndex(), s.getConstant());
                } else if (insn instanceof InvokeInstruction) {
                    InvokeInstruction invoke = (InvokeInstruction) insn;
                    MethodReference m = invoke.getMethod();
                    if (m.getClassName().equals("org.lwjgl.system.APIUtil")
                            && m.getName().startsWith("apiGetFunctionAddress")
                            && invoke.getReceiver() != null && invoke.getArguments().size() == 2) {
                        String s = strings.get(invoke.getArguments().get(1).getIndex());
                        if (s != null) {
                            strings.put(invoke.getReceiver().getIndex(), s);
                        }
                    }
                } else if (insn instanceof PutFieldInstruction) {
                    PutFieldInstruction put = (PutFieldInstruction) insn;
                    String s = strings.get(put.getValue().getIndex());
                    if (put.getInstance() == null && s != null && put.getField().getClassName().equals(className)) {
                        known.put(put.getField().getFieldName(), s);
                    }
                }
            }
        }
        return known;
    }

    private static void rewrite(Program program) {
        for (int i = 0; i < program.basicBlockCount(); i++) {
            BasicBlock block = program.basicBlockAt(i);
            Instruction insn = block.getFirstInstruction();
            while (insn != null) {
                Instruction next = insn.getNext();
                if (insn instanceof InvokeInstruction) {
                    InvokeInstruction invoke = (InvokeInstruction) insn;
                    MethodReference m = invoke.getMethod();
                    if (m.getClassName().equals("java.lang.invoke.MethodHandles") && m.getName().equals("lookup")) {
                        nullInPlace(insn, invoke.getReceiver());
                    } else if (m.getClassName().equals("java.lang.Runtime") && m.getName().equals("addShutdownHook")) {
                        invoke.setType(InvocationType.SPECIAL);
                        invoke.setInstance(null);
                        invoke.setMethod(new MethodReference(SETUP, "addShutdownHook",
                                ValueType.object("java.lang.Thread"), ValueType.VOID));
                    }
                }
                insn = next;
            }
        }
    }

    private static void nullInPlace(Instruction insn, Variable receiver) {
        if (receiver == null) {
            insn.delete();
            return;
        }
        NullConstantInstruction n = new NullConstantInstruction();
        n.setReceiver(receiver);
        n.setLocation(insn.getLocation());
        insn.replace(n);
    }
}
