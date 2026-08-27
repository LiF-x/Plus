// #154 render: learn the movable-image id-space.
// Decompile Mount_movable_object (0xEBA30) + its ShapeBaseImageData resolver (0x120B80),
// and decompile every direct CALL caller of 0xEBA30 to see what typeId values real
// equip/mount code passes (item ObjectTypeID? a separate movable id?).
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxMountResolve extends GhidraScript {

    private static final long MOUNT = 0x0EBA30L;     // Player::Mount_movable_object
    private static final long RESOLVE = 0x120B80L;   // ShapeBaseImageData-by-typeId resolver

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra"); outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        ReferenceManager rm = prog.getReferenceManager();
        DecompInterface dec = new DecompInterface(); dec.openProgram(prog);

        File out = new File(outDir, "weapon_mount_resolve.txt");
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (long t : new long[]{MOUNT, RESOLVE}) {
                Address a = base.add(t);
                Function f = fm.getFunctionAt(a);
                pw.printf("%n========== RVA 0x%X  %s ==========%n", t, (f==null?"?":f.getName()));
                if (f == null) continue;
                pw.println("-- callees --");
                for (Function c : f.getCalledFunctions(monitor)) {
                    Address ep = c.getEntryPoint();
                    if (c.isExternal() || ep == null || !ep.getAddressSpace().equals(base.getAddressSpace()))
                        pw.printf("   (extern)  %s%n", c.getName());
                    else
                        pw.printf("   0x%X  %s%n", ep.subtract(base), c.getName());
                }
                DecompileResults res = dec.decompileFunction(f, 120, monitor);
                pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
            }

            pw.println();
            pw.println("==================== CALLERS of Mount_movable_object (0xEBA30) ====================");
            LinkedHashSet<Long> callers = new LinkedHashSet<>();
            ReferenceIterator ri = rm.getReferencesTo(base.add(MOUNT));
            while (ri.hasNext()) {
                Reference r = ri.next();
                if (!r.getReferenceType().isCall()) continue;
                Function ff = fm.getFunctionContaining(r.getFromAddress());
                if (ff != null) callers.add(ff.getEntryPoint().subtract(base));
            }
            int n = 0;
            for (long rva : callers) {
                if (n++ >= 12) { pw.println("(capped at 12)"); break; }
                Function f = fm.getFunctionAt(base.add(rva));
                pw.printf("%n---------- caller RVA 0x%X  %s ----------%n", rva, (f==null?"?":f.getName()));
                if (f == null) continue;
                DecompileResults res = dec.decompileFunction(f, 120, monitor);
                pw.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "(decompile failed)");
            }
        }
        println("DONE -> " + out.getAbsolutePath());
    }
}
