import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class ExportGhidra extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("usage: ExportGhidra.java <output-file> [address ...]");
            return;
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);

        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                long value = Long.decode(args[i]);
                Address address = toAddr(value);
                Function function = getFunctionContaining(address);
                if (function == null) {
                    disassemble(address);
                    function = createFunction(address, null);
                    if (function == null) {
                        function = getFunctionContaining(address);
                    }
                }
                out.println("============================================================");
                out.printf("REQUESTED ADDRESS: 0x%X%n", value);
                if (function == null) {
                    out.println("FUNCTION: <none>");
                    continue;
                }
                out.printf("FUNCTION: %s @ %s%n", function.getName(), function.getEntryPoint());
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (result != null && result.decompileCompleted()) {
                    out.println(result.getDecompiledFunction().getC());
                } else {
                    out.println("DECOMPILE FAILED: " +
                        (result == null ? "null result" : result.getErrorMessage()));
                }
            }
        }
        decompiler.dispose();
    }
}
