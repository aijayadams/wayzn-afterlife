# Ghidra headless decompile of the STM32 dump

Needs Ghidra 12 (`brew install ghidra`) and a JDK 21 (`brew install openjdk@21`).

```sh
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home
G=/opt/homebrew/Cellar/ghidra/12.0/libexec/support/analyzeHeadless
mkdir -p /tmp/gh && $G /tmp/gh wayzn \
  -import firmware-dumps/stm32f051-u300.bin -overwrite \
  -loader BinaryLoader -loader-baseAddr 0x08000000 -processor ARM:LE:32:Cortex \
  -scriptPath tools/ghidra -preScript Prep.java \
  -postScript Label.java -postScript Export.java /tmp/gh/decomp.c
```

- `Prep.java`: adds RAM/peripheral blocks and creates functions at the vector table entries
  (Thumb). It logs one harmless context error for the second image at 0x08008400.
- `Label.java`: names the 45 shell handlers `cmd_<name>` from the table at 0x08007330
  and defines the strings.
- `Export.java <out.c>`: decompiles every function into one file.

The output goes to `firmware-dumps/ghidra/`. Neither the dump nor the decompile is published here: they are Wayzn's code. Dump your own board with tools/swd-dump. The findings are in
`docs/STM-FIRMWARE.md`.
