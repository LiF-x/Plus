// Follow-up wire-format dump: the actual send-side functions referenced from
// the script bindings + the cmDispUnitManager::processServerRequest opcode
// helpers. See issue #48.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class LifxDspWireFormat2 extends GhidraScript {

    // Targets are: ServerUUIDEvent::send (the real pack site), the request-
    // side dispatch (cmDispUnitManager::processServerRequest), each request
    // opcode's helper, the DspUtil charId-> ? helper used by opcode 2, and
    // the script-binding factory that builds NetEvent metadata. Last one is a
    // utility so we can mechanically resolve event NAMES later.
    private static final long[] RVAS = {
        0x4E7370L, // ServerUUIDEvent::send (pack site)
        0x4E7930L, // YoPatchTerCachedDataRequestEvent::create body
        0x3CA570L, // cmDispUnitManager::processServerRequest (already seen, dump for completeness)
        0x3C9AA0L, // request opcode 1 helper
        0x5367A0L, // DspUtil lookup helper used by opcode 2
        0x3C90F0L, // opcode 2 send helper
        0x3C9FE0L, // opcode 9 helper
        0x3CAB70L, // opcode 10 helper
        0x3CAAC0L, // opcode 0xb helper
        0x3CA9B0L, // opcode 0xe helper
        0x41DF20L, // EngineFunction::add (script-binding factory used by classreg slots)
    };

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Address base = prog.getImageBase();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "dsp_wire_send.txt"))))) {
            for (long rva : RVAS) {
                Function f = fm.getFunctionAt(base.add(rva));
                if (f == null) { pw.printf("=== RVA 0x%X (no fn) ===%n%n", rva); continue; }
                pw.printf("=== RVA 0x%X  %s ===%n", rva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 240, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/dsp_wire_send.txt");
    }
}
