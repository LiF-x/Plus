// Dump vtables by symbol name (args), side by side, with slot RVAs + target names.
// Usage: -postScript LifxVtableDump.java "Lands::BattleZoneLand::vftable" "Lands::GuildLand::vftable" ...
// Output: /tmp/lifx_ghidra/vtable_dump.txt
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.MemoryAccessException;
import java.io.*;
import java.util.*;

public class LifxVtableDump extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] argv = getScriptArgs();
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        SymbolTable st = prog.getSymbolTable();
        FunctionManager fm = prog.getFunctionManager();

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "vtable_dump.txt"))))) {
            for (String name : argv) {
                pw.printf("===== %s =====%n", name);
                Address va = null;
                if (name.startsWith("0x")) {
                    va = base.add(Long.decode(name));
                } else {
                    SymbolIterator it = st.getAllSymbols(true);
                    while (it.hasNext()) {
                        Symbol s = it.next();
                        if (s.getName(true).equals(name) || s.getName(true).endsWith(name)) {
                            va = s.getAddress(); break;
                        }
                    }
                }
                if (va == null) { pw.printf("  (symbol not found)%n%n"); continue; }
                pw.printf("  vtable VA=%s RVA=0x%X%n", va, va.subtract(base));
                for (int slot = 0; slot < 40; slot++) {
                    Address slotAddr = va.add((long) slot * 8);
                    long ptr;
                    try { ptr = prog.getMemory().getLong(slotAddr); }
                    catch (MemoryAccessException e) { break; }
                    if (ptr == 0) break;
                    Address target;
                    try { target = base.getNewAddress(ptr); } catch (Exception e) { break; }
                    Function f = fm.getFunctionAt(target);
                    if (f == null) {
                        // stop at first non-function (end of vtable) but only after slot 0
                        if (slot > 0) break;
                    }
                    long trva = ptr - base.getOffset();
                    pw.printf("    [%2d] 0x%06X  %s%n", slot, trva, f == null ? "(not a function)" : f.getName());
                }
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/vtable_dump.txt");
    }
}
