// Locate the resurrection-sickness apply-effect call site in the LiF server.
//
// Strategy: cross-reference three anchors that the resurrection handler must
// touch — the icon path string, the "Resurrected" message text, and the
// English/internal name strings — then rank candidate functions by how many
// anchors they share. Functions that appear in more than one anchor set are
// strong apply-site candidates.
//
// Outputs (in /tmp/lifx_ghidra/):
//   - res_icon_xrefs.tsv         functions that reference "Resurrection.png"
//   - res_name_xrefs.tsv         functions that reference any string containing
//                                "Resurrect" (case-insensitive)
//   - res_number_xrefs.tsv       functions that PUSH/MOV the immediate 47 OR 93
//                                (effect IDs) — narrow this with the others
//   - res_fan_in.tsv             functions ranked by how many of the above
//                                anchor sets contain them
//
//@category LiFx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.lang.OperandType;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxResurrectionScan extends GhidraScript {

    @Override
    public void run() throws Exception {
        File outDir = new File("/tmp/lifx_ghidra");
        outDir.mkdirs();

        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        Listing listing = prog.getListing();
        Address base = prog.getImageBase();

        Set<Function> iconFns = new HashSet<>();
        Set<Function> nameFns = new HashSet<>();
        Set<Function> numFns  = new HashSet<>();

        // --- Pass 1: string anchors (icon path + name) ---
        try (PrintWriter pwIcon = pw(outDir, "res_icon_xrefs.tsv");
             PrintWriter pwName = pw(outDir, "res_name_xrefs.tsv")) {

            pwIcon.println("string_rva\tvalue\tfrom_func_rva\tfrom_func\tfrom_addr");
            pwName.println("string_rva\tvalue\tfrom_func_rva\tfrom_func\tfrom_addr");

            DataIterator di = listing.getDefinedData(true);
            while (di.hasNext()) {
                Data d = di.next();
                String dn = d.getDataType().getName().toLowerCase();
                if (!(dn.contains("string") || dn.contains("char"))) continue;
                StringDataInstance sd = StringDataInstance.getStringDataInstance(d);
                String s = sd.getStringValue();
                if (s == null) continue;
                String lower = s.toLowerCase();

                boolean isIcon = s.contains("Resurrection.png");
                boolean isName = lower.contains("resurrect");
                if (!isIcon && !isName) continue;

                Address a = d.getAddress();
                long srva = a.getOffset() - base.getOffset();
                ReferenceIterator ri = d.getReferenceIteratorTo();
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Address from = r.getFromAddress();
                    Function f = fm.getFunctionContaining(from);
                    if (f == null) continue;
                    long frva = f.getEntryPoint().getOffset() - base.getOffset();
                    String esc = s.replace("\t","\\t").replace("\n","\\n");
                    if (isIcon) { pwIcon.printf("0x%X\t%s\t0x%X\t%s\t%s%n", srva, esc, frva, f.getName(), from); iconFns.add(f); }
                    if (isName) { pwName.printf("0x%X\t%s\t0x%X\t%s\t%s%n", srva, esc, frva, f.getName(), from); nameFns.add(f); }
                }
            }
        }
        println("Icon-string callers: " + iconFns.size());
        println("Name-string callers: " + nameFns.size());

        // --- Pass 2: instructions that materialize the immediate effect IDs 47 or 93 ---
        // We look for any MOV/PUSH-style operation whose scalar operand equals
        // 47 (0x2F) or 93 (0x5D). False positives are common — that's why we
        // only use this set as a tie-breaker in fan_in below, never alone.
        try (PrintWriter pwNum = pw(outDir, "res_number_xrefs.tsv")) {
            pwNum.println("imm\tfrom_func_rva\tfrom_func\tfrom_addr\tinsn");
            InstructionIterator it = listing.getInstructions(true);
            int hits = 0;
            while (it.hasNext()) {
                Instruction ins = it.next();
                int nops = ins.getNumOperands();
                for (int op = 0; op < nops; op++) {
                    Object[] objs = ins.getOpObjects(op);
                    for (Object o : objs) {
                        if (!(o instanceof Scalar)) continue;
                        long v = ((Scalar) o).getValue();
                        if (v != 47 && v != 93) continue;
                        // Skip obvious noise: prologue 'sub rsp, 0x...' rarely has 47/93.
                        Function f = fm.getFunctionContaining(ins.getAddress());
                        if (f == null) continue;
                        long frva = f.getEntryPoint().getOffset() - base.getOffset();
                        pwNum.printf("%d\t0x%X\t%s\t%s\t%s%n", v, frva, f.getName(), ins.getAddress(), ins);
                        numFns.add(f);
                        hits++;
                        if (hits > 5000) break;   // safety
                    }
                    if (hits > 5000) break;
                }
                if (hits > 5000) { println("Stopped at 5000 imm-47/93 hits"); break; }
            }
        }
        println("Imm-47/93 callers: " + numFns.size());

        // --- Pass 3: rank by anchor-set overlap ---
        Set<Function> universe = new HashSet<>();
        universe.addAll(iconFns); universe.addAll(nameFns); universe.addAll(numFns);
        List<Map.Entry<Function, Integer>> ranked = new ArrayList<>();
        for (Function f : universe) {
            int score = (iconFns.contains(f) ? 1 : 0)
                      + (nameFns.contains(f) ? 1 : 0)
                      + (numFns.contains(f)  ? 1 : 0);
            ranked.add(new AbstractMap.SimpleEntry<>(f, score));
        }
        ranked.sort((a, b) -> Integer.compare(b.getValue(), a.getValue()));

        try (PrintWriter pw = pw(outDir, "res_fan_in.tsv")) {
            pw.println("score\tin_icon\tin_name\tin_imm\tfunc_rva\tfunc");
            for (Map.Entry<Function, Integer> e : ranked) {
                Function f = e.getKey();
                long rva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("%d\t%d\t%d\t%d\t0x%X\t%s%n",
                    e.getValue(),
                    iconFns.contains(f) ? 1 : 0,
                    nameFns.contains(f) ? 1 : 0,
                    numFns.contains(f)  ? 1 : 0,
                    rva, f.getName());
            }
        }
        println("Ranked " + ranked.size() + " candidates");
        println("DONE. /tmp/lifx_ghidra/res_*.tsv");
    }

    private static PrintWriter pw(File dir, String name) throws IOException {
        return new PrintWriter(new BufferedWriter(new FileWriter(new File(dir, name))));
    }
}
