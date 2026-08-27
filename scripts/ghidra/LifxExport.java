// Ghidra headless export: symbols, strings with xrefs, RVA validation.
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxExport extends GhidraScript {

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Listing listing = prog.getListing();
        Address base = prog.getImageBase();
        Memory mem = prog.getMemory();
        SymbolTable st = prog.getSymbolTable();

        println("Image base: " + base);

        // -------- offset validation --------
        LinkedHashMap<String,Long> targets = new LinkedHashMap<>();
        targets.put("CON_INTERNAL_PRINTF",   0x405090L);
        targets.put("CON_ADD_CONSTANT",      0x406680L);
        targets.put("CON_EVALUATE",          0x406A50L);
        targets.put("CON_GET_VARIABLE",      0x4077B0L);
        targets.put("CON_INIT",              0x407990L);
        targets.put("CON_LOOKUP_NAMESPACE",  0x4082B0L);
        targets.put("CON_SET_VARIABLE",      0x408CD0L);
        targets.put("CON_ADD_INT_COMMAND",   0x410F90L);
        targets.put("CON_ADD_FLOAT_COMMAND", 0x411000L);
        targets.put("CON_ADD_STRING_COMMAND",0x411070L);
        targets.put("CON_ADD_VOID_COMMAND",  0x4110E0L);
        targets.put("CON_ADD_BOOL_COMMAND",  0x411150L);
        targets.put("CON_ADD_VARIABLE",      0x411360L);
        targets.put("STRING_TABLE_INSERT",   0x441BF0L);

        try (PrintWriter pw = new PrintWriter(new File(outDir, "offset_check.txt"))) {
            pw.println("name\trva\taddress\tin_function\tprologue_bytes");
            for (Map.Entry<String,Long> e : targets.entrySet()) {
                Address addr = base.add(e.getValue());
                Function f = fm.getFunctionContaining(addr);
                StringBuilder hex = new StringBuilder();
                try {
                    byte[] b = new byte[16];
                    mem.getBytes(addr, b);
                    for (byte x : b) hex.append(String.format("%02X ", x & 0xFF));
                } catch (Exception ex) { hex.append("<unreadable>"); }
                String fname = (f != null) ? f.getName() : "<no function>";
                pw.printf("%s\t0x%X\t%s\t%s\t%s%n", e.getKey(), e.getValue(), addr, fname, hex.toString().trim());
                println(String.format("%-24s RVA 0x%X -> %s -> %s", e.getKey(), e.getValue(), addr, fname));
            }
        }

        // -------- functions list --------
        int n = 0;
        try (PrintWriter pw = new PrintWriter(new File(outDir, "functions.tsv"))) {
            pw.println("rva\taddress\tname\tnamespace\tsignature");
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                Address a = f.getEntryPoint();
                long rva = a.getOffset() - base.getOffset();
                Namespace ns = f.getParentNamespace();
                String nss = (ns != null) ? ns.getName(true) : "";
                pw.printf("0x%X\t%s\t%s\t%s\t%s%n", rva, a, f.getName(), nss, f.getPrototypeString(false, false));
                n++;
            }
        }
        println("Wrote " + n + " functions");

        // -------- strings + xrefs --------
        n = 0;
        try (PrintWriter pw = new PrintWriter(new File(outDir, "strings.tsv"))) {
            pw.println("rva\taddress\tlen\ttype\tvalue\txref_funcs");
            DataIterator di = listing.getDefinedData(true);
            while (di.hasNext()) {
                Data d = di.next();
                String dn = d.getDataType().getName().toLowerCase();
                if (!(dn.contains("string") || dn.contains("char"))) continue;
                StringDataInstance sd = StringDataInstance.getStringDataInstance(d);
                String s = sd.getStringValue();
                if (s == null || s.length() < 3) continue;
                Address a = d.getAddress();
                long rva = a.getOffset() - base.getOffset();
                TreeSet<String> funcs = new TreeSet<>();
                ReferenceIterator ri = d.getReferenceIteratorTo();
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function fn = fm.getFunctionContaining(r.getFromAddress());
                    funcs.add(fn != null ? fn.getName() : r.getFromAddress().toString());
                }
                String sval = s.replace("\t","\\t").replace("\n","\\n").replace("\r","\\r");
                pw.printf("0x%X\t%s\t%d\t%s\t%s\t%s%n", rva, a, s.length(), dn, sval, String.join(",", funcs));
                n++;
            }
        }
        println("Wrote " + n + " strings");

        // -------- classes/namespaces (RTTI-recovered) --------
        TreeSet<String> classes = new TreeSet<>();
        SymbolIterator si = st.getAllSymbols(true);
        while (si.hasNext()) {
            Symbol sym = si.next();
            Namespace ns = sym.getParentNamespace();
            if (ns == null || ns.isGlobal()) continue;
            classes.add(ns.getName(true));
        }
        try (PrintWriter pw = new PrintWriter(new File(outDir, "classes.txt"))) {
            for (String c : classes) pw.println(c);
        }
        println("Wrote " + classes.size() + " namespaces/classes");
        println("DONE: outputs in " + outDir.getAbsolutePath());
    }
}
