// Decompile the four built-in AI behavior-node registration modules. The
// outputs feed scripts/parse_node_registrations.py which produces the
// XML-class-name -> C++-class catalog documented in docs/ai_and_spawning.md §3.4.
//
// Run via:
//   ~/.local/share/ghidra/support/analyzeHeadless ~/ghidra_projects LiF \
//       -process ddctd_cm_yo_server.exe -noanalysis \
//       -scriptPath scripts/ghidra -postScript LifxAllRegs.java
//
// Each module init is vtable slot 1 of its _ModuleInst:
//   _CommonBehaviorNodes::init   0x1513B0
//   _AnimalBehaviorNodes::init   0x18E8A0
//   _NPCBehaviorNodes::init      0x2E5FC0
//   _HorseBehaviorNodes::init    0x3E9210
//
// Outputs land in /tmp/lifx_ghidra/decompile/reg_full_<RVA>.c
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class LifxAllRegs extends GhidraScript {

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra/decompile");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();

        long[] targets = {
            0x1513B0L,   // _CommonBehaviorNodes::init
            0x18E8A0L,   // _AnimalBehaviorNodes::init
            0x2E5FC0L,   // _NPCBehaviorNodes::init
            0x3E9210L,   // _HorseBehaviorNodes::init
        };

        DecompInterface di = new DecompInterface();
        di.openProgram(prog);
        di.setSimplificationStyle("decompile");
        DecompileOptions opts = new DecompileOptions();
        opts.setMaxPayloadMBytes(200);   // AnimalBehaviorNodes::init is ~530 decompiled lines
        di.setOptions(opts);

        for (long rva : targets) {
            Address addr = base.add(rva);
            Function f = fm.getFunctionAt(addr);
            if (f == null) {
                println("skip 0x" + Long.toHexString(rva));
                continue;
            }
            DecompileResults r = di.decompileFunction(f, 300, monitor);
            File out = new File(outDir, "reg_full_" + String.format("%X", rva) + ".c");
            try (PrintWriter pw = new PrintWriter(out)) {
                pw.println("// 0x" + Long.toHexString(rva) + "  " + f.getName());
                pw.println(r.decompileCompleted()
                    ? r.getDecompiledFunction().getC()
                    : "// FAIL: " + r.getErrorMessage());
            }
            println("Wrote " + out.getName());
        }
        di.dispose();
    }
}
