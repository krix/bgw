import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;

public class FindStringXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String needle = getScriptArgs().length == 0 ? ".map" : getScriptArgs()[0];
        Memory memory = currentProgram.getMemory();
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        for (var block : memory.getBlocks()) {
            if (!block.isInitialized()) continue;
            for (Address address = block.getStart();
                 !monitor.isCancelled() && address.compareTo(block.getEnd()) <= 0;
                 address = address.next()) {
                if (address.add(needle.length() - 1).compareTo(block.getEnd()) > 0) break;
                if (memory.getByte(address) != needle.charAt(0)) continue;
                StringBuilder text = new StringBuilder();
                for (int i = 0; i < needle.length(); i++) {
                    text.append((char) memory.getByte(address.add(i)));
                }
                if (!needle.contentEquals(text)) continue;
                println("STRING " + address);
                for (Reference reference : getReferencesTo(address)) {
                    Function function = getFunctionContaining(reference.getFromAddress());
                    println("  XREF " + reference.getFromAddress() + " -> " +
                        (function == null ? "<none>" : function.getEntryPoint()));
                    if (function != null) {
                        DecompileResults result = decompiler.decompileFunction(function, 30, monitor);
                        if (result != null && result.decompileCompleted()) println(result.getDecompiledFunction().getC());
                    }
                }
            }
        }
        decompiler.dispose();
    }
}
