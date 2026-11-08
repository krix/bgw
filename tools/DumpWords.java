import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class DumpWords extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long startValue = args.length > 0 ? Long.decode(args[0]) : 0x45a498L;
        int count = args.length > 1 ? Integer.parseInt(args[1]) : 13 * 13;
        Address start = toAddr(startValue);
        for (int row = 0; row < count; row++) {
            Address address = start.add(row * 0x34L);
            StringBuilder line = new StringBuilder();
            line.append(String.format("%s:", address));
            for (int offset = 0; offset < 0x34; offset += 4) {
                line.append(String.format(" %08x", currentProgram.getMemory().getInt(address.add(offset))));
            }
            println(line.toString());
        }
    }
}
