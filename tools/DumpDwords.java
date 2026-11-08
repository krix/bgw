import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class DumpDwords extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long startValue = Long.decode(args[0]);
        int count = Integer.parseInt(args[1]);
        int stride = args.length > 2 ? Integer.parseInt(args[2]) : 4;
        Address start = toAddr(startValue);
        for (int row = 0; row < count; row++) {
            Address address = start.add((long) row * stride);
            println(String.format("%s: %08x", address,
                currentProgram.getMemory().getInt(address)));
        }
    }
}
