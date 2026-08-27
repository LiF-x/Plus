// Decompile FUN_140418710 + its one-hop callees. Confirms or refutes the
// "whole-event byte-copy serialization" hypothesis from
// docs/netevent_receive_path.md. Issue #56.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import java.io.*;
import java.util.*;

public class LifxFun418710 extends GhidraScript {
    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        Function f = fm.getFunctionAt(base.add(0x418710L));
        Set<Function> callees = new LinkedHashSet<>();
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "fun_418710.txt"))))) {
            if (f == null) { pw.println("(no fn at 0x418710)"); return; }
            DecompileResults r = dec.decompileFunction(f, 240, monitor);
            pw.printf("=== RVA 0x418710  %s ===%n", f.getName());
            pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
            pw.println();
            HighFunction hf = r.getHighFunction();
            if (hf != null) {
                Iterator<PcodeOpAST> ops = hf.getPcodeOps();
                while (ops.hasNext()) {
                    PcodeOpAST op = ops.next();
                    if (op.getOpcode() != PcodeOp.CALL) continue;
                    Varnode tgt = op.getInput(0);
                    if (tgt == null || !tgt.isAddress()) continue;
                    Function cf = fm.getFunctionAt(tgt.getAddress());
                    if (cf != null && cf != f) callees.add(cf);
                }
            }
            for (Function cf : callees) {
                long frva = cf.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== callee RVA 0x%X  %s ===%n", frva, cf.getName());
                DecompileResults rr = dec.decompileFunction(cf, 180, monitor);
                pw.println(rr.decompileCompleted() ? rr.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/fun_418710.txt");
    }
}
