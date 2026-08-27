// Scan all instructions for scalar (immediate) operands matching given values,
// report containing function + address + matched value.
// Usage: -postScript LifxConstXrefs.java 0xB34 0xB37 0xB38 ...
// Output: /tmp/lifx_ghidra/const_xrefs.txt
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.util.*;

public class LifxConstXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] argv = getScriptArgs();
        Set<Long> wanted = new LinkedHashSet<>();
        for (String s : argv) wanted.add(Long.decode(s));

        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();
        Program prog = currentProgram;
        Address base = prog.getImageBase();
        FunctionManager fm = prog.getFunctionManager();
        Listing listing = prog.getListing();

        // func RVA -> set of matched values
        Map<Long, Set<Long>> hitsByFunc = new TreeMap<>();
        Map<Long, String> funcName = new HashMap<>();
        List<String> rows = new ArrayList<>();

        InstructionIterator it = listing.getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            int nOps = ins.getNumOperands();
            for (int op = 0; op < nOps; op++) {
                Object[] objs = ins.getOpObjects(op);
                for (Object o : objs) {
                    if (o instanceof Scalar) {
                        long v = ((Scalar) o).getUnsignedValue();
                        if (wanted.contains(v)) {
                            Function f = fm.getFunctionContaining(ins.getAddress());
                            long frva = f == null ? -1 : f.getEntryPoint().subtract(base);
                            if (f != null) {
                                hitsByFunc.computeIfAbsent(frva, k -> new TreeSet<>()).add(v);
                                funcName.put(frva, f.getName());
                            }
                            rows.add(String.format("0x%06X  val=0x%X (%d)  func=%s@0x%X",
                                    ins.getAddress().subtract(base), v, v,
                                    f == null ? "(none)" : f.getName(), frva));
                        }
                    }
                }
            }
        }

        try (PrintWriter pw = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "const_xrefs.txt"))))) {
            pw.println("===== functions ranked by # distinct matched values =====");
            List<Long> funcs = new ArrayList<>(hitsByFunc.keySet());
            funcs.sort((a, b) -> Integer.compare(hitsByFunc.get(b).size(), hitsByFunc.get(a).size()));
            for (Long frva : funcs) {
                Set<Long> vs = hitsByFunc.get(frva);
                StringBuilder sb = new StringBuilder();
                for (Long v : vs) sb.append(String.format("0x%X(%d) ", v, v));
                pw.printf("  %s@0x%X  [%d vals] %s%n", funcName.get(frva), frva, vs.size(), sb);
            }
            pw.println();
            pw.println("===== raw hits =====");
            for (String r : rows) pw.println(r);
        }
        println("DONE. /tmp/lifx_ghidra/const_xrefs.txt");
    }
}
