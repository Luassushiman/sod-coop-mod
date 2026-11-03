// Lista as instruções que referenciam qualquer endereço numa faixa (ex.: uma tabela
// global), com o tipo de acesso e a função.
// Uso: -postScript RefsToRange.java <arquivo_saida> <ini_hex> <fim_hex>
//@category SoDCoop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import java.io.FileWriter;
import java.io.PrintWriter;

public class RefsToRange extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new FileWriter(args[0]));
		long ini = Long.parseLong(args[1].replace("0x", ""), 16);
		long fim = Long.parseLong(args[2].replace("0x", ""), 16);
		for (long v = ini; v < fim; v++) {
			Address a = toAddr(v);
			for (Reference r : getReferencesTo(a)) {
				Address from = r.getFromAddress();
				Function f = getFunctionContaining(from);
				Instruction ins = getInstructionAt(from);
				out.println(String.format("%08X <- %s %s in %s : %s", v, from, r.getReferenceType(),
						f == null ? "?" : f.getName() + "@" + f.getEntryPoint(), ins == null ? "" : ins.toString()));
			}
		}
		out.close();
	}
}
