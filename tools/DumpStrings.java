import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class DumpStrings extends GhidraScript {
    @Override
    public void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        for (String argument : getScriptArgs()) {
            Address address = toAddr(Long.decode(argument));
            StringBuilder text = new StringBuilder();
            for (int i = 0; i < 512; i++) {
                byte b = memory.getByte(address.add(i));
                if (b == 0) break;
                text.append((char) (b & 0xff));
            }
            println(String.format("%s: \"%s\"", address, text));
        }
    }
}
