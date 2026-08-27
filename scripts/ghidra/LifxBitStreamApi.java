// Issue #68: locate the engine's BitStream write/read API. We already know
// BitStream::readInt = FUN_140448580 (PR #57). Dump every function in the
// 0x140448000..0x140449000 neighbourhood and the full send-drainer body
// (FUN_140542630) so we can identify writeInt / writeBits / writeBuffer
// and their calling conventions before wiring our pack()/unpack().
//
// Output: /tmp/lifx_ghidra/bitstream_api.txt
//@category LiFx

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class LifxBitStreamApi extends GhidraScript {

    private static final long BS_REGION_START = 0x140448000L;
    private static final long BS_REGION_END   = 0x14044A000L;

    // Already known anchors for cross-reference.
    private static final long FUN_READINT      = 0x140448580L;
    private static final long FUN_READMAGIC    = 0x140448A30L;
    // Full send drainer — calls real pack on each queued event.
    private static final long FUN_SEND_DRAINER = 0x140542630L;

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
                new FileWriter(new File(outDir, "bitstream_api.txt"))))) {

            pw.println("=== BitStream-neighborhood functions (0x140448000..0x14044A000) ===");
            FunctionIterator fns = fm.getFunctions(true);
            List<Function> bsFns = new ArrayList<>();
            while (fns.hasNext()) {
                Function f = fns.next();
                long va = f.getEntryPoint().getOffset();
                if (va < BS_REGION_START || va >= BS_REGION_END) continue;
                bsFns.add(f);
            }
            pw.printf("(%d functions in region)%n%n", bsFns.size());
            for (Function f : bsFns) {
                long frva = f.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("=== RVA 0x%X  %s ===%n", frva, f.getName());
                DecompileResults r = dec.decompileFunction(f, 180, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }

            // Full send-drainer dump. Look for vtable slot calls AND any
            // BitStream:: methods invoked between them.
            Function drainer = fm.getFunctionAt(base.add(FUN_SEND_DRAINER - base.getOffset()));
            if (drainer == null) drainer = fm.getFunctionAt(prog.getAddressFactory().getAddress(Long.toHexString(FUN_SEND_DRAINER)));
            if (drainer != null) {
                pw.println("=== full send drainer FUN_140542630 ===");
                long frva = drainer.getEntryPoint().getOffset() - base.getOffset();
                pw.printf("(RVA 0x%X)%n", frva);
                DecompileResults r = dec.decompileFunction(drainer, 300, monitor);
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "(decompile failed)");
                pw.println();
            }
        }
        println("DONE. /tmp/lifx_ghidra/bitstream_api.txt");
    }
}
