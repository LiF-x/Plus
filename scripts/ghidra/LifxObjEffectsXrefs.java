// Find every reference to ObjEffectsEvent::vftable and decompile the enclosing
// function. These are the construction sites — i.e. the places that send an
// effect change to the client. The one we want is the per-row-update sender
// that fires when the server expires/removes an effect.
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxObjEffectsXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        SymbolTable st = prog.getSymbolTable();
        Address base = prog.getImageBase();

        // Find the ObjEffectsEvent vtable symbol.
        Address vftAddr = null;
        SymbolIterator si = st.getAllSymbols(true);
        while (si.hasNext()) {
            Symbol s = si.next();
            String name = s.getName();
            if (!"vftable".equals(name) && !"`vftable'".equals(name) && !name.endsWith("vftable")) continue;
            Namespace ns = s.getParentNamespace();
            if (ns == null) continue;
            if ("ObjEffectsEvent".equals(ns.getName())) {
                vftAddr = s.getAddress();
                break;
            }
        }
        if (vftAddr == null) {
            println("ObjEffectsEvent::vftable not found");
            return;
        }
        println("vtable @ " + vftAddr);

        Set<Function> callers = new LinkedHashSet<>();
        ReferenceIterator ri = prog.getReferenceManager().getReferencesTo(vftAddr);
        try (PrintWriter pwList = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "objeffectsevent_xrefs.tsv"))))) {
            pwList.println("from_func_rva\tfrom_func\tfrom_addr");
            while (ri.hasNext()) {
                Reference r = ri.next();
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f == null) continue;
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pwList.printf("0x%X\t%s\t%s%n", frva, f.getName(), r.getFromAddress());
                callers.add(f);
            }
        }
        println("Unique callers: " + callers.size());

        DecompInterface dec = new DecompInterface();
        dec.openProgram(prog);
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "objeffectsevent_callers_decomp.txt"))))) {
            for (Function f : callers) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults res = dec.decompileFunction(f, 120, monitor);
                if (res.decompileCompleted()) {
                    pw.println(res.getDecompiledFunction().getC());
                } else {
                    pw.println("(decompile failed)");
                }
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/objeffectsevent_*.{tsv,txt}");
    }
}
