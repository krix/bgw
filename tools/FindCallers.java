import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class FindCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        for (String argument : getScriptArgs()) {
            Address target = toAddr(Long.decode(argument));
            println("TARGET " + target);
            for (Reference reference : getReferencesTo(target)) {
                Function function = getFunctionContaining(reference.getFromAddress());
                println("CALL " + reference.getFromAddress() + " -> " +
                    (function == null ? "<none>" : function.getEntryPoint()));
                if (function != null) {
                    DecompileResults result = decompiler.decompileFunction(function, 30, monitor);
                    if (result != null && result.decompileCompleted()) {
                        println(result.getDecompiledFunction().getC());
                    }
                }
            }
        }
        decompiler.dispose();
    }
}
